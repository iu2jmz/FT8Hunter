using System.Globalization;
using System.Windows;
using HamDigiSharp.Engine;
using HamDigiSharp.Models;
using NAudio.Wave;
using NAudio.Wave.SampleProviders;

namespace FT8Hunter.CoreTest;

public partial class MainWindow
{
    private CancellationTokenSource? _ft8TxCts;
    private WaveOutEvent? _ft8WaveOut;
    private readonly SemaphoreSlim _txGate = new(1, 1);

    private void RefreshTxAudio_Click(object sender, RoutedEventArgs e) => RefreshTxAudioDevices();

    private void RefreshTxAudioDevices()
    {
        AudioTxCombo.Items.Clear();

        for (int i = 0; i < WaveOut.DeviceCount; i++)
        {
            var caps = WaveOut.GetCapabilities(i);
            AudioTxCombo.Items.Add(new TxAudioItem(i, $"{i}: {caps.ProductName}"));
        }

        AudioTxCombo.DisplayMemberPath = nameof(TxAudioItem.Display);

        if (AudioTxCombo.Items.Count > 0)
        {
            int preferred = -1;
            for (int i = 0; i < AudioTxCombo.Items.Count; i++)
            {
                if (AudioTxCombo.Items[i] is TxAudioItem item &&
                    item.Display.Contains("USB Audio CODEC", StringComparison.OrdinalIgnoreCase))
                {
                    preferred = i;
                    break;
                }
            }

            AudioTxCombo.SelectedIndex = preferred >= 0 ? preferred : 0;
        }

        AddLog("AUDIO", $"Trovate {AudioTxCombo.Items.Count} uscite audio TX.");
    }

    private async void TransmitFt8_Click(object sender, RoutedEventArgs e)
    {
        if (!ValidateFt8TxPrerequisites(showMessage: true))
            return;

        string message = TxMessageBox.Text.Trim().ToUpperInvariant();
        if (string.IsNullOrWhiteSpace(message))
        {
            AddLog("TX", "Bloccato: inserisci il messaggio FT8 da trasmettere.");
            return;
        }

        try
        {
            StopFt8TransmitInternal(log: false);
            _ft8TxCts = new CancellationTokenSource();
            await SendFt8MessageAsync(message, _ft8TxCts.Token, "TX", requiredParity: null);
            Ft8TxStatus.Text = "TX FT8 completato";
        }
        catch (OperationCanceledException)
        {
            Ft8TxStatus.Text = "TX FT8 annullato";
            AddLog("TX", "Trasmissione annullata.");
        }
        catch (Exception ex)
        {
            Ft8TxStatus.Text = "TX FT8: errore";
            AddLog("TX", "ERRORE: " + ex.Message);
        }
        finally
        {
            _ft8TxCts?.Dispose();
            _ft8TxCts = null;
        }
    }

    private bool ValidateFt8TxPrerequisites(bool showMessage)
    {
        if (ArmFt8Tx.IsChecked != true)
        {
            AddLog("TX", "Bloccato: abilita prima ABILITA TX FT8.");
            return false;
        }

        if (!_splitEnabled)
        {
            AddLog("TX", "Bloccato: attiva prima SPLIT.");
            if (showMessage)
                MessageBox.Show("Attiva SPLIT. VFO A resterà in RX e VFO B verrà usato per il TX.", "FT8 TX", MessageBoxButton.OK, MessageBoxImage.Information);
            return false;
        }

        if (AudioTxCombo.SelectedItem is not TxAudioItem)
        {
            RefreshTxAudioDevices();
            AddLog("TX", "Seleziona l'uscita Audio TX del collegamento USB della radio.");
            return false;
        }

        if (!int.TryParse(TxLevelBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int levelPercent) ||
            levelPercent is < 1 or > 90)
        {
            AddLog("TX", "Livello TX non valido: usa un valore tra 1 e 90%.");
            return false;
        }

        try { EnsureRig(); }
        catch (Exception ex)
        {
            AddLog("TX", "Bloccato: " + ex.Message);
            return false;
        }

        return true;
    }

