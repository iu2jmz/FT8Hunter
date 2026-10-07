using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Windows;
using HamDigiSharp.Models;

namespace FT8Hunter.CoreTest;

public partial class MainWindow
{
    private enum AutoQsoState
    {
        Off,
        WaitingCaller,
        WaitingRReport,
        Complete
    }

    // FAST-QSO: nessuna attesa artificiale dopo un decode vuoto.
    // Se il decoder non trova una risposta, il retry viene armato subito per
    // preservare lo slot FT8 successivo sul PC lento.
    private const int AutoDecodeGraceMs = 0;

    private CancellationTokenSource? _autoQsoCts;
    private CancellationTokenSource? _autoRetryCts;
    private CancellationTokenSource? _autoActiveTxCts;
    private AutoQsoState _autoQsoState = AutoQsoState.Off;
    private string? _autoDxCall;
    private string? _autoLastTxMessage;
    private DateTimeOffset? _autoLastTxSlot;
    private DateTimeOffset? _autoLastProcessedWindow;
    private int? _autoTxParity;
    private int _autoAttempts;
    private bool _autoTxInProgress;
    private ProcessPriorityClass? _previousProcessPriority;

    private async void StartAutoQso_Click(object sender, RoutedEventArgs e)
    {
        StopAutoQsoInternal("Riavvio Auto QSO", false);

        if (!ValidateFt8TxPrerequisites(showMessage: true))
            return;

        if (_rt is null)
        {
            AddLog("AUTO", "Bloccato: avvia prima RX FT8.");
            MessageBox.Show("Avvia prima RX FT8, così il programma può decodificare le risposte.", "Auto QSO", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        string myCall = MyCallBox.Text.Trim().ToUpperInvariant();
        string myGrid = MyGridBox.Text.Trim().ToUpperInvariant();

        if (!IsPlausibleCallsign(myCall))
        {
            AddLog("AUTO", "MY CALL non valido.");
            return;
        }

        if (!Regex.IsMatch(myGrid, "^[A-R]{2}[0-9]{2}$", RegexOptions.CultureInvariant))
        {
            AddLog("AUTO", "MY GRID non valido: inserisci il locator a 4 caratteri, per esempio JN45.");
            MessageBox.Show("Inserisci MY GRID nel formato Maidenhead a 4 caratteri, per esempio JN45.", "Auto QSO", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        _autoQsoCts = new CancellationTokenSource();
        _autoQsoState = AutoQsoState.WaitingCaller;
        _autoDxCall = null;
        _autoLastTxMessage = null;
        _autoLastTxSlot = null;
        _autoLastProcessedWindow = null;
        _autoTxParity = null;
        _autoAttempts = 0;

        EnterAutoQsoPerformanceMode();
        ConfigureAutoQsoDecoder(null, 0, QsoProgress.None);

        string cq = $"CQ {myCall} {myGrid}";
        AutoQsoStatus.Text = "Auto QSO: invio CQ...";
        AddLog("AUTO", $"START — {cq}");

        await AutoSendAsync(cq, AutoQsoState.WaitingCaller, resetAttempts: true, targetSlotUtc: null);
    }

    private void StopAutoQso_Click(object sender, RoutedEventArgs e)
    {
        StopAutoQsoInternal("STOP AUTO", true);
    }

    private void StopAutoQsoInternal(string reason, bool log)
    {
        CancelRetryTimer();
        try { _autoActiveTxCts?.Cancel(); } catch { }
        try { _autoActiveTxCts?.Dispose(); } catch { }
        _autoActiveTxCts = null;

        try { _autoQsoCts?.Cancel(); } catch { }
        try { _autoQsoCts?.Dispose(); } catch { }
        _autoQsoCts = null;

        _autoQsoState = AutoQsoState.Off;
        _autoDxCall = null;
        _autoLastTxMessage = null;
        _autoLastTxSlot = null;
        _autoLastProcessedWindow = null;
        _autoTxParity = null;
        _autoAttempts = 0;
        _autoTxInProgress = false;

        RestoreRealtimeBrowseDecoder();
        ExitAutoQsoPerformanceMode();

        if (AutoQsoStatus is not null)
            AutoQsoStatus.Text = "Auto QSO fermo";

        if (log)
            AddLog("AUTO", reason);
    }

    private void AutoQsoOnDecodedPeriod(IReadOnlyList<DecodeResult> results, DateTimeOffset windowStart)
    {
        if (_autoQsoState is AutoQsoState.Off or AutoQsoState.Complete)
            return;

        if (_autoQsoCts is null || _autoTxParity is null)
            return;

        // Una finestra viene marcata come processata solo quando abbiamo realmente
        // consumato una risposta valida. I callback vuoti/parziali restano quindi
        // aperti a un eventuale late decode dello stesso periodo.
        if (_autoLastProcessedWindow == windowStart)
            return;

        if (GetFt8SlotParity(windowStart) == _autoTxParity.Value)
            return;

        if (_autoLastTxSlot.HasValue && windowStart < _autoLastTxSlot.Value.AddSeconds(10))
            return;

        string myCall = NormalizeCallToken(MyCallBox.Text);

        if (_autoQsoState == AutoQsoState.WaitingCaller)
        {
            var caller = results
                .Select(r => new { Result = r, Parsed = ParseAddressedMessage(r.Message, myCall) })
                .Where(x => x.Parsed is not null && !string.Equals(x.Parsed.Value.DxCall, myCall, StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(x => x.Result.Snr)
                .FirstOrDefault();

            if (caller is null)
            {
                ScheduleRetryCurrentAutoMessage("nessuna risposta al CQ / attesa late decode");
                return;
            }

            _autoLastProcessedWindow = windowStart;
            CancelRetryTimer();

            string dx = caller.Parsed!.Value.DxCall;
            _autoDxCall = dx;

            int callerHz = Math.Clamp((int)Math.Round(caller.Result.FrequencyHz), 200, 3000);

            if (_txFreqLock)
            {
                AddLog("LOCK", $"Caller {dx} a {callerHz} Hz: TX resta bloccata a {_lockedTxWaterfallHz} Hz.");
            }
            else
            {
                TxPositionBox.Text = callerHz.ToString(System.Globalization.CultureInfo.InvariantCulture);
                _txWaterfallHz = callerHz;
            }

            string report = FormatFt8Report(caller.Result.Snr);
            string reply = $"{dx} {myCall} {report}";

            DateTimeOffset decodeAt = DateTimeOffset.UtcNow;
            DateTimeOffset replyTarget = windowStart.AddSeconds(15);
            double targetMarginMs = (replyTarget - decodeAt).TotalMilliseconds;

            AddLog("AUTO", $"LATE/VALID CALLER: {dx} | SNR {caller.Result.Snr:+#;-#;0} dB | RX {callerHz} Hz | TX {(_txFreqLock ? _lockedTxWaterfallHz : callerHz)} Hz → '{reply}'");
            AddLog("SCHED", $"DECODE {decodeAt:HH:mm:ss.fff} UTC | RX SLOT {windowStart:HH:mm:ss.fff} | TARGET RISPOSTA {replyTarget:HH:mm:ss.fff} | margine {targetMarginMs:F0} ms");
            AutoQsoStatus.Text = $"QSO con {dx}: risposta valida, target {replyTarget:HH:mm:ss} UTC";

            _ = ReplacePendingAutoTxAsync(reply, AutoQsoState.WaitingRReport, resetAttempts: true,
                replyTarget, $"decode valido di {dx}");

            // Solo dopo avere armato il TX restringiamo il decoder sul corrispondente:
            // meno candidati e finestra audio stretta = risposta successiva più rapida.
            ConfigureAutoQsoDecoder(dx, callerHz, QsoProgress.Called);
            return;
        }

        if (_autoQsoState == AutoQsoState.WaitingRReport && !string.IsNullOrWhiteSpace(_autoDxCall))
        {
            string dx = _autoDxCall;
            var addressed = results
                .Select(r => new { Result = r, Parsed = ParseAddressedMessage(r.Message, myCall) })
                .Where(x => x.Parsed is not null && string.Equals(x.Parsed.Value.DxCall, dx, StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(x => x.Result.Snr)
                .FirstOrDefault();

            if (addressed is null)
            {
                ScheduleRetryCurrentAutoMessage($"nessuna risposta da {dx} / attesa late decode");
                return;
            }

            string payload = addressed.Parsed!.Value.Payload;

            if (Regex.IsMatch(payload, "^R[+-][0-9]{2}$", RegexOptions.CultureInvariant))
            {
                _autoLastProcessedWindow = windowStart;
                CancelRetryTimer();

                string rr73 = $"{dx} {myCall} RR73";
                DateTimeOffset decodeAt = DateTimeOffset.UtcNow;
                DateTimeOffset replyTarget = windowStart.AddSeconds(15);
                double targetMarginMs = (replyTarget - decodeAt).TotalMilliseconds;

                AddLog("AUTO", $"LATE/VALID R-report da {dx}: {payload} → '{rr73}'");
                AddLog("SCHED", $"DECODE {decodeAt:HH:mm:ss.fff} UTC | RX SLOT {windowStart:HH:mm:ss.fff} | TARGET RR73 {replyTarget:HH:mm:ss.fff} | margine {targetMarginMs:F0} ms");
                AutoQsoStatus.Text = $"QSO con {dx}: R-report {payload}, target {replyTarget:HH:mm:ss} UTC";
                _ = ReplacePendingFinalTxAsync(rr73, dx, replyTarget, $"R-report {payload} ricevuto");
                ConfigureAutoQsoDecoder(dx, addressed.Result.FrequencyHz, QsoProgress.ReportReceived);
                return;
            }

            if (payload is "RR73" or "73")
            {
                _autoLastProcessedWindow = windowStart;
                CancelRetryTimer();
                CancelActiveAutoTx($"ricevuto {payload} da {dx}");
                FinishAutoQso(dx, $"ricevuto {payload}");
                return;
            }

            AddLog("AUTO", $"Messaggio da {dx} ricevuto ma non ancora conclusivo: '{addressed.Result.Message.Trim()}'");
            ScheduleRetryCurrentAutoMessage($"atteso R-report da {dx} / possibile late decode");
        }
    }

    private async Task ReplacePendingAutoTxAsync(
        string message,
        AutoQsoState stateAfterTx,
        bool resetAttempts,
        DateTimeOffset targetSlotUtc,
        string reason)
    {
        CancelRetryTimer();
        CancelActiveAutoTx(reason);

        // Se un retry aveva già iniziato la commutazione, attendiamo che il finally
        // abbia spento PTT e liberato il gate prima di armare il messaggio corretto.
        for (int i = 0; i < 80 && _autoTxInProgress; i++)
            await Task.Delay(50);

        if (_autoQsoCts is null || _autoQsoState == AutoQsoState.Off)
            return;

        await AutoSendAsync(message, stateAfterTx, resetAttempts, targetSlotUtc);
    }

    private async Task ReplacePendingFinalTxAsync(string message, string dx, DateTimeOffset targetSlotUtc, string reason)
    {
        CancelRetryTimer();
        CancelActiveAutoTx(reason);

        for (int i = 0; i < 80 && _autoTxInProgress; i++)
            await Task.Delay(50);

        if (_autoQsoCts is null || _autoQsoState == AutoQsoState.Off)
            return;

        await SendFinalRr73Async(message, dx, targetSlotUtc);
    }

    private void CancelActiveAutoTx(string reason)
    {
        if (_autoActiveTxCts is null || !_autoTxInProgress)
            return;

        try
        {
            _autoActiveTxCts.Cancel();
            AddLog("AUTO", $"TX/retry già armato annullato: {reason}.");
        }
        catch { }
    }

    private async Task AutoSendAsync(string message, AutoQsoState stateAfterTx, bool resetAttempts, DateTimeOffset? targetSlotUtc)
    {
        if (_autoQsoCts is null)
            return;

        if (resetAttempts)
            _autoAttempts = 0;

        if (_autoTxInProgress)
            return;

        _autoAttempts++;
        _autoLastTxMessage = message;
        _autoQsoState = stateAfterTx;
        _autoTxInProgress = true;

        using var txCts = CancellationTokenSource.CreateLinkedTokenSource(_autoQsoCts.Token);
        _autoActiveTxCts = txCts;

        try
        {
            DateTimeOffset slot = await SendFt8MessageAsync(message, txCts.Token, "AUTO", _autoTxParity, targetSlotUtc);
            _autoLastTxSlot = slot;
            _autoTxParity ??= GetFt8SlotParity(slot);

            string phase = stateAfterTx == AutoQsoState.WaitingCaller
                ? "attesa chiamanti"
                : $"attesa R-report da {_autoDxCall}";

            AutoQsoStatus.Text = $"Auto QSO: {phase} — tentativo {_autoAttempts}/3";
            AddLog("AUTO", $"TX completato, {phase}; parity TX {_autoTxParity}");
        }
        catch (OperationCanceledException)
        {
            if (_autoQsoState != AutoQsoState.Off)
                AddLog("AUTO", "TX armato annullato per un decode arrivato in ritardo o per STOP.");
        }
        catch (Exception ex)
        {
            AddLog("AUTO", "ERRORE: " + ex.Message);
            StopAutoQsoInternal("Errore Auto QSO", false);
        }
        finally
        {
            if (ReferenceEquals(_autoActiveTxCts, txCts))
                _autoActiveTxCts = null;
            _autoTxInProgress = false;
        }
    }

    private async Task SendFinalRr73Async(string message, string dx, DateTimeOffset? targetSlotUtc)
    {
        if (_autoQsoCts is null || _autoTxInProgress)
            return;

        _autoAttempts = 1;
        _autoLastTxMessage = message;
        _autoTxInProgress = true;

        using var txCts = CancellationTokenSource.CreateLinkedTokenSource(_autoQsoCts.Token);
        _autoActiveTxCts = txCts;

        try
        {
            DateTimeOffset slot = await SendFt8MessageAsync(message, txCts.Token, "AUTO", _autoTxParity, targetSlotUtc);
            _autoLastTxSlot = slot;
            FinishAutoQso(dx, "RR73 trasmesso");
        }
        catch (OperationCanceledException)
        {
            if (_autoQsoState != AutoQsoState.Off)
                AddLog("AUTO", "RR73 armato annullato.");
        }
        catch (Exception ex)
        {
            AddLog("AUTO", "ERRORE RR73: " + ex.Message);
            StopAutoQsoInternal("Errore RR73", false);
        }
        finally
        {
            if (ReferenceEquals(_autoActiveTxCts, txCts))
                _autoActiveTxCts = null;
            _autoTxInProgress = false;
        }
    }

    private void ScheduleRetryCurrentAutoMessage(string reason)
    {
        if (_autoQsoCts is null || string.IsNullOrWhiteSpace(_autoLastTxMessage))
            return;

        if (_autoRetryCts is not null)
            return;

        if (_autoAttempts >= 3)
        {
            string phase = _autoQsoState == AutoQsoState.WaitingCaller ? "CQ" : $"QSO con {_autoDxCall}";
            AddLog("AUTO", $"STOP — {phase}: 3 tentativi senza risposta valida ({reason}).");
            AutoQsoStatus.Text = $"Auto QSO fermato: 3 tentativi ({reason})";
            StopAutoQsoInternal("Limite tentativi", false);
            return;
        }

        var retryCts = CancellationTokenSource.CreateLinkedTokenSource(_autoQsoCts.Token);
        _autoRetryCts = retryCts;

        AutoQsoStatus.Text = "Auto QSO FAST: retry immediato";
        AddLog("AUTO", $"FAST retry: nessuna attesa aggiuntiva ({reason}).");

        _ = RunRetryAfterGraceAsync(retryCts, reason);
    }

    private async Task RunRetryAfterGraceAsync(CancellationTokenSource retryCts, string reason)
    {
        try
        {
            await Task.Delay(AutoDecodeGraceMs, retryCts.Token);

            if (_autoQsoCts is null || retryCts.IsCancellationRequested ||
                string.IsNullOrWhiteSpace(_autoLastTxMessage))
                return;

            string message = _autoLastTxMessage;
            AutoQsoState state = _autoQsoState;

            AddLog("AUTO", $"Nessun late decode valido: retry {_autoAttempts + 1}/3 ({reason}).");

            if (ReferenceEquals(_autoRetryCts, retryCts))
                _autoRetryCts = null;

            await AutoSendAsync(message, state, resetAttempts: false, targetSlotUtc: null);
        }
        catch (OperationCanceledException)
        {
            AddLog("AUTO", "Retry sospeso: è arrivato un decode utile prima della ritrasmissione.");
        }
        finally
        {
            if (ReferenceEquals(_autoRetryCts, retryCts))
                _autoRetryCts = null;
            retryCts.Dispose();
        }
    }

    private void CancelRetryTimer()
    {
        var cts = _autoRetryCts;
        _autoRetryCts = null;
        if (cts is null) return;

        try { cts.Cancel(); } catch { }
        try { cts.Dispose(); } catch { }
    }

    private void FinishAutoQso(string dx, string reason)
    {
        CancelRetryTimer();
        _autoQsoState = AutoQsoState.Complete;
        RestoreRealtimeBrowseDecoder();
        ExitAutoQsoPerformanceMode();
        AutoQsoStatus.Text = $"QSO COMPLETATO con {dx} — {reason}";
        AddLog("AUTO", $"QSO COMPLETATO con {dx} — {reason}");

        try { _autoQsoCts?.Dispose(); } catch { }
        _autoQsoCts = null;
    }

    private void ConfigureAutoQsoDecoder(string? hisCall, double qsoFrequencyHz, QsoProgress progress)
    {
        if (_rt is null) return;

        string myCall = MyCallBox.Text.Trim().ToUpperInvariant();
        string myGrid = MyGridBox.Text.Trim().ToUpperInvariant();
        bool focused = !string.IsNullOrWhiteSpace(hisCall) && qsoFrequencyHz is >= 200 and <= 3000;

        // 0.6.5 riduceva troppo sensibilità e candidati. Su PC lento era più veloce
        // ma perdeva troppe risposte. Qui torniamo a NORMAL per il CQ iniziale.
        // Solo dopo avere scelto il corrispondente restringiamo la banda, mantenendo
        // comunque DecoderDepth.Normal per non perdere R-report / RR73.
        _rt.FreqLow = focused ? Math.Max(200, qsoFrequencyHz - 220) : 200;
        _rt.FreqHigh = focused ? Math.Min(3000, qsoFrequencyHz + 220) : 3000;

        _rt.RealTimeOptions = new DecoderOptions
        {
            MyCall = myCall,
            MyBaseCall = myCall,
            MyGrid = myGrid,
            HisCall = hisCall ?? string.Empty,
            DecoderDepth = DecoderDepth.Normal,
            MaxCandidates = focused ? 36 : 50,
            MinSyncDb = 2.1f,
            ApDecode = !string.IsNullOrWhiteSpace(myCall),
            QsoProgress = progress,
            QsoFrequencyHz = focused ? qsoFrequencyHz : 0,
            TxFrequencyHz = _txFreqLock ? _lockedTxWaterfallHz : _txWaterfallHz,
            FreqTolerance = focused ? 220 : 200,
            AveragingEnabled = false
        };

        AddLog("PERF", focused
            ? $"QSO focalizzato su {hisCall} @ {qsoFrequencyHz:F0} Hz | NORMAL | 36 candidati | ±220 Hz."
            : "CQ Auto QSO: decoder NORMAL completo | 50 candidati.");
    }

    private void EnterAutoQsoPerformanceMode()
    {
        try
        {
            using Process process = Process.GetCurrentProcess();
            _previousProcessPriority ??= process.PriorityClass;
            process.PriorityClass = ProcessPriorityClass.High;
            AddLog("PERF", "Priorità processo HIGH durante Auto QSO.");
        }
        catch (Exception ex)
        {
            AddLog("PERF", "Priorità HIGH non disponibile: " + ex.Message);
        }
    }

    private void ExitAutoQsoPerformanceMode()
    {
        if (!_previousProcessPriority.HasValue)
            return;

        try
        {
            using Process process = Process.GetCurrentProcess();
            process.PriorityClass = _previousProcessPriority.Value;
        }
        catch { }
        finally
        {
            _previousProcessPriority = null;
        }
    }

    private void RestoreRealtimeBrowseDecoder()
    {
        if (_rt is null) return;

        string myCall = MyCallBox.Text.Trim().ToUpperInvariant();
        string myGrid = MyGridBox.Text.Trim().ToUpperInvariant();

        _rt.FreqLow = 200;
        _rt.FreqHigh = 3000;
        _rt.RealTimeOptions = new DecoderOptions
        {
            MyCall = myCall,
            MyBaseCall = myCall,
            MyGrid = myGrid,
            DecoderDepth = DecoderDepth.Normal,
            MaxCandidates = 75,
            MinSyncDb = 2.1f,
            ApDecode = !string.IsNullOrWhiteSpace(myCall),
            QsoProgress = QsoProgress.None,
            AveragingEnabled = false
        };
    }

    private static (string DxCall, string Payload)? ParseAddressedMessage(string message, string myCall)
    {
        string[] t = message
            .Trim()
            .ToUpperInvariant()
            .Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        if (t.Length < 2)
            return null;

        string to = NormalizeCallToken(t[0]);
        if (!string.Equals(to, myCall, StringComparison.OrdinalIgnoreCase))
            return null;

        string dx = NormalizeCallToken(t[1]);
        if (!IsPlausibleCallsign(dx))
            return null;

        string payload = t.Length >= 3 ? t[2] : string.Empty;
        return (dx, payload);
    }

    private static string NormalizeCallToken(string call) =>
        call.Trim().Trim('<', '>').ToUpperInvariant();

    private static bool IsPlausibleCallsign(string call)
    {
        string c = NormalizeCallToken(call);
        return c.Length is >= 3 and <= 12 &&
               c.Any(char.IsLetter) &&
               c.Any(char.IsDigit) &&
               c.All(ch => char.IsLetterOrDigit(ch) || ch == '/');
    }

    private static string FormatFt8Report(double snr)
    {
        int value = Math.Clamp((int)Math.Round(snr), -24, 24);
        return value >= 0 ? $"+{value:00}" : $"-{Math.Abs(value):00}";
    }
}
