/* Open listfile
 *   zip
 *   flat
 * Read vmeconfig from listfile
 * build StreamInfo for iteration
 * Iterate formatting events using multievent processing if available and enabled.
 */

#include "mvme_listfile.h"
#include "mvme_stream_iter.h"
#include "mvme_stream_util.h"
#include "vme_config.h"
#include "util.h"

#include <QCoreApplication>
#include <iostream>
#include <getopt.h>
#include <QDebug>

using std::cout;
using std::cerr;
using std::endl;

static void dump_listfile(ListFile *listfile, VMEConfig *vmeConfig)
{
    using namespace mvme_stream;
    using Result = StreamIterator::Result;

    DataBuffer streamBuffer(Kilobytes(100));
    StreamInfo streamInfo = streaminfo_from_vmeconfig(vmeConfig, listfile->getFileVersion());
    StreamIterator streamIter(streamInfo);
    const auto &lfc = listfile_constants(streamInfo.version);
    u64 bufferNumber = 0;

    while (listfile->readSectionsIntoBuffer(&streamBuffer) > 0)
    {
        streamBuffer.id = bufferNumber++;
        streamIter.setStreamBuffer(&streamBuffer);

        qDebug() << __PRETTY_FUNCTION__ << "read buffer #" << streamBuffer.id << ", used =" << streamBuffer.used;

        while (true)
        {
             auto &result(streamIter.next());

             if (result.flags & Result::Error)
             {
                 qDebug() << __PRETTY_FUNCTION__ << "break out of read loop because of Error";
                 break;
             }

             u32 sectionHeader = *streamBuffer.indexU32(result.sectionOffset);
             u32 sectionType   = lfc.section_type(sectionHeader);
             u32 sectionSize   = lfc.section_size(sectionHeader);

             if (result.flags & (Result::EventComplete | Result::MultiEvent))
             {
                 assert(sectionType == ListfileSections::SectionType_Event);

                 u32 crateIndex = lfc.crate_index(sectionHeader);
                 u32 eventIndex = lfc.event_index(sectionHeader);

                 // event section data (either from a full event or from multi event handling
                 for (size_t mi = 0; mi < result.moduleDataOffsets.size(); mi++)
                 {
                     const auto &offsets(result.moduleDataOffsets[mi]);

                     qDebug() << __PRETTY_FUNCTION__
                         << "crateIndex =" << crateIndex
                         << ", eventIndex =" << eventIndex
                         << ", moduleIndex =" << mi
                         << ", offsets.sectionHeader" << offsets.sectionHeader
                         << ", offsets.dataBegin" << offsets.dataBegin
                         << ", offsets.dataEnd" << offsets.dataEnd
                         ;

                     if (!offsets.isValid())
                         break;

                     u32 moduleSectionHeader = *streamBuffer.indexU32(offsets.sectionHeader);
                     u32 *dataBegin = streamBuffer.indexU32(offsets.dataBegin);
                     u32 *dataEnd   = streamBuffer.indexU32(offsets.dataEnd);
                     u32 dataSize   = dataEnd - dataBegin + 1;
                     u32 moduleType = lfc.module_type(moduleSectionHeader);

                     QString str = (QString("crateIndex=%1, eventIndex=%2, moduleIndex=%3, type=%4, sectionOffset=%5, moduleOffset=%6, dataBegin=%7, dataEnd=%8, dataSize=%9")
                                    .arg(crateIndex)
                                    .arg(eventIndex)
                                    .arg(mi)
                                    .arg(moduleType)
                                    .arg(result.sectionOffset)
                                    .arg(offsets.sectionHeader)
                                    .arg(offsets.dataBegin)
                                    .arg(offsets.dataEnd)
                                    .arg(dataSize)
                                   );

                     cout << str.toStdString() << endl;

                     qDebugOutputBuffer(reinterpret_cast<u8 *>(dataBegin), dataSize * sizeof(u32));
                 }
             }
             else if (result.flags & Result::NotSet)
             {
                 qDebug() << "result other than EventComplete";
             }

             // check if we need a new input buffer
             if (result.flags & Result::EndOfInput)
             {
                 qDebug() << __PRETTY_FUNCTION__ << "break out of read loop because of EndOfInput";
                 break;
             }
        }

        streamBuffer.used = 0;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString listfileFilename;
    bool showHelp = false;

    while (true)
    {
        static struct option long_options[] = {
            { "listfile",               required_argument,      nullptr,    0 },
            //{ "analysis",               required_argument,      nullptr,    0 },
            //{ "session-out",            required_argument,      nullptr,    0 },
            { "help",                   no_argument,            nullptr,    0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "listfile") { listfileFilename = QString(optarg); }
        //if (opt_name == "analysis") { analysisFilename = QString(optarg); }
        //if (opt_name == "session-out") { sessionOutFilename = QString(optarg); }
        if (opt_name == "help") { showHelp = true; }
    }

    auto do_show_help = [=]()
    {
        cout << "Usage: " << argv[0] << " --listfile <filename>" << endl;
        cout << "  The given filename may refer to a plain .mvmelst file or an mvme archive (.zip)." << endl;
    };

    if (showHelp)
    {
        do_show_help();
        return 0;
    }

    if (listfileFilename.isEmpty())
    {
        cerr << "Missing argument --listfile" << endl << endl;
        do_show_help();
        return 1;
    }

    try
    {
        auto openResult = open_listfile(listfileFilename);

        if (!openResult.listfile)
            return 1;

        std::unique_ptr<VMEConfig> vmeConfig(read_config_from_listfile(openResult.listfile.get()));

        dump_listfile(openResult.listfile.get(), vmeConfig.get());
    }
    catch (const std::exception &e)
    {
        cerr << e.what() << endl;
        return 1;
    }
    catch (const QString &e)
    {
        cerr << e.toStdString() << endl;
        return 1;
    }

    return 0;
}
