$path = 'src/FT8Hunter.CoreTest/MainWindow.xaml.cs'
$text = Get-Content $path -Raw

# Standard IC-7300 OmniRig profile maps PM_DIG_U to RTTY-D.
# For this app we want true USB-D, so replace mode writes with our direct CI-V helper.
$text = $text.Replace('_rig!.Mode = PM_SSB_U;', 'SetIcom7300UsbData(false);')
$text = $text.Replace('_rig!.Mode = PM_DIG_U;', 'SetIcom7300UsbData(true);')
$text = $text.Replace('_rig.Mode = PM_DIG_U;', 'SetIcom7300UsbData(true);')

# OmniRig still reports the base mode as USB after DATA is enabled on the IC-7300.
# Verify the USB carrier mode instead of expecting PM_DIG_U from the generic profile.
$text = $text.Replace('VerifyModeLater(PM_DIG_U, "DATA / USB-D");', 'VerifyModeLater(PM_SSB_U, "USB-D / DATA ON");')

Set-Content -Path $path -Value $text -Encoding utf8
Write-Host 'Applied IC-7300 USB-D patch.'
