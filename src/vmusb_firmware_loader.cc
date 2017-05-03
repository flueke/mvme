#include "vmusb_firmware_loader.h"
#include "vmusb_skipHeader.h"
#include "vmusb.h"
#include "mvme_context.h"
#include "mvme.h"

#include <QStandardPaths>
#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QThread>

/* VMUSB Firmware Loading
 * ----------------------
 *
 * The Prog dial needs to be in one of the programming positions P1-P4.
 * When connecting VMUSB will report a Firmware Version of 0000_0000 (or the
 * register reads will just fail and the default in VMUSB is a value of 0).
 *
 * When turning the dial to a programming position during normal operation
 * VMUSB will enter programming mode.
 *
 * Turning the dial back to one of the C1-C4 will leave programming mode if the
 * VMUSB was powered up in programming mode. Otherwise VMUSB will stay in
 * programming mode.
 *
 * After a successfull flash VMUSB will enter it's normal mode again using the
 * newly flashed firmware. There's no need to turn the dial back to C1-C4. But
 * after a power cycle the dial needs to obviously be in one of those positions
 * again, otherwise VMUSB will enter programming mode.
 *
 * From vmusb_flash.cpp:
 * - File data buffer is 220000 bytes long.
 * - skipHeader is called to be able to skip the header of xilinx .bit files.
 * - Flash loop:
 *   * calls xxusb_flashblock_program() on each iteration
 *   * 830 iterations
 *   * data pointer incremented by 256 on each iteration
 *   -> 212480 bytes total
 *   * delay after each xxusb_flashblock_program(): .03 * CLOCKS_PER_SEC
 * - xxusb_reset_toggle() is called after the flash loop, then the new firmware
 *   ID is read out.
 * - Xilinx .bit and .bin files are supported.
 */

static const s32 FirmwareBlocks = 830;
static const s32 FirmwareBlockSize = 256;
static const s32 FirmwareSize = FirmwareBlocks * FirmwareBlockSize; // 212480
static const s32 FirmwareBufferSize = 220000;

/* This code was adapted from xxusb_flashblock_program() in libxxusb.c */
static VMEError flashblock_program(VMUSB *vmusb, char *blockBuffer)
{
    char buf[518] ={(char)0xAA,(char)0xAA,(char)0x55,(char)0x55,(char)0xA0,(char)0xA0};
    char *pbuf=buf+6;

    for (s32 k = 0; k < FirmwareBlockSize; ++k)
    {
        *pbuf++ = *blockBuffer;
        *pbuf++ = *blockBuffer++;
    }

    s32 transferred = 0;
    return vmusb->bulkWrite(buf, sizeof(buf), &transferred, 2000);
}

/* This code was adapted from xxusb_reset_toggle() in libxxusb.c
 * Toggles the reset state of the FPGA while the xxusb is in programming mode.
 */
static VMEError reset_toggle(VMUSB *vmusb)
{
  char buf[2] = {(char)255,(char)255};
  s32 transferred = 0;
  return vmusb->bulkWrite(buf, sizeof(buf), &transferred, 1000);
}

/* Note: This function needs exclusive access to the VMUSB.
 * The VMUSB prog dial must be in one of the programming positions P1-P4.
 */
void vmusb_gui_load_firmware(MVMEContext *context)
{
    auto vmusb = qobject_cast<VMUSB *>(context->getController());
    Q_ASSERT(vmusb);

    auto parentWidget = context->getMainWindow();
    auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(parentWidget, "Open VMUSB firmware file",
                                                    startDir,
                                                    "Firmware Files (*.bin *.bit);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    QFile inFile(fileName);

    auto show_critical = [](const QString &text)
    {
        return QMessageBox::critical(nullptr,
                                     QSL("VMUSB Firmware Update"),
                                     text);
    };

    if (!inFile.open(QIODevice::ReadOnly))
    {
        show_critical(QString("Error opening %1 for reading: %2.")
                      .arg(inFile.fileName())
                      .arg(inFile.errorString()));
        return;
    }

    char fwBuffer[FirmwareBufferSize];

    qint64 bytesRead = inFile.read(fwBuffer, FirmwareBufferSize);

    if (bytesRead < 0)
    {
        show_critical(QString("Error reading from %1: %2.")
                      .arg(inFile.fileName())
                      .arg(inFile.errorString()));
        return;
    }

    if (bytesRead < FirmwareSize)
    {
        show_critical(QString("Error: Input file size is too small."));
        return;
    }

    char *buffP = reinterpret_cast<char *>(skipHeader(fwBuffer));

    if (!buffP)
    {
        show_critical(QString("Error: Input file format not recognized."));
        return;
    }


    if (!vmusb->isOpen())
    {
        show_critical(QString("Error: VMUSB controller is not connected."));
        return;
    }

    //
    // write the flash
    //

    QProgressDialog progress("Writing VMUSB firmware...",
                             "Abort", 0, FirmwareBlocks, nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(1000);

    VMEError error;
    bool canceled = false;

    for (s32 block = 0; block < FirmwareBlocks; ++block)
    {
        error = flashblock_program(vmusb, buffP);
        buffP += FirmwareBlockSize;

        if (error.isError())
        {
            break;
        }

        time_t t1=clock()+(time_t)(.03*CLOCKS_PER_SEC);
        while (t1>clock());

        progress.setValue(block + 1); // this calls processEvents() internally

        if (progress.wasCanceled())
        {
            canceled = true;
            break;
        }
    }

    progress.reset();

    if (canceled)
    {
        show_critical(QString("Firmware update canceled."));
        return;
    }

    if (error.isError())
    {
        show_critical(QString("Error during firmware update: %1.")
                      .arg(error.toString()));
        return;
    }

    error = reset_toggle(vmusb);

    if (error.isError())
    {
        show_critical(QString("Error resetting VMUSB FPGA: %1. Try power-cycling the device.")
                      .arg(error.toString()));
        return;
    }

    QThread::sleep(1);

    error = vmusb->readAllRegisters();
    if (error.isError())
    {
        show_critical(QString("Error reading VMUSB registers: %1. Try power-cycling the device.")
                      .arg(error.toString()));
        return;
    }

    u32 fwId = vmusb->getFirmwareId();
    u32 fwMajor = (fwId & 0xFFFF);
    u32 fwMinor = ((fwId >> 16) &  0xFFFF);

    auto message = (QString("Firmware updated successfully. New firmware ID: %1_%2")
                    .arg(fwMajor, 4, 16, QLatin1Char('0'))
                    .arg(fwMinor, 4, 16, QLatin1Char('0'))
                   );

    QMessageBox::information(nullptr, QSL("VMUSB Firmware Update"), message);
}
