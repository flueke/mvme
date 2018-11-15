#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <fstream>
#include <array>
#include <iostream>
#include <getopt.h>
#include <signal.h>

#include "analysis/analysis.h"
#include "analysis/analysis_session.h"
#include "data_server.h"
#include "mvme_listfile_utils.h"
#include "mvme_stream_processor.h"
#include "util/counters.h"
#include "util/strings.h"
#include "vme_config.h"

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

    const auto &lfc = listfile_constants();

    if (std::strncmp(fourCC, lfc.FourCC, bytesToRead) == 0)
    {
        infile.read(reinterpret_cast<char *>(&fileVersion), sizeof(fileVersion));
    }

    infile.seekg(0, std::ifstream::beg);

    return fileVersion;

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
    std::unique_ptr<AnalysisDataServer> dataServer;
};

void process_listfile(Context &context, ListFile *listfile)
{
    assert(listfile);

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

        s32 numSections = listfile->readSectionsIntoBuffer(&sectionBuffer);

        if (numSections <= 0)
            break;

        context.streamProcessor.processDataBuffer(&sectionBuffer);
        // This is needed for the sockets used in the analysis data server to work.
        qApp->processEvents(QEventLoop::AllEvents);
    }

    context.streamProcessor.endRun();

    counters.stopTime = QDateTime::currentDateTime();
}

void load_analysis_config(const QString &filename, Analysis *analysis, VMEConfig *vmeConfig = nullptr)
{
    assert(analysis);
    QFile infile(filename);

    if (!infile.open(QIODevice::ReadOnly))
        throw infile.errorString();

    auto doc(QJsonDocument::fromJson(infile.readAll()));

    auto json = doc.object();

    if (json.contains("AnalysisNG"))
    {
        json = json["AnalysisNG"].toObject();
    }

    auto readResult = analysis->read(json, vmeConfig);

    if (!readResult)
    {
        throw readResult.toRichText();
    }
}

} // end anon namespace

