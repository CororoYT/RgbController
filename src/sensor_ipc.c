#include "sensor_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE g_hProc = NULL;
static HANDLE g_hInWrite = NULL;
static HANDLE g_hOutRead = NULL;
static CRITICAL_SECTION g_cs;
static bool g_cs_init = false;
static char g_exe_path[MAX_PATH] = {0};
static char g_exe_dir[MAX_PATH]  = {0};

static bool read_line_pipe(HANDLE h, char *buf, size_t max_len) {
    size_t pos = 0;
    char ch = 0;
    DWORD rd = 0;
    while (pos + 1 < max_len) {
        if (!ReadFile(h, &ch, 1, &rd, NULL) || rd == 0) {
            buf[pos] = '\0';
            return pos > 0;
        }
        if (ch == '\n') break;
        if (ch != '\r') buf[pos++] = ch;
    }
    buf[pos] = '\0';
    return true;
}

static bool write_cmd_pipe(const char *cmd, char *resp, size_t resp_sz) {
    if (!g_hProc || !g_hInWrite || !g_hOutRead) return false;
    DWORD wr = 0;
    size_t len = strlen(cmd);
    if (!WriteFile(g_hInWrite, cmd, (DWORD)len, &wr, NULL)) return false;
    if (!WriteFile(g_hInWrite, "\n", 1, &wr, NULL)) return false;
    return read_line_pipe(g_hOutRead, resp, resp_sz);
}

bool bridge_init(const char *base_dir) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cs);
        g_cs_init = true;
    }
    EnterCriticalSection(&g_cs);

    snprintf(g_exe_dir, sizeof(g_exe_dir), "%s\\tools\\LibreHardwareMonitor", base_dir);
    snprintf(g_exe_path, sizeof(g_exe_path), "%s\\SensorBridge.exe", g_exe_dir);

    if (GetFileAttributesA(g_exe_path) == INVALID_FILE_ATTRIBUTES) {
        LeaveCriticalSection(&g_cs);
        return false;
    }

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hInRead = NULL, hInWrite = NULL, hOutRead = NULL, hOutWrite = NULL;
    if (!CreatePipe(&hInRead, &hInWrite, &sa, 0) || !CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
        LeaveCriticalSection(&g_cs);
        return false;
    }
    SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInRead;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessA(g_exe_path, NULL, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_exe_dir, &si, &pi);

    CloseHandle(hInRead);
    CloseHandle(hOutWrite);

    if (!ok) {
        CloseHandle(hInWrite);
        CloseHandle(hOutRead);
        LeaveCriticalSection(&g_cs);
        return false;
    }

    CloseHandle(pi.hThread);
    g_hProc = pi.hProcess;
    g_hInWrite = hInWrite;
    g_hOutRead = hOutRead;

    char line[512] = {0};
    bool ready = read_line_pipe(g_hOutRead, line, sizeof(line)) && strstr(line, "\"ready\":true") != NULL;
    LeaveCriticalSection(&g_cs);
    return ready;
}

// ---------------------------------------------------------------------------
// Parser JSON minimale ad alte prestazioni (zero allocazioni)
// ---------------------------------------------------------------------------

static const char *extract_str(const char *obj, const char *key, char *out, size_t max_out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(obj, pat);
    if (!p) { out[0] = '\0'; return NULL; }
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < max_out) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p;
}

static double extract_num(const char *obj, const char *key, double def_val) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(obj, pat);
    if (!p) return def_val;
    p += strlen(pat);
    return atof(p);
}

static bool extract_bool(const char *obj, const char *key, bool def_val) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(obj, pat);
    if (!p) return def_val;
    p += strlen(pat);
    return strncmp(p, "true", 4) == 0;
}

