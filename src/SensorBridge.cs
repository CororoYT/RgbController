using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using LibreHardwareMonitor.Hardware;
using RAMSPDToolkit.I2CSMBus;

namespace SensorBridge
{
    class Program
    {
        static Computer _computer;
        static Dictionary<string, IControl> _controls = new Dictionary<string, IControl>();

        // Controller RAM rilevati su bus SMBus (Intel I801 / AMD PIIX4 / PawnIO)
        static List<SMBusInterface> _hyperxBuses = new List<SMBusInterface>();
        static List<KeyValuePair<SMBusInterface, byte>> _eneDevices = new List<KeyValuePair<SMBusInterface, byte>>();
        static List<KeyValuePair<SMBusInterface, byte>> _corsairDevices = new List<KeyValuePair<SMBusInterface, byte>>();
        static string _ramModelName = "";

        static void Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;
            Console.InputEncoding = Encoding.UTF8;

            _computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true,
                IsMotherboardEnabled = true,
                IsMemoryEnabled = true,
                IsControllerEnabled = true,
                IsStorageEnabled = true
            };

            try
            {
                _computer.Open();
            }
            catch (Exception ex)
            {
                Console.WriteLine("{\"error\":" + JsonStr(ex.Message) + "}");
                return;
            }

            DetectRamRgb();

            if (args.Length > 0 && args[0] == "--once")
            {
                Console.WriteLine(ReadSnapshotJson());
                _computer.Close();
                return;
            }

            Console.WriteLine("{\"ready\":true}");
            Console.Out.Flush();

