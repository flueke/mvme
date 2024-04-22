#include <iostream>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "mvme_session.h"
#include "listfile_replay.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"

using std::cerr;
using std::cout;
using std::endl;

using namespace mesytec;
using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    mvme_init();

    if (argc < 2 || argv[1] == std::string("-h") || argv[1] == std::string("--help"))
    {
        cout << endl << "Usage: " << argv[0] << " <inputListfile>" << endl << endl;
        cout << "mvlc_listfile_updater is a tool for updating MVLC listfiles written by" << endl
            << "mvme-1.0.1 to the newer mvme-1.1 format." << endl
            << "The program  reads the mvme VMEConfig from the input listfile and generates " << endl
            << "a mesytec-mvlc CrateConfig. Both configs and the readout data are then written " << endl
            << "out to a new output file." << endl;

        return 1;
    }

    QString inputFilename(argv[1]);
    QString outputFilename = QFileInfo(inputFilename).baseName() + "_updated.zip";

#define DO_CATCH 1

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

        // Read and skip past the preamble so that we do not read it again in the
        // loop below.
        auto preamble = mvlc::listfile::read_preamble(*readHandle);
        readHandle->seek(preamble.endOffset);

        // Create and open the output listfile
        cout << "Opening " << outputFilename.toStdString() << " for writing" << endl;
        mvlc::listfile::ZipCreator zipCreator;
        zipCreator.createArchive(outputFilename.toStdString());
        auto writeHandle = zipCreator.createZIPEntry(
            listfileHandle.listfileFilename.toStdString());

        // Write the standard mvlc preamble followed by the mvme VMEConfig.
        mvlc::listfile::listfile_write_preamble(*writeHandle, mvlcCrateConfig);
        mvme_mvlc::listfile_write_mvme_config(*writeHandle, 0, *vmeConfig);

        mvlc::ReadoutBuffer workBuffer(Megabytes(1));

        struct Counters
        {
            size_t totalBytesRead = 0;
            size_t totalBytesWritten = 0;
        };

        Counters counters = {};

        // Main loop copying data from readHandle to writeHandle.
        while (true)
        {
            size_t bytesRead = readHandle->read(
                workBuffer.data() + workBuffer.used(),
                workBuffer.free());
            workBuffer.use(bytesRead);

            if (bytesRead == 0)
                break;

            counters.totalBytesRead += bytesRead;

            counters.totalBytesWritten += writeHandle->write(
                workBuffer.data(), workBuffer.used());

            workBuffer.clear();
        }

        cout << "totalBytesRead=" << counters.totalBytesRead << endl;
        cout << "totalBytesWritten=" << counters.totalBytesWritten << endl;

        {
            auto rh = zipReader.openEntry("messages.log");
            zipCreator.closeCurrentEntry();
            auto wh = zipCreator.createZIPEntry("messages.log", 0);

            size_t bytesRead = 0u;

            do
            {
                workBuffer.clear();
                bytesRead = rh->read(workBuffer.data(), workBuffer.free());
                workBuffer.use(bytesRead);
                wh->write(workBuffer.data(), workBuffer.used());
            } while (bytesRead > 0);
        }

        {
            auto rh = zipReader.openEntry("analysis.analysis");
            zipCreator.closeCurrentEntry();
            auto wh = zipCreator.createZIPEntry("analysis.analysis", 0);

            size_t bytesRead = 0u;

            do
            {
                workBuffer.clear();
                bytesRead = rh->read(workBuffer.data(), workBuffer.free());
                workBuffer.use(bytesRead);
                wh->write(workBuffer.data(), workBuffer.used());
            } while (bytesRead > 0);
        }
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
