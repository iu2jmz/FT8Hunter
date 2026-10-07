using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using HamDigiSharp.Engine;
using HamDigiSharp.Models;
using NAudio.Wave;

namespace FT8Hunter.CoreTest;

public partial class MainWindow : Window
{
    private const int PM_UNKNOWN = 0x00000001;
    private const int PM_FREQ = 0x00000002;
    private const int PM_FREQA = 0x00000004;
    private const int PM_FREQB = 0x00000008;
    private const int PM_VFOEQUAL = 0x00002000;
    private const int PM_VFOSWAP = 0x00004000;
    private const int PM_VFOA = 0x00000800;
    private const int PM_VFOB = 0x00001000;
    private const int PM_SPLITON = 0x00008000;
    private const int PM_SPLITOFF = 0x00010000;
    private const int PM_RX = 0x00200000;
    private const int PM_TX = 0x00400000;
    private const int PM_SSB_U = 0x02000000;
    private const int PM_DIG_U = 0x08000000;

    // In Rig Split il motore FT8 trasmetterà sempre a 1500 Hz audio.
    // Il VFO B viene spostato per mantenere invariata la frequenza RF scelta sul waterfall.
    private const int SplitTxAudioHz = 1500;

    private dynamic? _omniRig;
    private dynamic? _rig;
    private WaveInEvent? _waveIn;
    private readonly DecoderEngine _decoder = new();
    private RealTimeDecoder? _rt;
    private readonly DispatcherTimer _poll;

    private bool _splitEnabled;
    private long _splitRxFrequency;
    private int _txWaterfallHz = SplitTxAudioHz;
    private bool _ignoreSplitToggle;
    private bool _rxUsbLock;
    private bool _ignoreRxLockToggle;

    public MainWindow()
    {
        InitializeComponent();
        _poll = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _poll.Tick += (_, _) => RefreshRigStatus();
        _poll.Start();
        Loaded += (_, _) => { RefreshAudioDevices(); RefreshTxAudioDevices(); };
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

            string type = Convert.ToString(_rig.RigType) ?? "?";
            AddLog("RADIO", $"Connesso a OmniRig Rig {rigNo}: {type}");
            AddLog("RADIO", $"Parametri scrivibili OmniRig: 0x{GetWritableParams():X8}");

            try
            {
                _splitEnabled = ReadSplitActive();
                _splitRxFrequency = ReadRxFrequency();
                SetSplitButtonState(_splitEnabled);
            }
            catch
            {
                _splitEnabled = false;
                SetSplitButtonState(false);
            }

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
            long hzLong = long.Parse(FrequencyBox.Text.Trim(), CultureInfo.InvariantCulture);
            if (hzLong is < 100_000 or > 100_000_000)
                throw new ArgumentOutOfRangeException(nameof(hzLong), "Frequenza fuori intervallo.");

            SetRadioFrequency(checked((int)hzLong));
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE FREQUENZA: " + ex.Message); }
    }

