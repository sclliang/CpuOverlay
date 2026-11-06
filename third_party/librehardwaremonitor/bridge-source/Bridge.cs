using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using LibreHardwareMonitor.Hardware;

namespace WatchHardwareInfo.Bridge;

[StructLayout(LayoutKind.Sequential)]
public struct NativeSensor
{
    public double Value;
    public double Minimum;
    public double Maximum;
    public double Average;
    public int SensorType;
    public int HardwareType;
}

internal sealed record SensorSnapshot(
    string Key,
    string Hardware,
    string Name,
    NativeSensor Numeric);

internal struct RunningStats
{
    public double Total;
    public long Count;
}

public static unsafe class Bridge
{
    private static readonly object Sync = new();
    private static readonly Dictionary<string, RunningStats> Stats = new(StringComparer.Ordinal);
    private static Computer? _computer;
    private static List<SensorSnapshot> _snapshot = [];
    private static string _lastError = "";

    [UnmanagedCallersOnly(EntryPoint = "lhm_open", CallConvs = [typeof(CallConvCdecl)])]
    public static int Open()
    {
        lock (Sync)
        {
            try
            {
                CloseCore();
                _computer = new Computer
                {
                    IsCpuEnabled = true,
                    IsGpuEnabled = true
                };
                _computer.Open();
                RefreshCore();
                _lastError = "";
                return 1;
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                CloseCore();
                return 0;
            }
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "lhm_refresh", CallConvs = [typeof(CallConvCdecl)])]
    public static int Refresh()
    {
        lock (Sync)
        {
            if (_computer is null)
            {
                _lastError = "LibreHardwareMonitor is not open.";
                return 0;
            }

            try
            {
                RefreshCore();
                _lastError = "";
                return 1;
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                return 0;
            }
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "lhm_sensor_count", CallConvs = [typeof(CallConvCdecl)])]
    public static int SensorCount()
    {
        lock (Sync)
            return _snapshot.Count;
    }

    [UnmanagedCallersOnly(EntryPoint = "lhm_get_sensor", CallConvs = [typeof(CallConvCdecl)])]
    public static int GetSensor(
        int index,
        NativeSensor* numeric,
        byte* key,
        int keyCapacity,
        byte* hardware,
        int hardwareCapacity,
        byte* name,
        int nameCapacity)
    {
        lock (Sync)
        {
            if ((uint)index >= (uint)_snapshot.Count || numeric is null)
                return 0;

            SensorSnapshot sensor = _snapshot[index];
            *numeric = sensor.Numeric;
            WriteUtf8(sensor.Key, key, keyCapacity);
            WriteUtf8(sensor.Hardware, hardware, hardwareCapacity);
            WriteUtf8(sensor.Name, name, nameCapacity);
            return 1;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "lhm_last_error", CallConvs = [typeof(CallConvCdecl)])]
    public static int LastError(byte* destination, int capacity)
    {
        lock (Sync)
            return WriteUtf8(_lastError, destination, capacity);
    }

    [UnmanagedCallersOnly(EntryPoint = "lhm_close", CallConvs = [typeof(CallConvCdecl)])]
    public static void Close()
    {
        lock (Sync)
            CloseCore();
    }

    private static void RefreshCore()
    {
        Computer computer = _computer ?? throw new InvalidOperationException("LibreHardwareMonitor is not open.");
        foreach (IHardware hardware in computer.Hardware)
            UpdateHardware(hardware);

        List<SensorSnapshot> next = [];
        foreach (IHardware hardware in computer.Hardware)
            CaptureHardware(hardware, next);
        _snapshot = next;
    }

    private static void UpdateHardware(IHardware hardware)
    {
        hardware.Update();
        foreach (IHardware child in hardware.SubHardware)
            UpdateHardware(child);
    }

    private static void CaptureHardware(IHardware hardware, List<SensorSnapshot> destination)
    {
        foreach (ISensor sensor in hardware.Sensors)
        {
            if (sensor.SensorType is not (
                SensorType.Power or SensorType.Clock or SensorType.Temperature or SensorType.Load))
                continue;

            if (sensor.Value is not float current || !float.IsFinite(current))
                continue;

            string key = sensor.Identifier.ToString();
            Stats.TryGetValue(key, out RunningStats stats);
            stats.Total += current;
            stats.Count++;
            Stats[key] = stats;

            destination.Add(new SensorSnapshot(
                key,
                hardware.Name,
                sensor.Name,
                new NativeSensor
                {
                    Value = current,
                    Minimum = sensor.Min ?? current,
                    Maximum = sensor.Max ?? current,
                    Average = stats.Total / stats.Count,
                    SensorType = (int)sensor.SensorType,
                    HardwareType = (int)hardware.HardwareType
                }));
        }

        foreach (IHardware child in hardware.SubHardware)
            CaptureHardware(child, destination);
    }

    private static int WriteUtf8(string value, byte* destination, int capacity)
    {
        int required = Encoding.UTF8.GetByteCount(value) + 1;
        if (destination is null || capacity <= 0)
            return required;

        Span<byte> output = new(destination, capacity);
        output.Clear();
        if (capacity == 1)
            return required;

        Encoder encoder = Encoding.UTF8.GetEncoder();
        encoder.Convert(value.AsSpan(), output[..^1], true, out _, out _, out _);
        return required;
    }

    private static void CloseCore()
    {
        if (_computer is not null)
        {
            try
            {
                _computer.Close();
            }
            catch
            {
                // Shutdown must release as many hardware handles as possible.
            }
        }

        _computer = null;
        _snapshot = [];
        Stats.Clear();
    }
}