static void parse_snapshot_json(const char *json, SensorSnapshot *s) {
    memset(s, 0, sizeof(*s));
    s->bridge_ready = true;
    s->ram_rgb = extract_bool(json, "ram_rgb", false);
    extract_str(json, "ram_model", s->ram_model, sizeof(s->ram_model));

    // Parse fans array
    const char *pf = strstr(json, "\"fans\":[");
    if (pf) {
        const char *end = strstr(pf, "],\"temps\":");
        if (!end) end = pf + strlen(pf);
        const char *cur = pf;
        while ((cur = strchr(cur, '{')) != NULL && cur < end && s->fan_count < MAX_FANS) {
            const char *cend = strchr(cur, '}');
            if (!cend || cend > end) break;
            size_t olen = (size_t)(cend - cur + 1);
            char obj[512];
            if (olen < sizeof(obj)) {
                memcpy(obj, cur, olen);
                obj[olen] = '\0';
                FanInfo *f = &s->fans[s->fan_count++];
                extract_str(obj, "id", f->id, sizeof(f->id));
                extract_str(obj, "name", f->name, sizeof(f->name));
                extract_str(obj, "hardware", f->hardware, sizeof(f->hardware));
                extract_str(obj, "control_id", f->control_id, sizeof(f->control_id));
                f->rpm = (int)extract_num(obj, "rpm", 0);
                f->duty = (int)extract_num(obj, "duty", 0);
                f->controllable = extract_bool(obj, "controllable", false);
            }
            cur = cend + 1;
        }
    }

    // Parse temps array
    const char *pt = strstr(json, "\"temps\":[");
    if (pt) {
        const char *end = strstr(pt, "],\"loads\":");
        if (!end) end = pt + strlen(pt);
        const char *cur = pt;
        while ((cur = strchr(cur, '{')) != NULL && cur < end && s->temp_count < MAX_TEMPS) {
            const char *cend = strchr(cur, '}');
            if (!cend || cend > end) break;
            size_t olen = (size_t)(cend - cur + 1);
            char obj[512];
            if (olen < sizeof(obj)) {
                memcpy(obj, cur, olen);
                obj[olen] = '\0';
                char tname[64] = {0};
                extract_str(obj, "name", tname, sizeof(tname));
                if (strstr(tname, "Distance to TjMax") == NULL) {
                    TempInfo *t = &s->temps[s->temp_count++];
                    extract_str(obj, "id", t->id, sizeof(t->id));
                    snprintf(t->name, sizeof(t->name), "%s", tname);
                    extract_str(obj, "hardware", t->hardware, sizeof(t->hardware));
                    t->celsius = (float)extract_num(obj, "celsius", 0.0);

                    if (s->cpu_temp == 0.0f && (strstr(t->name, "Package") || strstr(t->name, "Core Max")))
                        s->cpu_temp = t->celsius;
                    if (s->gpu_temp == 0.0f && strstr(t->name, "GPU Core"))
                        s->gpu_temp = t->celsius;
                }
            }
            cur = cend + 1;
        }
    }

    // Parse loads array
    const char *pl = strstr(json, "\"loads\":[");
    if (pl) {
        const char *cur = pl;
        while ((cur = strchr(cur, '{')) != NULL) {
            const char *cend = strchr(cur, '}');
            if (!cend) break;
            size_t olen = (size_t)(cend - cur + 1);
            char obj[512];
            if (olen < sizeof(obj)) {
                memcpy(obj, cur, olen);
                obj[olen] = '\0';
                char lname[64] = {0};
                extract_str(obj, "name", lname, sizeof(lname));
                float pct = (float)extract_num(obj, "percent", 0.0);
                if (strcmp(lname, "CPU Total") == 0) s->cpu_load = pct;
                else if (strcmp(lname, "GPU Core") == 0) s->gpu_load = pct;
                else if (strcmp(lname, "Memory") == 0 && strstr(obj, "Total Memory")) s->ram_load = pct;
            }
            cur = cend + 1;
        }
    }
}

bool bridge_refresh(SensorSnapshot *out_snap) {
    if (!g_cs_init) return false;
    EnterCriticalSection(&g_cs);
    char *buf = (char *)malloc(65536);
    if (!buf) {
        LeaveCriticalSection(&g_cs);
        return false;
    }
    bool ok = write_cmd_pipe("GET", buf, 65536);
    if (ok) parse_snapshot_json(buf, out_snap);
    free(buf);
    LeaveCriticalSection(&g_cs);
    return ok;
}

bool bridge_set_ram_rgb(const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    if (!g_cs_init) return false;
    char mode_tok[64];
    snprintf(mode_tok, sizeof(mode_tok), "%s", effect);
    for (int i = 0; mode_tok[i]; i++) if (mode_tok[i] == ' ') mode_tok[i] = '_';

    char cmd[128], resp[256];
    snprintf(cmd, sizeof(cmd), "RAMRGB %s %u %u %u %d", mode_tok, r, g, b, brightness);
    EnterCriticalSection(&g_cs);
    bool ok = write_cmd_pipe(cmd, resp, sizeof(resp)) && strstr(resp, "\"ok\":true") != NULL;
    LeaveCriticalSection(&g_cs);
    return ok;
}

bool bridge_set_fan_duty(const char *control_id, int duty) {
    if (!g_cs_init || !control_id || !control_id[0]) return false;
    char cmd[192], resp[256];
    snprintf(cmd, sizeof(cmd), "SET %s %d", control_id, duty);
    EnterCriticalSection(&g_cs);
    bool ok = write_cmd_pipe(cmd, resp, sizeof(resp)) && strstr(resp, "\"ok\":true") != NULL;
    LeaveCriticalSection(&g_cs);
    return ok;
}

bool bridge_set_fan_auto(const char *control_id) {
    if (!g_cs_init || !control_id || !control_id[0]) return false;
    char cmd[192], resp[256];
    snprintf(cmd, sizeof(cmd), "AUTO %s", control_id);
    EnterCriticalSection(&g_cs);
    bool ok = write_cmd_pipe(cmd, resp, sizeof(resp)) && strstr(resp, "\"ok\":true") != NULL;
    LeaveCriticalSection(&g_cs);
    return ok;
}

void bridge_stop(void) {
    if (!g_cs_init) return;
    EnterCriticalSection(&g_cs);
    if (g_hInWrite) {
        DWORD wr = 0;
        WriteFile(g_hInWrite, "QUIT\n", 5, &wr, NULL);
        CloseHandle(g_hInWrite);
        g_hInWrite = NULL;
    }
    if (g_hProc) {
        if (WaitForSingleObject(g_hProc, 1000) == WAIT_TIMEOUT) TerminateProcess(g_hProc, 0);
        CloseHandle(g_hProc);
        g_hProc = NULL;
    }
    if (g_hOutRead) { CloseHandle(g_hOutRead); g_hOutRead = NULL; }
    LeaveCriticalSection(&g_cs);
}
