using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using HamDigiSharp.Engine;
using HamDigiSharp.Models;
using NAudio.Wave;

namespace FT8Hunter.CoreTest;

public partial class MainWindow : Window
{
    private const int PM_RX = 0x00200000;
    private const int PM_TX = 0x00400000;
    private const int PM_DIG_U = 0x08000000;

    private dynamic? _omniRig;
    private dynamic? _rig;
    private WaveInEvent? _waveIn;
    private readonly DecoderEngine _decoder = new();
    private RealTimeDecoder? _rt;
    private readonly DispatcherTimer _poll;

    public MainWindow()
    {
        InitializeComponent();
        _poll = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _poll.Tick += (_, _) => RefreshRigStatus();
        _poll.Start();
        Loaded += (_, _) => RefreshAudioDevices();
        Closing += (_, _) => Shutdown();
    }

    private void ConnectRig_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var item = (ComboBoxItem)RigCombo.SelectedItem;
            int rigNo = int.Parse((string)item.Tag, CultureInfo.InvariantCulture);
            var t = Type.GetTypeFromProgID("OmniRig.OmniRigX")
                    ?? throw new InvalidOperationException("OmniRig non risulta installato o registrato.");
            _omniRig = Activator.CreateInstance(t)
                       ?? throw new InvalidOperationException("Impossibile avviare OmniRig.");
            _rig = rigNo == 1 ? _omniRig.Rig1 : _omniRig.Rig2;
            AddLog("RADIO", $"Connesso a OmniRig Rig {rigNo}");
            RefreshRigStatus();
        }
        catch (Exception ex)
        {
            AddLog("RADIO", "ERRORE: " + ex.Message);
            MessageBox.Show(ex.Message, "OmniRig", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void SetupOmniRig_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();
            _omniRig!.DialogVisible = true;
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE: " + ex.Message); }
    }

    private void SetFrequency_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();
            long hz = long.Parse(FrequencyBox.Text.Trim(), CultureInfo.InvariantCulture);
            if (hz is < 100_000 or > 100_000_000) throw new ArgumentOutOfRangeException(nameof(hz));
            _rig!.Freq = checked((int)hz);
            AddLog("RADIO", $"Frequenza impostata: {hz:N0} Hz");
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE: " + ex.Message); }
    }

    private void Tune20m_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();
            _rig!.Freq = 14_074_000;
            _rig.Mode = PM_DIG_U;
            FrequencyBox.Text = "14074000";
            AddLog("RADIO", "14.074.000 Hz + DIG-U richiesti via OmniRig");
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE: " + ex.Message); }
    }

    private void PttOn_Click(object sender, RoutedEventArgs e)
    {
        if (ArmPtt.IsChecked != true)
        {
            AddLog("PTT", "Bloccato: abilita prima TEST PTT.");
            return;
        }
        try { EnsureRig(); _rig!.Tx = PM_TX; AddLog("PTT", "ON"); }
        catch (Exception ex) { AddLog("PTT", "ERRORE: " + ex.Message); }
    }

    private void PttOff_Click(object sender, RoutedEventArgs e)
    {
        try { EnsureRig(); _rig!.Tx = PM_RX; AddLog("PTT", "OFF"); }
        catch (Exception ex) { AddLog("PTT", "ERRORE: " + ex.Message); }
    }

    private void SelfTest_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ConfigureDecoder();
            using var enc = new EncoderEngine();
            const string expected = "CQ W1AW FN42";
            var audio = enc.Encode(expected, DigitalMode.FT8, new EncoderOptions
            {
                FrequencyHz = 1500,
                Amplitude = 0.7
            });
            var results = _decoder.Decode(audio, DigitalMode.FT8, 200, 3000, "120000");
            var match = results.FirstOrDefault(r => string.Equals(r.Message.Trim(), expected, StringComparison.OrdinalIgnoreCase));
            if (match is null)
            {
                Ft8Status.Text = "Self-test FT8: FALLITO";
                AddLog("FT8", results.Count == 0 ? "Self-test: nessun decode" : "Self-test: " + string.Join(" | ", results.Select(r => r.Message)));
            }
            else
            {
                Ft8Status.Text = "Self-test FT8: OK";
                AddLog("FT8", $"Self-test OK: {match.Message} @ {match.FrequencyHz:F0} Hz, SNR {match.Snr:F0} dB");
            }
        }
        catch (Exception ex)
        {
            Ft8Status.Text = "Self-test FT8: errore";
            AddLog("FT8", "ERRORE SELF-TEST: " + ex.Message);
        }
    }

    private void RefreshAudio_Click(object sender, RoutedEventArgs e) => RefreshAudioDevices();

    private void RefreshAudioDevices()
    {
        AudioCombo.Items.Clear();
        for (int i = 0; i < WaveIn.DeviceCount; i++)
        {
            var caps = WaveIn.GetCapabilities(i);
            AudioCombo.Items.Add(new AudioItem(i, $"{i}: {caps.ProductName}"));
        }
        AudioCombo.DisplayMemberPath = nameof(AudioItem.Display);
        if (AudioCombo.Items.Count > 0) AudioCombo.SelectedIndex = 0;
        AddLog("AUDIO", $"Trovati {AudioCombo.Items.Count} ingressi audio.");
    }

    private void StartFt8_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            StopFt8();
            if (AudioCombo.SelectedItem is not AudioItem dev)
                throw new InvalidOperationException("Seleziona l'ingresso audio USB del 7300.");

            ConfigureDecoder();
            _rt = new RealTimeDecoder(_decoder, DigitalMode.FT8, 48_000)
            {
                FreqLow = 200,
                FreqHigh = 3000,
                AlignToUtc = true
            };
            _rt.PeriodDecoded += OnPeriodDecoded;

            _waveIn = new WaveInEvent
            {
                DeviceNumber = dev.Index,
                BufferMilliseconds = 100,
                NumberOfBuffers = 3,
                WaveFormat = new WaveFormat(48_000, 16, 2)
            };
            _waveIn.DataAvailable += OnAudioData;
            _waveIn.RecordingStopped += (_, a) =>
            {
                if (a.Exception is not null) Dispatcher.Invoke(() => AddLog("AUDIO", a.Exception.Message));
            };
            _waveIn.StartRecording();
            Ft8Status.Text = $"FT8 Engine attivo — {dev.Display}";
            AddLog("FT8", "RX FT8 avviata; attesa del prossimo periodo UTC completo.");
        }
        catch (Exception ex)
        {
            AddLog("FT8", "ERRORE: " + ex.Message);
            Ft8Status.Text = "FT8 Engine: errore";
        }
    }

    private void OnAudioData(object? sender, WaveInEventArgs e)
    {
        if (_rt is null) return;
        int frames = e.BytesRecorded / 4;
        var mono = new float[frames];
        int p = 0;
        for (int i = 0; i < frames; i++)
        {
            short l = (short)(e.Buffer[p] | (e.Buffer[p + 1] << 8));
            short r = (short)(e.Buffer[p + 2] | (e.Buffer[p + 3] << 8));
            p += 4;
            mono[i] = ((l + r) * 0.5f) / 32768f;
        }
        try { _rt.AddSamples(mono); }
        catch (Exception ex) { Dispatcher.Invoke(() => AddLog("FT8", "Decode error: " + ex.Message)); }
    }

    private void OnPeriodDecoded(IReadOnlyList<DecodeResult> results, DateTimeOffset windowStart)
    {
        Dispatcher.Invoke(() =>
        {
            if (results.Count == 0)
            {
                AddLog("FT8", $"{windowStart:HH:mm:ss} UTC — nessun decode");
                return;
            }
            foreach (var r in results.OrderBy(x => x.FrequencyHz))
                AddLog("FT8", $"{windowStart:HH:mm:ss}  {r.Snr,4:+#;-#;0} dB  dt={r.Dt,5:F1}s  {r.FrequencyHz,7:F0} Hz  {r.Message}");
        });
    }

    private void StopFt8_Click(object sender, RoutedEventArgs e)
    {
        StopFt8();
        Ft8Status.Text = "FT8 Engine fermo";
        AddLog("FT8", "RX fermata.");
    }

    private void StopFt8()
    {
        if (_waveIn is not null)
        {
            _waveIn.DataAvailable -= OnAudioData;
            try { _waveIn.StopRecording(); } catch { }
            _waveIn.Dispose();
            _waveIn = null;
        }
        if (_rt is not null)
        {
            _rt.PeriodDecoded -= OnPeriodDecoded;
            _rt.Dispose();
            _rt = null;
        }
    }

    private void ConfigureDecoder()
    {
        _decoder.Configure(new DecoderOptions
        {
            MyCall = string.IsNullOrWhiteSpace(MyCallBox.Text) ? null : MyCallBox.Text.Trim().ToUpperInvariant(),
            DecoderDepth = DecoderDepth.Normal,
            ApDecode = !string.IsNullOrWhiteSpace(MyCallBox.Text),
            MaxCandidates = 500,
            MinSyncDb = 2.5f,
            QsoProgress = QsoProgress.None
        });
    }

    private void RefreshRigStatus()
    {
        if (_rig is null) return;
        try
        {
            string type = Convert.ToString(_rig.RigType) ?? "?";
            string status = Convert.ToString(_rig.StatusStr) ?? "?";
            long freq = Convert.ToInt64(_rig.Freq);
            int mode = Convert.ToInt32(_rig.Mode);
            int tx = Convert.ToInt32(_rig.Tx);
            RigStatus.Text = $"{type} | {status} | {freq:N0} Hz | Mode=0x{mode:X8} | {((tx & PM_TX) != 0 ? "TX" : "RX")}";
        }
        catch { }
    }

    private void EnsureRig()
    {
        if (_rig is null) throw new InvalidOperationException("Radio non connessa a OmniRig.");
    }

    private void AddLog(string source, string message)
    {
        string line = $"[{DateTime.Now:HH:mm:ss}] [{source}] {message}";
        LogList.Items.Add(line);
        LogList.ScrollIntoView(line);
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e) => LogList.Items.Clear();

    private void Shutdown()
    {
        _poll.Stop();
        StopFt8();
        try { if (_rig is not null) _rig.Tx = PM_RX; } catch { }
        _rig = null;
        _omniRig = null;
        _decoder.Dispose();
    }

    private sealed record AudioItem(int Index, string Display);
}
