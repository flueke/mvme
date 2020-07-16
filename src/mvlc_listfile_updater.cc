#include <iostream>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvme_session.h"
#include "listfile_replay.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvlc_listfile.h"

using std::cerr;
using std::cout;
using std::endl;

using namespace mesytec;
using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    mvme_init();

    if (argc != 2)
    {
        cerr << "Usage: " << argv[0] << " <inputListfile>" << endl;
        return 1;
    }

    QString inputFilename(argv[1]);
    QString outputFilename = QFileInfo(inputFilename).baseName() + "_updated.zip";

#define DO_CATCH 0

#if DO_CATCH
    try
#endif
    {
        // Open the input listfile, read the mvme VMEConfig from it and convert
        // it to an mvlc CrateConfig.
        auto listfileHandle = open_listfile(inputFilename);
        std::unique_ptr<VMEConfig> vmeConfig;

        std::error_code ec;
        std::tie(vmeConfig, ec) = read_vme_config_from_listfile(listfileHandle);
        if (ec) throw ec;

        auto mvlcCrateConfig = vmeconfig_to_crateconfig(vmeConfig.get());

        // Reopen the input listfile using the mesytec::mvlc::ZipReader
        mvlc::listfile::ZipReader zipReader;
        zipReader.openArchive(listfileHandle.inputFilename.toStdString());
        auto readHandle = zipReader.openEntry(listfileHandle.listfileFilename.toStdString());

        auto inputPreamble = mvlc::listfile::read_preamble(*readHandle);
        cout << inputPreamble.magic << endl;

        // Create and open the output listfile
        mvlc::listfile::ZipCreator zipCreator;
        zipCreator.createArchive(outputFilename.toStdString());
        auto writeHandle = zipCreator.createLZ4Entry(
            listfileHandle.listfileFilename.toStdString());

        // Write the standard mvlc preamble followed by the mvme VMEConfig.
        mvlc::listfile::listfile_write_preamble(*writeHandle, mvlcCrateConfig);
        mvme_mvlc_listfile::listfile_write_mvme_config(*writeHandle, vmeConfig.get());

        mvlc::ReadoutBuffer workBuffer(Megabytes(1));
        mvlc::ReadoutBuffer previousData(workBuffer.capacity());

        struct Counters
        {
            size_t totalBytesRead = 0;
            size_t totalBytesWritten = 0;
        };

        Counters counters = {};

        // Main loop copying data from readHandle to writeHandle.
        while (true)
        {
            if (previousData.used())
            {
                workBuffer.ensureFreeSpace(previousData.used());
                std::memcpy(workBuffer.data() + workBuffer.used(),
                            previousData.data(), previousData.used());
                workBuffer.use(previousData.used());
                previousData.clear();
            }

            size_t bytesRead = readHandle->read(
                workBuffer.data() + workBuffer.used(),
                workBuffer.free());
            workBuffer.use(bytesRead);

            if (bytesRead == 0)
                break;

            counters.totalBytesRead += bytesRead;

            // Buffer cleanup so that we do not have incomplete frames in the
            // workBuffer.
            mvlc::fixup_buffer(
                mvlcCrateConfig.connectionType,
                workBuffer, previousData);

            // TODO: do any processing here

            counters.totalBytesWritten += writeHandle->write(
                workBuffer.data(), workBuffer.used());
            workBuffer.clear();
        }

        cout << "totalBytesRead=" << counters.totalBytesRead << endl;
        cout << "totalBytesWritten=" << counters.totalBytesWritten << endl;
    }
#if DO_CATCH
    catch (const QString &s)
    {
        cerr << "Caught an exception: " << s.toStdString() << endl;
        return 1;
    }
    catch (const std::error_code &ec)
    {
        cerr << "Caught an error_code: " << ec.message() << endl;
        return 1;
    }
    catch(const std::runtime_error &e)
    {
        cerr << "Caught an exception: " << e.what() << endl;
        return 1;
    }
#endif

    mvme_shutdown();
    return 0;

}
