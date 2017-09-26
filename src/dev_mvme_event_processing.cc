#include <QCoreApplication>
#include "mvme_listfile.h"
#include "mvme_event_processor.h"
#include "vme_config.h"
#include "mvme_context.h"
#include "analysis/analysis.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <fstream>
#include <array>
#include <iostream>

using std::cout;
using std::cerr;
using std::endl;

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

    auto configJson = QJsonDocument::fromJson(vmeConfigBuffer);
    auto vmeConfig = new VMEConfig;
    vmeConfig->read(configJson.object()["DAQConfig"].toObject());
    return vmeConfig;
}

VMEConfig *read_config_from_listfile(std::ifstream &infile)
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

MVMEContext *g_context = nullptr;
MVMEEventProcessor *g_eventProcessor = nullptr;

template<typename LF>
void process_listfile(std::ifstream &infile)
{
    DataBuffer sectionBuffer(Megabytes(1));

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
        else
        {
            g_eventProcessor->processDataBuffer(&sectionBuffer);
        }
    }
}

void process_listfile(std::ifstream &infile)
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

    // Move to the start of the first section
    auto firstSectionOffset = ((fileVersion == 0)
                               ? listfile_v0::FirstSectionOffset
                               : listfile_v1::FirstSectionOffset);

    infile.seekg(firstSectionOffset, std::ifstream::beg);

    cout << "Detected listfile version " << fileVersion << endl;

    if (fileVersion == 0)
    {
        process_listfile<listfile_v0>(infile);
    }
    else
    {
        process_listfile<listfile_v1>(infile);
    }
}

int main(int argc, char *argv[])
{
    s32 nRuns = 10;
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

    try
    {
        auto vmeConfig = read_config_from_listfile(infile);
        g_context = new MVMEContext(nullptr);
        g_context->setVMEConfig(vmeConfig);
        g_eventProcessor = new MVMEEventProcessor(g_context);
        g_eventProcessor->newRun({});

        while (nRuns-- > 0)
        {
            infile.seekg(0, std::ifstream::beg);
            process_listfile(infile);
        }

        auto stats = g_eventProcessor->getStats();
        double secsElapsed = stats.startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;
        double mbRead = stats.bytesProcessed / (1024.0 * 1024.0);
        double mbPerSec = mbRead / secsElapsed;

        cout << secsElapsed << " seconds elapsed" << endl;
        cout << mbRead << " MB read" << endl;
        cout << mbPerSec << " MB/s" << endl;
        cout << stats.buffersProcessed << " buffers processed" << endl;
        cout << stats.buffersWithErrors << " buffers with errors" << endl;
        cout << stats.eventSections << " event section seen" << endl;
        cout << stats.invalidEventIndices << " invalid event indices" << endl;
        for (u32 ei = 0; ei < MVMEEventProcessorStats::MaxEvents; ++ei)
        {
            for (u32 mi = 0; mi < MVMEEventProcessorStats::MaxModulesPerEvent; ++mi)
            {
                u32 count = stats.moduleCounters[ei][mi];
                if (count)
                {
                    cout << "event=" << ei << ", module=" << mi << ", count=" << count << endl;
                }
            }
        }

        for (u32 ei = 0; ei < MVMEEventProcessorStats::MaxEvents; ++ei)
        {
            u32 count = stats.eventCounters[ei];
            if (count)
            {
                cout << "event=" << ei << ", count=" << count << endl;
            }
        }
    }
    catch (const std::exception &e)
    {
        cerr << e.what() << endl;
        return 1;
    }

    return 0;
}
