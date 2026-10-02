namespace FT8Hunter.CoreTest;

public partial class MainWindow
{
    private const byte Icom7300Address = 0x94;
    private const byte IcomControllerAddress = 0xE0;

    // IC-7300 CI-V command 26 00 sets the selected VFO mode, DATA flag and filter.
    // USB-D FIL1 = 26 00 01 01 01
    // USB   FIL1 = 26 00 01 00 01
    private void SetIcom7300UsbData(bool dataOn)
    {
        EnsureRig();

        byte[] command =
        {
            0xFE, 0xFE,
            Icom7300Address, IcomControllerAddress,
            0x26, 0x00,
            0x01,                    // USB
            dataOn ? (byte)0x01 : (byte)0x00,
            0x01,                    // FIL1
            0xFD
        };

        // IC-7300/OmniRig normally works with CI-V echo enabled:
        // 10-byte echo + 6-byte ACK = 16 bytes.
        _rig!.SendCustomCommand(command, 16, "");
        AddLog("RADIO", dataOn
            ? "CI-V diretto: USB-D / DATA ON / FIL1"
            : "CI-V diretto: USB / DATA OFF / FIL1");
    }
}
