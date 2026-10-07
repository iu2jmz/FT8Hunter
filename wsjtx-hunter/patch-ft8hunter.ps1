param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "1.0.1"
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

# Colori FT8 Hunter: CQ blu, RX indirizzato a noi verde, TX rosso.
# Usiamo sfondi scuri con testo bianco per mantenere un contrasto leggibile.
Replace-Required "widgets/displaytext.cpp" `
    '  bool CQcall = false;' `
    ('  bool CQcall = false;' + [Environment]::NewLine +
     '  bool hunterRxForMe = false;' + [Environment]::NewLine +
     '  bool hunterCq = decodedText.string ().contains (" CQ ") || decodedText.string ().contains (" CQDX ");')

Replace-Required "widgets/displaytext.cpp" `
    '          if ((tw.size () > 0 && tw[0].contains(myCall)) or decodedText.clean_string().contains("; " + myCall)) {' `
    ('          if ((tw.size () > 0 && tw[0].contains(myCall)) or decodedText.clean_string().contains("; " + myCall)) {' + [Environment]::NewLine +
     '            hunterRxForMe = true;')

Replace-Required "widgets/displaytext.cpp" `
    '  insertText (message.trimmed (), bg, fg, decodedText.call (), dxCall);' `
    ('  // FT8 Hunter visual priority: RX per noi > CQ > colori configurabili WSJT-X.' + [Environment]::NewLine +
     '  if (hunterRxForMe)' + [Environment]::NewLine +
     '    {' + [Environment]::NewLine +
     '      bg = QColor {46, 125, 50};       // verde scuro' + [Environment]::NewLine +
     '      fg = QColor {255, 255, 255};     // testo bianco' + [Environment]::NewLine +
     '    }' + [Environment]::NewLine +
     '  else if (hunterCq)' + [Environment]::NewLine +
     '    {' + [Environment]::NewLine +
     '      bg = QColor {21, 101, 192};      // blu leggibile' + [Environment]::NewLine +
     '      fg = QColor {255, 255, 255};' + [Environment]::NewLine +
     '    }' + [Environment]::NewLine +
     [Environment]::NewLine +
     '  insertText (message.trimmed (), bg, fg, decodedText.call (), dxCall);')

Replace-Required "widgets/displaytext.cpp" `
    ('    highlight_types types {Highlight::Tx};' + [Environment]::NewLine +
     '    set_colours (m_config, &bg, &fg, types);') `
    ('    highlight_types types {Highlight::Tx};' + [Environment]::NewLine +
     '    set_colours (m_config, &bg, &fg, types);' + [Environment]::NewLine +
     '    // FT8 Hunter: ogni riga realmente trasmessa da noi resta rossa e leggibile.' + [Environment]::NewLine +
     '    bg = QColor {198, 40, 40};' + [Environment]::NewLine +
     '    fg = QColor {255, 255, 255};')
Write-Host "Applied FT8 Hunter $HunterVersion branding and decode colors to WSJT-X source."