            string line;
            while ((line = Console.ReadLine()) != null)
            {
                line = line.Trim();
                if (line == "QUIT" || line == "EXIT")
                    break;

                if (line == "GET")
                {
                    Console.WriteLine(ReadSnapshotJson());
                    Console.Out.Flush();
                }
                else if (line.StartsWith("SET "))
                {
                    string[] parts = line.Split(new[] { ' ' }, 3);
                    float val;
                    if (parts.Length == 3 && float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out val))
                    {
                        bool ok = SetControl(parts[1], val);
                        Console.WriteLine("{\"ok\":" + (ok ? "true" : "false") + "}");
                    }
                    else
                    {
                        Console.WriteLine("{\"ok\":false,\"error\":\"invalid_args\"}");
                    }
                    Console.Out.Flush();
                }
                else if (line.StartsWith("AUTO "))
                {
                    string id = line.Substring(5).Trim();
                    Console.WriteLine("{\"ok\":" + (SetAuto(id) ? "true" : "false") + "}");
                    Console.Out.Flush();
                }
                else if (line.StartsWith("RAMRGB "))
                {
                    string[] parts = line.Split(' ');
                    int r, g, b, bright;
                    if (parts.Length >= 6 &&
                        int.TryParse(parts[2], out r) && int.TryParse(parts[3], out g) &&
                        int.TryParse(parts[4], out b) && int.TryParse(parts[5], out bright))
                    {
                        bool ok = SetAllRamRgb(parts[1], r, g, b, bright);
                        Console.WriteLine("{\"ok\":" + (ok ? "true" : "false") + "}");
                    }
                    else
                    {
                        Console.WriteLine("{\"ok\":false}");
                    }
                    Console.Out.Flush();
                }
            }

            try { _computer.Close(); } catch { }
        }

        // ------------------------------------------------------------------ //
        // Rilevamento universale RAM RGB DDR4 & DDR5 su SMBus                //
        // ------------------------------------------------------------------ //

        static void DetectRamRgb()
        {
            _hyperxBuses.Clear();
            _eneDevices.Clear();
            _corsairDevices.Clear();
            _ramModelName = "";

            try
            {
                SMBusManager.DetectSMBuses();
                foreach (var bus in SMBusManager.RegisteredSMBuses)
                {
                    if (bus == null) continue;
                    string bname = bus.DeviceName ?? "";
                    if (bname.IndexOf("NVAPI", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        bname.IndexOf("ADL", StringComparison.OrdinalIgnoreCase) >= 0)
                        continue;

                    // 1) Kingston HyperX Fury / Predator DDR4 (0x27)
                    try
                    {
                        if (bus.i2c_smbus_read_byte_data(0x27, 0x00) >= 0)
                        {
                            _hyperxBuses.Add(bus);
                            if (string.IsNullOrEmpty(_ramModelName))
                                _ramModelName = "Kingston HyperX RGB (SMBus)";
                        }
                    }
                    catch { }

                    // 2) ENE / Aura DRAM DDR4 & DDR5 (G.Skill Trident Z/Z5, ASUS, Kingston Fury DDR5, TeamGroup, ADATA: 0x70..0x77)
                    for (byte addr = 0x70; addr <= 0x77; addr++)
                    {
                        try
                        {
                            bus.i2c_smbus_write_byte_data(addr, 0x00, 0x80);
                            bus.i2c_smbus_write_byte_data(addr, 0x01, 0xF8);
                            int sig = bus.i2c_smbus_read_byte_data(addr, 0x81);
                            if (sig >= 0)
                            {
                                _eneDevices.Add(new KeyValuePair<SMBusInterface, byte>(bus, addr));
                                if (string.IsNullOrEmpty(_ramModelName))
                                    _ramModelName = "ENE / Aura DRAM DDR4-DDR5 (SMBus)";
                            }
                        }
                        catch { }
                    }

                    // 3) Corsair Vengeance / Dominator DDR4 & DDR5 (0x58..0x5F)
                    for (byte addr = 0x58; addr <= 0x5F; addr++)
                    {
                        try
                        {
                            int sig = bus.i2c_smbus_read_byte_data(addr, 0x43);
                            if (sig >= 0)
                            {
                                _corsairDevices.Add(new KeyValuePair<SMBusInterface, byte>(bus, addr));
                                if (string.IsNullOrEmpty(_ramModelName))
                                    _ramModelName = "Corsair Vengeance / Dominator RGB (SMBus)";
                            }
                        }
                        catch { }
                    }
                }
            }
            catch { }
        }

        static bool HasAnyRamRgb()
        {
            return _hyperxBuses.Count > 0 || _eneDevices.Count > 0 || _corsairDevices.Count > 0;
        }

        static bool SetAllRamRgb(string mode, int r, int g, int b, int brightness)
        {
            byte sr = (byte)Math.Max(0, Math.Min(255, r));
            byte sg = (byte)Math.Max(0, Math.Min(255, g));
            byte sb = (byte)Math.Max(0, Math.Min(255, b));
            byte bright = (byte)Math.Max(0, Math.Min(100, brightness));

            bool ok = false;
            if (_hyperxBuses.Count > 0) ok |= SetHyperXRgb(mode, sr, sg, sb, bright);
            if (_eneDevices.Count > 0) ok |= SetEneRamRgb(mode, sr, sg, sb, bright);
            if (_corsairDevices.Count > 0) ok |= SetCorsairRamRgb(sr, sg, sb, bright);
            return ok;
        }

        // ---- 1. Kingston HyperX DDR4 (0x27) ----
        static bool SetHyperXRgb(string mode, byte sr, byte sg, byte sb, byte bright)
        {
            const byte DEV = 0x27;
            const byte REG_EFFECT_BRIGHTNESS = 0xDD;
            const byte REG_APPLY = 0xE1;
            const byte REG_MODE_RANDOM = 0xE3;
            const byte REG_MODE_CUSTOM = 0xE4;
            const byte REG_EFFECT_RED = 0xEC;
            const byte REG_EFFECT_GREEN = 0xED;
            const byte REG_EFFECT_BLUE = 0xEE;
            const byte REG_TIMER_MSB = 0xD1;
            const byte REG_TIMER_LSB = 0xD2;
            const byte REG_FADE_IN_MSB = 0xD7;
            const byte REG_FADE_IN_LSB = 0xD8;
            const byte REG_FADE_OUT_MSB = 0xD9;
            const byte REG_FADE_OUT_LSB = 0xDA;

            bool anyOk = false;
            foreach (var bus in _hyperxBuses)
            {
                try
                {
                    if (bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x01) < 0)
                        continue;
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_RED, sr);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_GREEN, sg);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_BLUE, sb);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_BRIGHTNESS, bright);
                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x02);
                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x03);

                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x01);
                    if (mode == "Rainbow" || mode == "Wave")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_RANDOM, 0x05);
                        bus.i2c_smbus_write_byte_data(DEV, REG_TIMER_MSB, 0x00);
                        bus.i2c_smbus_write_byte_data(DEV, REG_TIMER_LSB, 0x64);
                    }
                    else if (mode == "Color_Cycle")
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_RANDOM, 0x04);
                    else if (mode == "Breathing")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_IN_MSB, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_IN_LSB, 0xE8);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_OUT_MSB, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_OUT_LSB, 0xE8);
                    }
                    else if (mode == "Flicker")
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x06);
                    else if (mode == "Chase")
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x08);
                    else
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x09);

                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x02);
                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x03);
                    anyOk = true;
                }
                catch { }
            }
            return anyOk;
        }

        // ---- 2. ENE / Aura / G.Skill / Kingston DDR5 (0x70..0x77) ----
        static void EneWriteReg(SMBusInterface bus, byte addr, ushort reg, byte val)
        {
            bus.i2c_smbus_write_byte_data(addr, 0x00, (byte)(reg >> 8));
            bus.i2c_smbus_write_byte_data(addr, 0x01, (byte)(reg & 0xFF));
            bus.i2c_smbus_write_byte_data(addr, 0x03, val);
        }

        static bool SetEneRamRgb(string mode, byte sr, byte sg, byte sb, byte bright)
        {
            float f = bright / 100f;
            byte cr = (byte)Math.Round(sr * f);
            byte cg = (byte)Math.Round(sg * f);
            byte cb = (byte)Math.Round(sb * f);

            byte eneMode = 0x01; // Static
            if (mode == "Breathing") eneMode = 0x02;
            else if (mode == "Flicker" || mode == "Chase") eneMode = 0x03;
            else if (mode == "Color_Cycle") eneMode = 0x04;
            else if (mode == "Rainbow" || mode == "Wave") eneMode = 0x05;

            bool anyOk = false;
            foreach (var kv in _eneDevices)
            {
                var bus = kv.Key;
                byte addr = kv.Value;
                try
                {
                    EneWriteReg(bus, addr, 0x8020, eneMode);
                    EneWriteReg(bus, addr, 0x8021, 0x00); // speed
                    EneWriteReg(bus, addr, 0x8022, 0x00); // direction
                    // ENE usa ordine R, B, G a partire da 0x8000 (fino a 10 LED)
                    for (int i = 0; i < 10; i++)
                    {
                        ushort baseReg = (ushort)(0x8000 + i * 3);
                        EneWriteReg(bus, addr, baseReg, cr);
                        EneWriteReg(bus, addr, (ushort)(baseReg + 1), cb);
                        EneWriteReg(bus, addr, (ushort)(baseReg + 2), cg);
                    }
                    EneWriteReg(bus, addr, 0x802F, 0x01); // Apply
                    anyOk = true;
                }
                catch { }
            }
            return anyOk;
        }

        // ---- 3. Corsair Vengeance / Dominator DDR4 & DDR5 (0x58..0x5F) ----
        static byte Crc8(byte[] data, int len)
        {
            byte crc = 0;
            for (int i = 0; i < len; i++)
            {
                crc ^= data[i];
                for (int b = 0; b < 8; b++)
                    crc = (byte)((crc & 0x80) != 0 ? (crc << 1) ^ 0x07 : (crc << 1));
            }
            return crc;
        }

        static bool SetCorsairRamRgb(byte sr, byte sg, byte sb, byte bright)
        {
            float f = bright / 100f;
            byte cr = (byte)Math.Round(sr * f);
            byte cg = (byte)Math.Round(sg * f);
            byte cb = (byte)Math.Round(sb * f);

            bool anyOk = false;
            foreach (var kv in _corsairDevices)
            {
                var bus = kv.Key;
                byte addr = kv.Value;
                try
                {
                    // 10 LED x 3 byte (R,G,B) + CRC8 su comando 0x31
                    byte[] pkt = new byte[31];
                    for (int i = 0; i < 10; i++)
                    {
                        pkt[i * 3 + 0] = cr;
                        pkt[i * 3 + 1] = cg;
                        pkt[i * 3 + 2] = cb;
                    }
                    pkt[30] = Crc8(pkt, 30);
                    bus.i2c_smbus_write_block_data(addr, 0x31, 31, pkt);
                    anyOk = true;
                }
                catch { }
            }
            return anyOk;
        }

        // ------------------------------------------------------------------ //
        // Controllo Ventole                                                   //
        // ------------------------------------------------------------------ //

        static bool SetControl(string id, float duty)
        {
            if (duty < 0) duty = 0;
            if (duty > 100) duty = 100;
            IControl ctrl;
            if (_controls.TryGetValue(id, out ctrl))
            {
                try { ctrl.SetSoftware(duty); return true; } catch { return false; }
            }
            return false;
        }

        static bool SetAuto(string id)
        {
            IControl ctrl;
            if (_controls.TryGetValue(id, out ctrl))
            {
                try { ctrl.SetDefault(); return true; } catch { return false; }
            }
            return false;
        }

        static string ReadSnapshotJson()
        {
            var fans = new List<string>();
            var temps = new List<string>();
            var loads = new List<string>();
            var controlsMap = new Dictionary<string, IControl>();
            var controlsList = new List<ISensor>();

            _controls.Clear();

            foreach (var hw in _computer.Hardware)
                UpdateHardwareRecursive(hw, controlsList);

            foreach (var cs in controlsList)
            {
                if (cs.Control != null)
                {
                    string cid = cs.Identifier.ToString();
                    _controls[cid] = cs.Control;
                    controlsMap[cid] = cs.Control;
                }
            }

            foreach (var hw in _computer.Hardware)
                CollectSensorsRecursive(hw, fans, temps, loads, controlsMap);

            return "{\"fans\":[" + string.Join(",", fans) + "]," +
                   "\"temps\":[" + string.Join(",", temps) + "]," +
                   "\"loads\":[" + string.Join(",", loads) + "]," +
                   "\"ram_rgb\":" + (HasAnyRamRgb() ? "true" : "false") + "," +
                   "\"ram_model\":" + JsonStr(_ramModelName) + "}";
        }

        static void UpdateHardwareRecursive(IHardware hw, List<ISensor> controlsList)
        {
            try { hw.Update(); } catch { }
            foreach (var s in hw.Sensors)
                if (s.SensorType == SensorType.Control) controlsList.Add(s);
            foreach (var sub in hw.SubHardware)
                UpdateHardwareRecursive(sub, controlsList);
        }

        static void CollectSensorsRecursive(
            IHardware hw, List<string> fans, List<string> temps,
            List<string> loads, Dictionary<string, IControl> controls)
        {
            foreach (var s in hw.Sensors)
            {
                if (!s.Value.HasValue) continue;
                float val = s.Value.Value;
                string id = s.Identifier.ToString();

                if (s.SensorType == SensorType.Fan)
                {
                    string ctrlId = id.Replace("/fan/", "/control/");
                    bool hasCtrl = controls.ContainsKey(ctrlId);
                    float duty = 0;
                    if (hasCtrl) { try { duty = controls[ctrlId].SoftwareValue; } catch { } }
                    fans.Add(
                        "{\"id\":" + JsonStr(id) +
                        ",\"name\":" + JsonStr(s.Name) +
                        ",\"hardware\":" + JsonStr(hw.Name) +
                        ",\"rpm\":" + ((int)Math.Round(val)).ToString(CultureInfo.InvariantCulture) +
                        ",\"control_id\":" + JsonStr(hasCtrl ? ctrlId : "") +
                        ",\"duty\":" + ((int)Math.Round(duty)).ToString(CultureInfo.InvariantCulture) +
                        ",\"controllable\":" + (hasCtrl ? "true" : "false") + "}"
                    );
                }
                else if (s.SensorType == SensorType.Temperature)
                {
                    temps.Add(
                        "{\"id\":" + JsonStr(id) +
                        ",\"name\":" + JsonStr(s.Name) +
                        ",\"hardware\":" + JsonStr(hw.Name) +
                        ",\"celsius\":" + val.ToString("F1", CultureInfo.InvariantCulture) + "}"
                    );
                }
                else if (s.SensorType == SensorType.Load)
                {
                    loads.Add(
                        "{\"id\":" + JsonStr(id) +
                        ",\"name\":" + JsonStr(s.Name) +
                        ",\"hardware\":" + JsonStr(hw.Name) +
                        ",\"percent\":" + val.ToString("F1", CultureInfo.InvariantCulture) + "}"
                    );
                }
            }
            foreach (var sub in hw.SubHardware)
                CollectSensorsRecursive(sub, fans, temps, loads, controls);
        }

        static string JsonStr(string s)
        {
            if (s == null) return "null";
            var sb = new StringBuilder("\"");
            foreach (char c in s)
            {
                if (c == '\\') sb.Append("\\\\");
                else if (c == '"') sb.Append("\\\"");
                else if (c == '\n') sb.Append("\\n");
                else if (c == '\r') sb.Append("\\r");
                else if (c == '\t') sb.Append("\\t");
                else sb.Append(c);
            }
            sb.Append("\"");
            return sb.ToString();
        }
    }
}