int main(int argc, char *argv[])
{
#ifndef Q_OS_WIN
    signal(SIGPIPE, SIG_IGN);
#endif


    QCoreApplication app(argc, argv);

    QString analysisFilename;
    bool writeSession = false;
    bool showHelp = false;
    bool enableAnalysisServer = false;

    while (true)
    {
        static struct option long_options[] = {
            { "analysis",               required_argument,      nullptr,    0 },
            { "write-session",          no_argument,            nullptr,    0 },
            { "enable-analysis-server", no_argument,            nullptr,    0 },
            { "help",                   no_argument,            nullptr,    0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == '?') // Unrecognized option
        {
            return 1;
        }

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "write-session")            { writeSession = true; }
        if (opt_name == "analysis")                 { analysisFilename = QString(optarg); }
        if (opt_name == "enable-analysis-server")   { enableAnalysisServer = true; }
        if (opt_name == "help")                     { showHelp = true; }
    }

    if (showHelp)
    {
        cout << "Usage: " << argv[0]
            << " [--analysis <filename>]"
            << " [--write-session] [--enable-analysis-server]"
            << " listfile1 listfile2 ... listfileN"
            << endl;

        cout << "Example: " << argv[0]
            << " --analysis example.analysis"
            << " myrun001.zip myrun002.mvmelst"
            << endl;

        return 0;
    }

    QVector<QString> listfiles;

    for (int i = optind; i < argc; i++)
    {
        listfiles.push_back(argv[i]);
    }

    if (listfiles.isEmpty())
    {
        cerr << "No listfiles specified, exiting" << endl;
        return 1;
    }

    try
    {
        QVector<QString> failedToOpen;
        size_t filesProcessed = 0u;

        auto logger = [] (const QString &msg)
        {
            cout << msg.toStdString() << endl;
        };

        Context context = {};
        context.logger = logger;

        if (enableAnalysisServer)
        {
            context.dataServer = std::make_unique<AnalysisDataServer>();
            context.dataServer->setLogger(logger);
            context.streamProcessor.attachModuleConsumer(context.dataServer.get());
        }

        context.streamProcessor.startup();

        if (enableAnalysisServer)
        {
            assert(context.dataServer->isListening());
            cout << "waiting for client to connect..." << endl;

            while (context.dataServer->getNumberOfClients() == 0)
            {
                app.processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
            }

        }

        cout << "processing" << listfiles.size() << "listfiles";

        for (const auto &listfileFilename: listfiles)
        {
            auto openResult = open_listfile(listfileFilename);

            if (!openResult.listfile)
            {
                failedToOpen.push_back(listfileFilename);
                continue;
            }

            std::unique_ptr<VMEConfig> vmeConfig(read_config_from_listfile(openResult.listfile.get()));
            std::unique_ptr<Analysis> analysis = std::make_unique<Analysis>();

            if (!analysisFilename.isEmpty())
            {
                load_analysis_config(analysisFilename, analysis.get(), vmeConfig.get());
            }

            context.runInfo.runId = QFileInfo(listfileFilename).completeBaseName();
            context.runInfo.isReplay = true;
            context.analysis = analysis.get();
            context.vmeConfig = vmeConfig.get();
            context.listfileVersion = openResult.listfile->getFileVersion();

            auto indexMapping = vme_analysis_common::build_id_to_index_mapping(context.vmeConfig);
            context.analysis->beginRun(context.runInfo, indexMapping);


            cout << "processing listfile" << listfileFilename.toStdString() << "...";

            process_listfile(context, openResult.listfile.get());

            cout << "process_listfile() returned";

            auto counters = context.streamProcessor.getCounters();

            auto elapsed_ms = counters.startTime.msecsTo(counters.stopTime);
            double dt_s = elapsed_ms / 1000.0;

            double bytesPerSecond = counters.bytesProcessed / dt_s;

            std::cout << "startTime:" << counters.startTime.toString().toStdString() << endl;
            std::cout << "endTime:" << counters.stopTime.toString().toStdString() << endl;
            std::cout << "dt_s:" << dt_s << endl;
            std::cout << "bytesProcessed:" << format_number(counters.bytesProcessed,
                                                           "B", UnitScaling::Binary).toStdString() << endl;

            std::cout << "bytesPerSecond:" << format_number(bytesPerSecond,
                                                           "B/s", UnitScaling::Binary).toStdString() << endl;

            for (u32 ei = 0; ei < counters.eventCounters.size(); ei++)
            {
                if (counters.eventCounters[ei] > 0.0)
                {
                    cout << (QString("ei=%1, count=%2, rate=%3")
                             .arg(ei)
                             //.arg(counters.eventCounters[ei])
                             .arg(format_number(counters.eventCounters[ei], "", UnitScaling::Decimal))
                             .arg(format_number(counters.eventCounters[ei] / dt_s, "Hz", UnitScaling::Decimal))
                            ).toStdString();
                }
            }

            for (u32 ei = 0; ei < counters.moduleCounters.size(); ei++)
            {
                for (u32 mi = 0; mi < counters.moduleCounters[ei].size(); mi++)
                {
                    if (counters.moduleCounters[ei][mi] > 0.0)
                    {
                        auto msg = (QString("ei=%1, mi=%2 count=%3 (%5), rate=%4")
                                    .arg(ei).arg(mi)
                                    .arg(format_number(counters.moduleCounters[ei][mi], "", UnitScaling::Decimal))
                                    .arg(format_number(counters.moduleCounters[ei][mi] / dt_s, "Hz", UnitScaling::Decimal))
                                    .arg(counters.moduleCounters[ei][mi])
                                   );
                        cout << msg.toStdString() << endl;
                    }
                }
            }

            if (writeSession)
            {
                QString sessionOutFilename = context.runInfo.runId + ".msess";
                cout << "saving session to" << sessionOutFilename.toStdString() << " ...";
                auto result = save_analysis_session(sessionOutFilename, analysis.get());
                if (!result.first)
                {
                    throw result.second;
                }
            }

            filesProcessed++;
        }

        cout << "processed" << filesProcessed << "listfiles";

        if (failedToOpen.size())
        {
            cout << "failed to open" << failedToOpen.size() << "files";
        }
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
