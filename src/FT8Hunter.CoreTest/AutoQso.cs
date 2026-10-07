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

    private CancellationTokenSource? _autoQsoCts;
    private AutoQsoState _autoQsoState = AutoQsoState.Off;
    private string? _autoDxCall;
    private string? _autoLastTxMessage;
    private DateTimeOffset? _autoLastTxSlot;
    private DateTimeOffset? _autoLastProcessedWindow;
    private int? _autoTxParity;
    private int _autoAttempts;
    private bool _autoTxInProgress;

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

        string cq = $"CQ {myCall} {myGrid}";
        AutoQsoStatus.Text = "Auto QSO: invio CQ...";
        AddLog("AUTO", $"START — {cq}");

        await AutoSendAsync(cq, AutoQsoState.WaitingCaller, resetAttempts: true);
    }

    private void StopAutoQso_Click(object sender, RoutedEventArgs e)
    {
        StopAutoQsoInternal("STOP AUTO", true);
    }

    private void StopAutoQsoInternal(string reason, bool log)
    {
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

        if (AutoQsoStatus is not null)
            AutoQsoStatus.Text = "Auto QSO fermo";

        if (log)
            AddLog("AUTO", reason);
    }

    private void AutoQsoOnDecodedPeriod(IReadOnlyList<DecodeResult> results, DateTimeOffset windowStart)
    {
        if (_autoQsoState is AutoQsoState.Off or AutoQsoState.Complete)
            return;

        if (_autoTxInProgress || _autoQsoCts is null || _autoTxParity is null)
            return;

        if (_autoLastProcessedWindow == windowStart)
            return;

        // Le finestre con la stessa parità del nostro TX sono i nostri slot di trasmissione.
        // La risposta deve arrivare sulla parità opposta.
        if (GetFt8SlotParity(windowStart) == _autoTxParity.Value)
            return;

        if (_autoLastTxSlot.HasValue && windowStart < _autoLastTxSlot.Value.AddSeconds(10))
            return;

        _autoLastProcessedWindow = windowStart;

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
                RetryCurrentAutoMessage("nessuna risposta al CQ");
                return;
            }

            string dx = caller.Parsed!.Value.DxCall;
            _autoDxCall = dx;

            int callerHz = Math.Clamp((int)Math.Round(caller.Result.FrequencyHz), 200, 3000);
            TxPositionBox.Text = callerHz.ToString(System.Globalization.CultureInfo.InvariantCulture);
            _txWaterfallHz = callerHz;

            string report = FormatFt8Report(caller.Result.Snr);
            string reply = $"{dx} {myCall} {report}";

            AddLog("AUTO", $"CALLER selezionato: {dx} | SNR {caller.Result.Snr:+#;-#;0} dB | {callerHz} Hz → '{reply}'");
            AutoQsoStatus.Text = $"QSO con {dx}: invio rapporto {report}";

            _ = AutoSendAsync(reply, AutoQsoState.WaitingRReport, resetAttempts: true);
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
                RetryCurrentAutoMessage($"nessuna risposta da {dx}");
                return;
            }

            string payload = addressed.Parsed!.Value.Payload;

            if (Regex.IsMatch(payload, "^R[+-][0-9]{2}$", RegexOptions.CultureInvariant))
            {
                string rr73 = $"{dx} {myCall} RR73";
                AddLog("AUTO", $"R-report ricevuto da {dx}: {payload} → '{rr73}'");
                AutoQsoStatus.Text = $"QSO con {dx}: R-report {payload}, invio RR73";
                _ = SendFinalRr73Async(rr73, dx);
                return;
            }

            if (payload is "RR73" or "73")
            {
                FinishAutoQso(dx, $"ricevuto {payload}");
                return;
            }

            AddLog("AUTO", $"Messaggio da {dx} ricevuto ma non ancora conclusivo: '{addressed.Result.Message.Trim()}'");
            RetryCurrentAutoMessage($"atteso R-report da {dx}");
        }
    }

    private async Task AutoSendAsync(string message, AutoQsoState stateAfterTx, bool resetAttempts)
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

        try
        {
            DateTimeOffset slot = await SendFt8MessageAsync(message, _autoQsoCts.Token, "AUTO", _autoTxParity);
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
                AddLog("AUTO", "Operazione annullata.");
        }
        catch (Exception ex)
        {
            AddLog("AUTO", "ERRORE: " + ex.Message);
            StopAutoQsoInternal("Errore Auto QSO", false);
        }
        finally
        {
            _autoTxInProgress = false;
        }
    }

    private async Task SendFinalRr73Async(string message, string dx)
    {
        if (_autoQsoCts is null || _autoTxInProgress)
            return;

        _autoAttempts = 1;
        _autoLastTxMessage = message;
        _autoTxInProgress = true;

        try
        {
            DateTimeOffset slot = await SendFt8MessageAsync(message, _autoQsoCts.Token, "AUTO", _autoTxParity);
            _autoLastTxSlot = slot;
            FinishAutoQso(dx, "RR73 trasmesso");
        }
        catch (OperationCanceledException)
        {
            AddLog("AUTO", "RR73 annullato.");
        }
        catch (Exception ex)
        {
            AddLog("AUTO", "ERRORE RR73: " + ex.Message);
            StopAutoQsoInternal("Errore RR73", false);
        }
        finally
        {
            _autoTxInProgress = false;
        }
    }

    private void RetryCurrentAutoMessage(string reason)
    {
        if (_autoQsoCts is null || string.IsNullOrWhiteSpace(_autoLastTxMessage))
            return;

        if (_autoAttempts >= 3)
        {
            string phase = _autoQsoState == AutoQsoState.WaitingCaller ? "CQ" : $"QSO con {_autoDxCall}";
            AddLog("AUTO", $"STOP — {phase}: 3 tentativi senza risposta valida ({reason}).");
            AutoQsoStatus.Text = $"Auto QSO fermato: 3 tentativi ({reason})";
            try { _autoQsoCts.Cancel(); } catch { }
            try { _autoQsoCts.Dispose(); } catch { }
            _autoQsoCts = null;
            _autoQsoState = AutoQsoState.Off;
            return;
        }

        AddLog("AUTO", $"Retry {_autoAttempts + 1}/3: {reason}.");
        _ = AutoSendAsync(_autoLastTxMessage, _autoQsoState, resetAttempts: false);
    }

    private void FinishAutoQso(string dx, string reason)
    {
        _autoQsoState = AutoQsoState.Complete;
        AutoQsoStatus.Text = $"QSO COMPLETATO con {dx} — {reason}";
        AddLog("AUTO", $"QSO COMPLETATO con {dx} — {reason}");

        try { _autoQsoCts?.Dispose(); } catch { }
        _autoQsoCts = null;
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
