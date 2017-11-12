#include <QCoreApplication>
#include "mvme_listfile.h"
#include "vme_config.h"
#include "mvme_context.h"
#include "mvme_stream_processor.h"
#include "analysis/analysis.h"
#include "util/strings.h"
#include "util/counters.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <fstream>
#include <array>
#include <iostream>

using std::cout;
using std::cerr;
using std::endl;

using namespace analysis;

namespace
{

u32 read_listfile_version(std::ifstream &infile)
{
    u32 fileVersion = 0;
    // Read the fourCC that's at the start of listfiles from version 1 and up.
    const size_t bytesToRead = 4;
    char fourCC[bytesToRead] = {};

    infile.read(fourCC, bytesToRead);

    if (std::strncmp(fourCC, listfile_v1::FourCC, bytesToRead) == 0)
    {
        infile.read(reinterpret_cast<char *>(&fileVersion), sizeof(fileVersion));
    }

    infile.seekg(0, std::ifstream::beg);

    return fileVersion;

}

template<typename LF>
VMEConfig *read_config_from_listfile(std::ifstream &infile)
{
    DataBuffer sectionBuffer(Megabytes(1));
    QByteArray vmeConfigBuffer;

    while (true)
    {
        sectionBuffer.used = 0;
        u32 *sectionHeaderPtr = sectionBuffer.asU32();
        infile.read((char *)sectionHeaderPtr, sizeof(u32));
        u32 sectionType   = (*sectionHeaderPtr & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionSize   = (*sectionHeaderPtr & LF::SectionSizeMask) >> LF::SectionSizeShift;

        ssize_t bytesToRead = sectionSize * sizeof(u32);
        sectionBuffer.ensureCapacity(bytesToRead + sizeof(u32));

        infile.read(reinterpret_cast<char *>(sectionBuffer.asU32(sizeof(u32))), bytesToRead);

        if (infile.gcount() != bytesToRead)
        {
            throw std::runtime_error("Error reading full section");
        }

        sectionBuffer.used = bytesToRead + sizeof(u32);

        if (sectionType == ListfileSections::SectionType_Config)
        {
            vmeConfigBuffer.append(reinterpret_cast<const char *>(sectionBuffer.data + sizeof(u32)),
                                   sectionBuffer.used - sizeof(u32));
        }
        else
        {
            break;
        }
    }

    infile.seekg(0, std::ifstream::beg);

    auto configJson = QJsonDocument::fromJson(vmeConfigBuffer);
    auto vmeConfig = new VMEConfig;
    vmeConfig->read(configJson.object()["DAQConfig"].toObject());
    return vmeConfig;
}

VMEConfig *read_config_from_listfile(std::ifstream &infile)
{
    u32 fileVersion = read_listfile_version(infile);

    // Move to the start of the first section
    auto firstSectionOffset = ((fileVersion == 0)
                               ? listfile_v0::FirstSectionOffset
                               : listfile_v1::FirstSectionOffset);

    infile.seekg(firstSectionOffset, std::ifstream::beg);

    cout << "Detected listfile version " << fileVersion << endl;

    if (fileVersion == 0)
    {
        return read_config_from_listfile<listfile_v0>(infile);
    }
    else
    {
        return read_config_from_listfile<listfile_v1>(infile);
    }
}


//
// process_listfile
//

struct Context
{
    RunInfo runInfo;
    Analysis *analysis;
    VMEConfig *vmeConfig;
    u32 listfileVersion;
    MVMEStreamProcessor::Logger logger;
    MVMEStreamProcessor streamProcessor;
};


template<typename LF>
void process_listfile(Context &context, std::ifstream &infile)
{
    DataBuffer sectionBuffer(Megabytes(1));

    context.streamProcessor.beginRun(
        context.runInfo,
        context.analysis,
        context.vmeConfig,
        context.listfileVersion,
        context.logger);

    auto &counters = context.streamProcessor.getCounters();
    counters.startTime = QDateTime::currentDateTime();
    counters.stopTime  = QDateTime();

    while (true)
    {
        sectionBuffer.used = 0;
        u32 *sectionHeaderPtr = sectionBuffer.asU32();
        infile.read((char *)sectionHeaderPtr, sizeof(u32));
        u32 sectionType   = (*sectionHeaderPtr & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionSize   = (*sectionHeaderPtr & LF::SectionSizeMask) >> LF::SectionSizeShift;

        ssize_t bytesToRead = sectionSize * sizeof(u32);
        sectionBuffer.ensureCapacity(bytesToRead + sizeof(u32));

        infile.read(reinterpret_cast<char *>(sectionBuffer.asU32(sizeof(u32))), bytesToRead);

        if (infile.gcount() != bytesToRead)
        {
            throw std::runtime_error("Error reading full section");
        }

        sectionBuffer.used = bytesToRead + sizeof(u32);

        if (sectionType == ListfileSections::SectionType_End)
        {
            break;
        }

        context.streamProcessor.processDataBuffer(&sectionBuffer);
    }

    counters.stopTime = QDateTime::currentDateTime();
}

void process_listfile(Context &context, std::ifstream &infile)
{
    u32 fileVersion = read_listfile_version(infile);

    // Move to the start of the first section
    auto firstSectionOffset = ((fileVersion == 0)
                               ? listfile_v0::FirstSectionOffset
                               : listfile_v1::FirstSectionOffset);

    infile.seekg(firstSectionOffset, std::ifstream::beg);

    cout << "Detected listfile version " << fileVersion << endl;

    if (fileVersion == 0)
    {
        process_listfile<listfile_v0>(context, infile);
    }
    else
    {
        process_listfile<listfile_v1>(context, infile);
    }
}

} // end anon namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc != 2)
    {
        cerr << "Invalid number of arguments" << endl;
        cerr << "Usage: " << argv[0] << " <listfile>" << endl;
        return 1;
    }

    std::ifstream infile(argv[1], std::ios::binary);

    if (!infile.is_open())
    {
        cerr << "Error opening " << argv[1] << " for reading: " << std::strerror(errno) << endl;
        return 1;
    }

    infile.exceptions(std::ifstream::badbit | std::ifstream::failbit | std::ifstream::eofbit);

#if 1
    try
    {
#endif
        auto logger = [] (const QString &msg) { qDebug() << msg; };

        Context context = {};
        context.analysis = new analysis::Analysis();
        context.vmeConfig = read_config_from_listfile(infile);
        context.listfileVersion = read_listfile_version(infile);
        context.logger = logger;

        process_listfile(context, infile);

        auto counters = context.streamProcessor.getCounters();

        auto elapsed_ms = counters.startTime.msecsTo(counters.stopTime);
        double dt_s = elapsed_ms / 1000.0;

        double bytesPerSecond = counters.bytesProcessed / dt_s;

        qDebug() << "startTime:" << counters.startTime.toString();
        qDebug() << "endTime:" << counters.stopTime.toString();
        qDebug() << "dt_s:" << dt_s;
        qDebug() << "bytesProcessed:" << format_number(counters.bytesProcessed,
                                                       "B", UnitScaling::Binary);

        qDebug() << "bytesPerSecond:" << format_number(bytesPerSecond,
                                                       "B/s", UnitScaling::Binary);

        for (u32 ei = 0; ei < counters.eventCounters.size(); ei++)
        {
            if (counters.eventCounters[ei] > 0.0)
            {
                qDebug() << (QString("ei=%1, count=%2, rate=%3")
                             .arg(ei)
                             //.arg(counters.eventCounters[ei])
                             .arg(format_number(counters.eventCounters[ei], "", UnitScaling::Decimal))
                             .arg(format_number(counters.eventCounters[ei] / dt_s, "Hz", UnitScaling::Decimal))
                            );
            }
        }

        for (u32 ei = 0; ei < counters.moduleCounters.size(); ei++)
        {
            for (u32 mi = 0; mi < counters.moduleCounters[ei].size(); mi++)
            {
                if (counters.moduleCounters[ei][mi] > 0.0)
                {
                    qDebug() << (QString("ei=%1, mi=%2 count=%3, rate=%4")
                                 .arg(ei).arg(mi)
                                 .arg(format_number(counters.moduleCounters[ei][mi], "", UnitScaling::Decimal))
                                 .arg(format_number(counters.moduleCounters[ei][mi] / dt_s, "Hz", UnitScaling::Decimal))
                                );
                }
            }
        }

#if 0
        g_context = new MVMEContext(nullptr);
        g_context->setVMEConfig(vmeConfig);
        g_eventProcessor = new MVMEEventProcessor(g_context);
        g_eventProcessor->newRun({}, {});

        while (nRuns-- > 0)
        {
            infile.seekg(0, std::ifstream::beg);
            process_listfile(infile);
        }

        auto stats = g_eventProcessor->getCounters();
        double secsElapsed = stats.startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;
        double mbRead = stats.bytesProcessed / (1024.0 * 1024.0);
        double mbPerSec = mbRead / secsElapsed;

        qDebug() << stats.startTime.toString();

        cout << "startTime: " << stats.startTime.toString().toLocal8Bit().constData() << endl;
        cout << secsElapsed << " seconds elapsed" << endl;
        cout << mbRead << " MB read" << endl;
        cout << mbPerSec << " MB/s" << endl;
        cout << stats.buffersProcessed << " buffers processed" << endl;
        cout << stats.buffersWithErrors << " buffers with errors" << endl;
        cout << stats.eventSections << " event section seen" << endl;
        cout << stats.invalidEventIndices << " invalid event indices" << endl;
        for (u32 ei = 0; ei < MaxVMEEvents; ++ei)
        {
            for (u32 mi = 0; mi < MaxVMEModules; ++mi)
            {
                u32 count = stats.moduleCounters[ei][mi];
                if (count)
                {
                    cout << "event=" << ei << ", module=" << mi << ", count=" << count << endl;
                }
            }
        }

        for (u32 ei = 0; ei < MaxVMEEvents; ++ei)
        {
            u32 count = stats.eventCounters[ei];
            if (count)
            {
                cout << "event=" << ei << ", count=" << count << endl;
            }
        }
#endif
#if 1
    }
    catch (const std::exception &e)
    {
        cerr << e.what() << endl;
        return 1;
    }
#endif

    return 0;
}
