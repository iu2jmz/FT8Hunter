using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using HamDigiSharp.Models;

namespace FT8Hunter.CoreTest;

public partial class MainWindow
{
    private static readonly Brush CqBackground = new SolidColorBrush(Color.FromRgb(21, 101, 192));
    private static readonly Brush TxBackground = new SolidColorBrush(Color.FromRgb(198, 40, 40));
    private static readonly Brush MyRxBackground = new SolidColorBrush(Color.FromRgb(46, 125, 50));
    private static readonly Brush HighlightForeground = Brushes.White;

    private void AddDecodeLog(DecodeResult result, DateTimeOffset windowStart)
    {
        string message = result.Message.Trim();
        string display = $"{windowStart:HH:mm:ss}  {result.Snr,4:+#;-#;0} dB  dt={result.Dt,5:F1}s  {result.FrequencyHz,7:F0} Hz  {message}";
        string myCall = MyCallBox.Text.Trim().ToUpperInvariant();

        // Priorità: un messaggio ricevuto che contiene MY CALL è verde,
        // anche se appartiene a una sequenza QSO. I CQ generici restano blu.
        if (!string.IsNullOrWhiteSpace(myCall) && MessageContainsCall(message, myCall))
        {
            AddStyledLog("RX", display, MyRxBackground, HighlightForeground, FontWeights.SemiBold);
            return;
        }

        if (message.StartsWith("CQ ", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(message, "CQ", StringComparison.OrdinalIgnoreCase))
        {
            AddStyledLog("CQ", display, CqBackground, HighlightForeground, FontWeights.SemiBold);
            return;
        }

        AddStyledLog("FT8", display, null, null, FontWeights.Normal);
    }

    private void AddTxMessageLog(DateTimeOffset slot, string message)
    {
        AddStyledLog("TX", $"{slot:HH:mm:ss} UTC  {SplitTxAudioHz,4} Hz  {message}",
            TxBackground, HighlightForeground, FontWeights.Bold);
    }

    private static bool MessageContainsCall(string message, string call)
    {
        string expected = call.Trim().Trim('<', '>').ToUpperInvariant();

        return message
            .ToUpperInvariant()
            .Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(token => token.Trim('<', '>'))
            .Any(token => string.Equals(token, expected, StringComparison.OrdinalIgnoreCase));
    }

    private void AddStyledLog(string source, string message, Brush? background, Brush? foreground, FontWeight fontWeight)
    {
        string line = $"[{DateTime.Now:HH:mm:ss}] [{source}] {message}";

        var item = new ListBoxItem
        {
            Content = line,
            Padding = new Thickness(5, 2, 5, 2),
            Background = background ?? Brushes.Transparent,
            Foreground = foreground ?? SystemColors.ControlTextBrush,
            FontWeight = fontWeight,
            HorizontalContentAlignment = HorizontalAlignment.Stretch
        };

        LogList.Items.Add(item);
        LogList.ScrollIntoView(item);
    }
}
