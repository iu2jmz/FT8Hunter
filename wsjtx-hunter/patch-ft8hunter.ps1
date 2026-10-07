param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "1.0.0"
)

$ErrorActionPreference = "Stop"

function Replace-Required([string]$Path, [string]$Old, [string]$New) {
    $full = Join-Path $SourceDir $Path
    if (-not (Test-Path $full)) { throw "File non trovato: $full" }
    $text = Get-Content $full -Raw
    if (-not $text.Contains($Old)) { throw "Pattern non trovato in $Path : $Old" }
    $text = $text.Replace($Old, $New)
    Set-Content -Path $full -Value $text -Encoding utf8
}

# Branding applicativo. DSP, decoder e sincronizzazione restano quelli originali WSJT-X.
Replace-Required "main.cpp" 'a.setApplicationName ("WSJT-X");' 'a.setApplicationName ("FT8 Hunter");'
Replace-Required "main.cpp" 'a.setApplicationVersion (version ());' ('a.setApplicationVersion ("' + $HunterVersion + '");')
Replace-Required "widgets/mainwindow.ui" '<string>WSJT-X   by K1JT</string>' ('<string>FT8 Hunter ' + $HunterVersion + ' - WSJT-X Engine</string>')

# About: identifica chiaramente il fork e mantiene copyright/licenza originali.
Replace-Required "widgets/about.cpp" '"WSJT-X implements a number of digital modes designed for <br />"' '"FT8 Hunter is a modified derivative of WSJT-X. <br />"'

Write-Host "Applied FT8 Hunter $HunterVersion branding to WSJT-X source."
