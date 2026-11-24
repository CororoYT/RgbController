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
        static List<SMBusInterface> _ramBuses = new List<SMBusInterface>();
        static bool _hasHyperX = false;

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
                    // RAMRGB <mode> <r> <g> <b> <brightness>
                    string[] parts = line.Split(' ');
                    if (parts.Length >= 6)
                    {
                        string mode = parts[1];
                        int r, g, b, bright;
                        if (int.TryParse(parts[2], out r) && int.TryParse(parts[3], out g) &&
                            int.TryParse(parts[4], out b) && int.TryParse(parts[5], out bright))
                        {
                            bool ok = SetHyperXRgb(mode, r, g, b, bright);
                            Console.WriteLine("{\"ok\":" + (ok ? "true" : "false") + "}");
                        }
                        else
                        {
                            Console.WriteLine("{\"ok\":false}");
                        }
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

        static void DetectRamRgb()
        {
            _ramBuses.Clear();
            _hasHyperX = false;
            try
            {
                SMBusManager.DetectSMBuses();
                var logLines = new List<string>();
                foreach (var bus in SMBusManager.RegisteredSMBuses)
                {
                    if (bus == null) continue;
                    string bname = bus.DeviceName ?? "";
                    logLines.Add("BUS: " + bname + " VID=0x" + bus.PCIVendor.ToString("X4") + " DID=0x" + bus.PCIDevice.ToString("X4"));
                    // Escludi bus GPU (NVAPI / ADL): le RAM sono su I801 / PIIX4 / PawnIO
                    if (bname.IndexOf("NVAPI", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        bname.IndexOf("ADL", StringComparison.OrdinalIgnoreCase) >= 0)
                        continue;

                    try
                    {
                        int p27 = bus.i2c_smbus_read_byte_data(0x27, 0x00);
                        logLines.Add("  probe 0x27 = " + p27);
                        if (p27 >= 0)
                        {
                            _ramBuses.Add(bus);
                            _hasHyperX = true;
                        }
                    }
                    catch (Exception ex)
                    {
                        logLines.Add("  probe 0x27 err = " + ex.Message);
                    }
                }
                try
                {
                    string logPath = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "config", "ram_debug.log");
                    System.IO.File.WriteAllLines(logPath, logLines);
                }
                catch { }
            }
            catch { }
        }

        static bool SetHyperXRgb(string mode, int r, int g, int b, int brightness)
        {
            if (!_hasHyperX || _ramBuses.Count == 0)
                return false;

            byte sr = (byte)Math.Max(0, Math.Min(255, r));
            byte sg = (byte)Math.Max(0, Math.Min(255, g));
            byte sb = (byte)Math.Max(0, Math.Min(255, b));
            byte bright = (byte)Math.Max(0, Math.Min(100, brightness));

            // Registri controller HyperX / Kingston Fury DDR4 RGB (indirizzo SMBus 0x27)
            const byte DEV = 0x27;
            const byte REG_EFFECT_BRIGHTNESS = 0xDD;
            const byte REG_APPLY = 0xE1;
            const byte REG_MODE_RANDOM = 0xE3;
            const byte REG_MODE_CUSTOM = 0xE4;
            const byte REG_EFFECT_RED = 0xEC;
            const byte REG_EFFECT_GREEN = 0xED;
            const byte REG_EFFECT_BLUE = 0xEE;
            const byte REG_TIMER_MSB = 0xD1;   // velocità effetti animati
            const byte REG_TIMER_LSB = 0xD2;
            const byte REG_FADE_IN_MSB = 0xD7;
            const byte REG_FADE_IN_LSB = 0xD8;
            const byte REG_FADE_OUT_MSB = 0xD9;
            const byte REG_FADE_OUT_LSB = 0xDA;

            bool anyOk = false;
            foreach (var bus in _ramBuses)
            {
                try
                {
                    // 1) Colore + luminosità dell'effetto
                    if (bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x01) < 0)
                        continue;
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_RED, sr);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_GREEN, sg);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_BLUE, sb);
                    bus.i2c_smbus_write_byte_data(DEV, REG_EFFECT_BRIGHTNESS, bright);
                    SendApply(bus, DEV, REG_APPLY);

                    // 2) Modalità
                    bus.i2c_smbus_write_byte_data(DEV, REG_APPLY, 0x01);
                    if (mode == "Rainbow" || mode == "Wave")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_RANDOM, 0x05);
                        bus.i2c_smbus_write_byte_data(DEV, REG_TIMER_MSB, 0x00);
                        bus.i2c_smbus_write_byte_data(DEV, REG_TIMER_LSB, 0x64);
                    }
                    else if (mode == "Color_Cycle")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_RANDOM, 0x04);
                    }
                    else if (mode == "Breathing")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_IN_MSB, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_IN_LSB, 0xE8);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_OUT_MSB, 0x03);
                        bus.i2c_smbus_write_byte_data(DEV, REG_FADE_OUT_LSB, 0xE8);
                    }
                    else if (mode == "Flicker")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x06);
                    }
                    else if (mode == "Chase")
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x08);
                    }
                    else
                    {
                        bus.i2c_smbus_write_byte_data(DEV, REG_MODE_CUSTOM, 0x09); // Static
                    }
                    SendApply(bus, DEV, REG_APPLY);
                    anyOk = true;
                }
                catch { }
            }
            return anyOk;
        }

        static void SendApply(SMBusInterface bus, byte dev, byte regApply)
        {
            bus.i2c_smbus_write_byte_data(dev, regApply, 0x02);
            bus.i2c_smbus_write_byte_data(dev, regApply, 0x03);
        }

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
                   "\"ram_rgb\":" + (_hasHyperX ? "true" : "false") + "}";
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