    private async Task<DateTimeOffset> SendFt8MessageAsync(
        string message,
        CancellationToken ct,
        string logTag,
        int? requiredParity)
    {
        if (!ValidateFt8TxPrerequisites(showMessage: false))
            throw new InvalidOperationException("Prerequisiti TX FT8 non soddisfatti.");

        if (AudioTxCombo.SelectedItem is not TxAudioItem txDevice)
            throw new InvalidOperationException("Uscita Audio TX non selezionata.");

        if (!int.TryParse(TxLevelBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int levelPercent))
            throw new InvalidOperationException("Livello TX non valido.");

        await _txGate.WaitAsync(ct);

        try
        {
            int position = ParseTxPosition();
            long rx = ReadRxFrequency();
            ApplyRigSplit(rx, position, $"Preparazione {logTag}");

            using var encoder = new EncoderEngine();
            float[] audio12k = encoder.Encode(message, DigitalMode.FT8, new EncoderOptions
            {
                FrequencyHz = SplitTxAudioHz,
                Amplitude = levelPercent / 100.0
            });

            var mono = new ArraySampleProvider(audio12k, 12_000);
            ISampleProvider stereo = new MonoToStereoSampleProvider(mono);
            ISampleProvider audio48k = new WdlResamplingSampleProvider(stereo, 48_000);

            _ft8WaveOut = new WaveOutEvent
            {
                DeviceNumber = txDevice.Index,
                DesiredLatency = 80,
                NumberOfBuffers = 2
            };

            _ft8WaveOut.Init(new SampleToWaveProvider16(audio48k));

            var playbackStopped = new TaskCompletionSource<StoppedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
            _ft8WaveOut.PlaybackStopped += (_, args) => playbackStopped.TrySetResult(args);

            DateTimeOffset slot = NextFt8Slot(DateTimeOffset.UtcNow, requiredParity);
            DateTimeOffset pttAt = slot.AddMilliseconds(-250);

            Ft8TxStatus.Text = $"{logTag} armato — slot {slot:HH:mm:ss} UTC";
            AddLog(logTag, $"Armato: '{message}' | {txDevice.Display} | {levelPercent}% | slot {slot:HH:mm:ss} UTC | parity {GetFt8SlotParity(slot)}");

            TimeSpan waitPtt = pttAt - DateTimeOffset.UtcNow;
            if (waitPtt > TimeSpan.Zero)
                await Task.Delay(waitPtt, ct);

            _rig!.Tx = PM_TX;
            AddLog(logTag, "PTT ON — pre-key 250 ms");

            TimeSpan waitAudio = slot - DateTimeOffset.UtcNow;
            if (waitAudio > TimeSpan.Zero)
                await Task.Delay(waitAudio, ct);

            Ft8TxStatus.Text = $"TRASMISSIONE — {message}";
            AddTxMessageLog(slot, message);
            AddLog(logTag, $"Audio FT8 START {DateTimeOffset.UtcNow:HH:mm:ss.fff} UTC @ {SplitTxAudioHz} Hz");
            _ft8WaveOut.Play();

            StoppedEventArgs stopped = await playbackStopped.Task.WaitAsync(ct);
            if (stopped.Exception is not null)
                throw stopped.Exception;

            AddLog(logTag, $"Audio FT8 END {DateTimeOffset.UtcNow:HH:mm:ss.fff} UTC");
            return slot;
        }
        finally
        {
            try { _ft8WaveOut?.Stop(); } catch { }
            try { _ft8WaveOut?.Dispose(); } catch { }
            _ft8WaveOut = null;

            try { if (_rig is not null) _rig.Tx = PM_RX; } catch { }
            try { ApplyRxUsbLock(); } catch (Exception ex) { AddLog("LOCK", "Ripristino RX non riuscito: " + ex.Message); }
            AddLog(logTag, "PTT OFF");
            _txGate.Release();
        }
    }

    private void StopTransmitFt8_Click(object sender, RoutedEventArgs e)
    {
        StopAutoQsoInternal("STOP TX manuale", false);
        StopFt8TransmitInternal(log: true);
    }

    private void StopFt8TransmitInternal(bool log)
    {
        try { _ft8TxCts?.Cancel(); } catch { }
        try { _ft8WaveOut?.Stop(); } catch { }
        try { if (_rig is not null) _rig.Tx = PM_RX; } catch { }
        try { ApplyRxUsbLock(); } catch { }

        Ft8TxStatus.Text = "TX FT8 fermo";
        if (log) AddLog("TX", "STOP TX richiesto — PTT OFF.");
    }

    private static DateTimeOffset NextFt8Slot(DateTimeOffset nowUtc, int? requiredParity)
    {
        long slotTicks = TimeSpan.FromSeconds(15).Ticks;
        long slotIndex = (nowUtc.UtcTicks / slotTicks) + 1;

        if (requiredParity.HasValue && (slotIndex & 1L) != requiredParity.Value)
            slotIndex++;

        return new DateTimeOffset(slotIndex * slotTicks, TimeSpan.Zero);
    }

    private static int GetFt8SlotParity(DateTimeOffset slotUtc)
    {
        long slotTicks = TimeSpan.FromSeconds(15).Ticks;
        return (int)((slotUtc.UtcTicks / slotTicks) & 1L);
    }

    private sealed record TxAudioItem(int Index, string Display);

    private sealed class ArraySampleProvider : ISampleProvider
    {
        private readonly float[] _samples;
        private int _position;

        public ArraySampleProvider(float[] samples, int sampleRate)
        {
            _samples = samples;
            WaveFormat = WaveFormat.CreateIeeeFloatWaveFormat(sampleRate, 1);
        }

        public WaveFormat WaveFormat { get; }

        public int Read(float[] buffer, int offset, int count)
        {
            int remaining = _samples.Length - _position;
            int toCopy = Math.Min(count, remaining);
            if (toCopy <= 0) return 0;

            Array.Copy(_samples, _position, buffer, offset, toCopy);
            _position += toCopy;
            return toCopy;
        }
    }
}
