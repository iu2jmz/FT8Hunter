$path = 'ThirdParty/HamDigiSharp/HamDigiSharp/Engine/RealTimeDecoder.cs'
if (-not (Test-Path $path)) { throw "RealTimeDecoder.cs non trovato: $path" }

$text = Get-Content $path -Raw
$old = 'public double NegativeDtGuardFraction { get; init; } = 0.08;'
$new = 'public double NegativeDtGuardFraction { get; init; } = 0.02;'

if (-not $text.Contains($old)) {
    throw 'Pattern NegativeDtGuardFraction 0.08 non trovato: patch FT8 realtime non applicata.'
}

$text = $text.Replace($old, $new)
Set-Content -Path $path -Value $text -Encoding utf8
Write-Host 'Applied FT8 realtime tuning: NegativeDtGuardFraction 8% -> 2%.'