    private void Tune20m_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();
            const int hz = 14_074_000;
            SetRadioFrequency(hz);
            FrequencyBox.Text = hz.ToString(CultureInfo.InvariantCulture);
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE 20m: " + ex.Message); }
    }

    private void SetRadioFrequency(int hz)
    {
        EnsureRig();

        if (_splitEnabled)
        {
            _splitRxFrequency = hz;
            ApplyRigSplit(hz, _txWaterfallHz, "Cambio frequenza RX");
            return;
        }

        int writable = GetWritableParams();
        AddLog("RADIO", $"Richiesta sintonia {hz:N0} Hz (Writeable=0x{writable:X8})");

        Exception? lastError = null;
        bool commandSent = false;

        try
        {
            _rig!.SetSimplexMode(hz);
            commandSent = true;
            AddLog("RADIO", "Comando inviato con SetSimplexMode().");
        }
        catch (Exception ex)
        {
            lastError = ex;
            AddLog("RADIO", "SetSimplexMode non riuscito: " + ex.Message);
        }

        if (!commandSent || !FrequencyLooksCorrect(hz))
        {
            if ((writable & PM_FREQA) != 0)
            {
                try
                {
                    _rig!.FreqA = hz;
                    commandSent = true;
                    AddLog("RADIO", "Fallback FreqA inviato.");
                }
                catch (Exception ex) { lastError = ex; }
            }

            if (!FrequencyLooksCorrect(hz) && (writable & PM_FREQ) != 0)
            {
                try
                {
                    _rig!.Freq = hz;
                    commandSent = true;
                    AddLog("RADIO", "Fallback Freq inviato.");
                }
                catch (Exception ex) { lastError = ex; }
            }

            if (!FrequencyLooksCorrect(hz) && (writable & PM_FREQB) != 0 &&
                (writable & (PM_FREQ | PM_FREQA)) == 0)
            {
                try
                {
                    _rig!.FreqB = hz;
                    commandSent = true;
                    AddLog("RADIO", "Fallback FreqB inviato.");
                }
                catch (Exception ex) { lastError = ex; }
            }
        }

        if (!commandSent)
            throw new InvalidOperationException("OmniRig non espone un comando di frequenza scrivibile.", lastError);

        Dispatcher.BeginInvoke(async () =>
        {
            await Task.Delay(650);
            long readback = ReadRxFrequency();
            if (Math.Abs(readback - hz) <= 20)
                AddLog("RADIO", $"OK: radio sintonizzata a {readback:N0} Hz");
            else
                AddLog("RADIO", $"ATTENZIONE: richiesti {hz:N0} Hz, letti {readback:N0} Hz");
            RefreshRigStatus();
        });
    }

    private void UsbMode_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();
            SelectRxVfoAIfPossible();
            SetIcom7300UsbData(false);
            AddLog("RADIO", "VFO A / RX richiesto: USB (DATA OFF)");

            if (_splitEnabled)
                ForceTxVfoDataMode();

            VerifyModeLater(PM_SSB_U, "USB");
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE USB: " + ex.Message); }
    }

    private void DataMode_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureRig();

            if (_rxUsbLock)
            {
                AddLog("LOCK", "DATA ignorato sul VFO A: LOCK RX attivo. A resta USB; B resta USB-D.");
                ApplyRxUsbLock();
                return;
            }

            SelectRxVfoAIfPossible();
            SetIcom7300UsbData(true);
            AddLog("RADIO", "Modo RX richiesto: DATA / USB-D");
            VerifyModeLater(PM_SSB_U, "USB-D / DATA ON");
        }
        catch (Exception ex) { AddLog("RADIO", "ERRORE DATA: " + ex.Message); }
    }

    private void RxModeLock_Checked(object sender, RoutedEventArgs e)
    {
        if (_ignoreRxLockToggle) return;

        try
        {
            EnsureRig();
            _rxUsbLock = true;
            ApplyRxUsbLock();
            SetRxLockButtonState(true);
            AddLog("LOCK", "ON — VFO A bloccato in USB; VFO B riservato al TX USB-D.");
        }
        catch (Exception ex)
        {
            _rxUsbLock = false;
            SetRxLockButtonState(false);
            AddLog("LOCK", "ERRORE: " + ex.Message);
        }
    }

    private void RxModeLock_Unchecked(object sender, RoutedEventArgs e)
    {
        if (_ignoreRxLockToggle) return;
        _rxUsbLock = false;
        SetRxLockButtonState(false);
        AddLog("LOCK", "OFF — modo RX non più forzato.");
    }

    private void SetRxLockButtonState(bool enabled)
    {
        _ignoreRxLockToggle = true;
        RxModeLockButton.IsChecked = enabled;
        RxModeLockButton.Content = enabled ? "LOCK ON" : "LOCK RX";
        RxModeLockButton.FontWeight = enabled ? FontWeights.Bold : FontWeights.Normal;
        _ignoreRxLockToggle = false;
    }

    private void SelectRxVfoAIfPossible()
    {
        EnsureRig();
        int writable = GetWritableParams();
        if ((writable & PM_VFOA) != 0)
        {
            try { _rig!.Vfo = PM_VFOA; }
            catch { }
        }
    }

    private void ApplyRxUsbLock()
    {
        if (!_rxUsbLock || _rig is null) return;

        if (_splitEnabled)
        {
            ForceTxVfoDataMode();
            return;
        }

        SelectRxVfoAIfPossible();
        SetIcom7300UsbData(false);
    }

    private void VerifyModeLater(int expectedMode, string requestedName)
    {
        Dispatcher.BeginInvoke(async () =>
        {
            await Task.Delay(650);
            try
            {
                int mode = Convert.ToInt32(_rig!.Mode);
                string actual = ModeName(mode);
                bool ok = (mode & expectedMode) != 0;
                AddLog("RADIO", ok
                    ? $"Modo confermato: {actual}"
                    : $"ATTENZIONE: richiesto {requestedName}, OmniRig legge {actual} (0x{mode:X8}).");

                if (expectedMode == PM_DIG_U && !ok)
                    AddLog("RADIO", "Se il display del 7300 non mostra USB-D, verificare in Setup OmniRig l'uso del profilo IC-7300-DATA.");

                RefreshRigStatus();
            }
            catch (Exception ex) { AddLog("RADIO", "Verifica modo non riuscita: " + ex.Message); }
        });
    }

    private void SplitButton_Checked(object sender, RoutedEventArgs e)
    {
        if (_ignoreSplitToggle) return;
        try
        {
            EnableRigSplit();
        }
        catch (Exception ex)
        {
            AddLog("SPLIT", "ERRORE: " + ex.Message);
            _splitEnabled = false;
            SetSplitButtonState(false);
        }
    }

    private void SplitButton_Unchecked(object sender, RoutedEventArgs e)
    {
        if (_ignoreSplitToggle) return;
        try
        {
            DisableRigSplit();
        }
        catch (Exception ex) { AddLog("SPLIT", "ERRORE: " + ex.Message); }
    }

    private void EnableRigSplit()
    {
        EnsureRig();
        int position = ParseTxPosition();
        long rx = ReadRxFrequency();
        _splitEnabled = true;
        _splitRxFrequency = rx;
        _txWaterfallHz = position;
        SetSplitButtonState(true);
        ApplyRigSplit(rx, position, "SPLIT attivato");
    }

    private void DisableRigSplit()
    {
        if (_rig is null)
        {
            _splitEnabled = false;
            SetSplitButtonState(false);
            return;
        }

        long rx = ReadRxFrequency();
        _rig.SetSimplexMode(checked((int)rx));
        _splitEnabled = false;
        _splitRxFrequency = rx;
        SetSplitButtonState(false);
        TxSplitStatus.Text = "TX VFO B: --";
        AddLog("SPLIT", $"OFF — simplex su {rx:N0} Hz");
        RefreshRigStatus();
    }

    private void ApplyTxPosition_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            int position = ParseTxPosition();
            _txWaterfallHz = position;

            if (!_splitEnabled)
            {
                AddLog("SPLIT", $"Posizione TX memorizzata: {position} Hz. Attiva SPLIT per aggiornare il VFO B.");
                return;
            }

            long rx = ReadRxFrequency();
            _splitRxFrequency = rx;
            ApplyRigSplit(rx, position, "Posizione TX aggiornata");
        }
        catch (Exception ex) { AddLog("SPLIT", "ERRORE POSIZIONE TX: " + ex.Message); }
    }

    private int ParseTxPosition()
    {
        if (!int.TryParse(TxPositionBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int hz))
            throw new InvalidOperationException("Posizione TX non valida.");
        if (hz is < 200 or > 3000)
            throw new InvalidOperationException("La posizione TX deve essere compresa tra 200 e 3000 Hz.");
        return hz;
    }

    private void ApplyRigSplit(long rxFrequency, int waterfallTxHz, string reason)
    {
        EnsureRig();
        if (rxFrequency is < 100_000 or > 100_000_000)
            throw new InvalidOperationException("Frequenza RX non valida per lo split.");

        // RF desiderata = dial RX + posizione scelta sul waterfall.
        // Generando l'FT8 a 1500 Hz audio, il dial TX deve essere:
        // VFO B = RX + posizioneWaterfall - 1500.
        long txDial = rxFrequency + waterfallTxHz - SplitTxAudioHz;
        if (txDial is < 100_000 or > 100_000_000)
            throw new InvalidOperationException("Frequenza TX calcolata fuori intervallo.");

        _rig!.SetSplitMode(checked((int)rxFrequency), checked((int)txDial));
        ForceTxVfoDataMode();

        _splitEnabled = true;
        _splitRxFrequency = rxFrequency;
        _txWaterfallHz = waterfallTxHz;
        SetSplitButtonState(true);

        long rfSignal = txDial + SplitTxAudioHz;
        TxSplitStatus.Text = $"TX VFO B: {txDial:N0} Hz";
        AddLog("SPLIT", $"{reason}: A/RX {rxFrequency:N0} Hz | B/TX {txDial:N0} Hz | audio TX {SplitTxAudioHz} Hz | RF {rfSignal:N0} Hz");

        Dispatcher.BeginInvoke(async () =>
        {
            await Task.Delay(750);
            try
            {
                long rx = ReadRxFrequency();
                long tx = ReadTxFrequency();
                bool split = ReadSplitActive();
                string txText = tx > 0 ? tx.ToString("N0", CultureInfo.CurrentCulture) : "non leggibile";
                AddLog("SPLIT", $"Readback: SPLIT {(split ? "ON" : "?")} | RX {rx:N0} Hz | TX {txText} Hz");
                RefreshRigStatus();
            }
            catch (Exception ex) { AddLog("SPLIT", "Readback non riuscito: " + ex.Message); }
        });
    }

    private void ForceTxVfoDataMode()
    {
        EnsureRig();
        int writable = GetWritableParams();
        bool setOnB = false;

        if ((writable & PM_VFOA) != 0 && (writable & PM_VFOB) != 0)
        {
            try
            {
                _rig!.Vfo = PM_VFOB;
                SetIcom7300UsbData(true);
                _rig.Vfo = PM_VFOA;

                if (_rxUsbLock)
                    SetIcom7300UsbData(false);

                setOnB = true;
                AddLog("SPLIT", _rxUsbLock
                    ? "Modo VFO fissato: A/RX USB | B/TX USB-D (LOCK ON)."
                    : "VFO B impostato USB-D; ritorno a VFO A.");
            }
            catch (Exception ex)
            {
                AddLog("SPLIT", "Impostazione modi A/B non riuscita: " + ex.Message);
            }
        }
        else if ((writable & PM_VFOSWAP) != 0)
        {
            try
            {
                _rig!.Vfo = PM_VFOSWAP;
                SetIcom7300UsbData(true);
                _rig.Vfo = PM_VFOSWAP;

                if (_rxUsbLock)
                    SetIcom7300UsbData(false);

                setOnB = true;
                AddLog("SPLIT", _rxUsbLock
                    ? "Modo VFO fissato via SWAP: RX USB | TX USB-D (LOCK ON)."
                    : "VFO TX impostato USB-D tramite VFO SWAP.");
            }
            catch (Exception ex)
            {
                AddLog("SPLIT", "Impostazione DATA tramite VFO SWAP non riuscita: " + ex.Message);
            }
        }

        if (!setOnB)
        {
            AddLog("SPLIT", "ATTENZIONE: OmniRig non espone A/B separati; LOCK RX non può garantire modi differenti sui due VFO.");
        }
    }

    private void SetSplitButtonState(bool enabled)
    {
        _ignoreSplitToggle = true;
        SplitButton.IsChecked = enabled;
        SplitButton.Content = enabled ? "SPLIT ON" : "SPLIT";
        _ignoreSplitToggle = false;
    }

    private void LogList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (LogList.SelectedItem is not string line) return;

        Match match = Regex.Match(line, @"\b(\d{3,4})\s+Hz\b");
        if (!match.Success || !int.TryParse(match.Groups[1].Value, out int hz))
            return;
        if (hz is < 200 or > 3000)
            return;

        TxPositionBox.Text = hz.ToString(CultureInfo.InvariantCulture);
        _txWaterfallHz = hz;
        AddLog("SPLIT", $"Posizione TX presa dal decode: {hz} Hz");

        if (_splitEnabled)
        {
            try
            {
                long rx = ReadRxFrequency();
                _splitRxFrequency = rx;
                ApplyRigSplit(rx, hz, "Doppio clic decode");
            }
            catch (Exception ex) { AddLog("SPLIT", "ERRORE: " + ex.Message); }
        }
    }

    private int GetWritableParams()
    {
        try { return Convert.ToInt32(_rig!.WriteableParams); }
        catch { return 0; }
    }

    private bool FrequencyLooksCorrect(int requestedHz)
    {
        try
        {
            long current = ReadRxFrequency();
            return Math.Abs(current - requestedHz) <= 20;
        }
        catch { return false; }
    }

    private long ReadRxFrequency()
    {
        EnsureRig();
        try
        {
            long f = Convert.ToInt64(_rig!.GetRxFrequency());
            if (f > 0) return f;
        }
        catch { }

        try
        {
            long f = Convert.ToInt64(_rig!.Freq);
            if (f > 0) return f;
        }
        catch { }

        return Convert.ToInt64(_rig!.FreqA);
    }

    private long ReadTxFrequency()
    {
        EnsureRig();
        try
        {
            long f = Convert.ToInt64(_rig!.GetTxFrequency());
            if (f > 0) return f;
        }
        catch { }

        try
        {
            long f = Convert.ToInt64(_rig!.FreqB);
            if (f > 0) return f;
        }
        catch { }

        return 0;
    }

    private bool ReadSplitActive()
    {
        EnsureRig();
        try
        {
            int split = Convert.ToInt32(_rig!.Split);
            if ((split & PM_SPLITON) != 0) return true;
            if ((split & PM_SPLITOFF) != 0) return false;
        }
        catch { }
        return _splitEnabled;
    }

    private static string ModeName(int mode)
    {
        // DIG-U ha priorità nel caso un driver esponga più flag contemporaneamente.
        if ((mode & PM_DIG_U) != 0) return "DATA/USB-D";
        if ((mode & PM_SSB_U) != 0) return "USB";
        return $"0x{mode:X8}";
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
        try { EnsureRig(); _rig!.Tx = PM_RX; ApplyRxUsbLock(); AddLog("PTT", "OFF"); }
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
                FrequencyHz = SplitTxAudioHz,
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
            _rt = new RealTimeDecoder(DigitalMode.FT8, 48_000)
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
            AutoQsoOnDecodedPeriod(results, windowStart);
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
            long rx = ReadRxFrequency();
            long tx = ReadTxFrequency();
            int mode = Convert.ToInt32(_rig.Mode);
            int ptt = Convert.ToInt32(_rig.Tx);
            bool splitReadback = ReadSplitActive();

            string txText = tx > 0 ? $"{tx:N0} Hz" : "--";
            RigStatus.Text = $"{type} | {status} | RX {rx:N0} Hz | TX {txText} | {ModeName(mode)} | SPLIT {(splitReadback ? "ON" : "OFF")} | LOCK {(_rxUsbLock ? "ON" : "OFF")} | {((ptt & PM_TX) != 0 ? "TX" : "RX")}";

            UsbButton.FontWeight = (mode & PM_SSB_U) != 0 ? FontWeights.Bold : FontWeights.Normal;
            DataButton.FontWeight = (mode & PM_DIG_U) != 0 ? FontWeights.Bold : FontWeights.Normal;

            if (_splitEnabled && tx > 0)
                TxSplitStatus.Text = $"TX VFO B: {tx:N0} Hz";
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
        StopAutoQsoInternal("Chiusura programma", false);
        StopFt8TransmitInternal(false);
        StopFt8();
        try { if (_rig is not null) _rig.Tx = PM_RX; } catch { }
        _rig = null;
        _omniRig = null;
        _decoder.Dispose();
    }

    private sealed record AudioItem(int Index, string Display);
}
