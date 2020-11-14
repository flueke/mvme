#include <QApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include "analysis/a2_adapter.h"
#include "mvme_session.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "daqcontrol.h"
#include "util/qt_metaobject.h"

/*
open listfile
open analysis (from command line or from the listfile)
build and wire everything up
start replay
at end of replay record data:
  - run duration
  - data rates
  - data size
  - per event and module hits
  - per histogram: counts and statistics, maybe also the non-zero bin values
  - workspace path
  - input listfile name
  - input analysis filename and if it came from the listfile
*/

namespace
{
QJsonArray collect_h1d_stats(const MVMEContext &mvmeContext)
{
    QJsonArray sinksArray;

    auto analysis = mvmeContext.getAnalysis();
    auto a2aState = analysis->getA2AdapterState();

    for (const auto &a1_op: a2aState->operatorMap.hash.keys())
    {
        if (auto sink = qobject_cast<analysis::Histo1DSink *>(a1_op))
        {
            auto dir = analysis->getParentDirectory(sink->shared_from_this());
            auto data = analysis::get_runtime_h1dsink_data(*a2aState, sink);

            QJsonObject sinkJ;
            sinkJ["id"] = sink->getId().toString();
            sinkJ["name"] = sink->objectName();
            sinkJ["userlevel"] = sink->getUserLevel();
            sinkJ["className"] = getClassName(a1_op);

            QJsonArray histosArray;

            for (auto hi=0; hi<data->histos.size; ++hi)
            {
                const auto &a2_h1d = data->histos[hi];

                QJsonObject statsJ;
                statsJ["entryCount(a2)"] = a2_h1d.entryCount;
                statsJ["underflow"] = a2_h1d.underflow ? *a2_h1d.underflow : 0.0;
                statsJ["overflow"] = a2_h1d.overflow ? *a2_h1d.overflow : 0.0;

                if (const auto a1_h1d = sink->getHisto(hi))
                {
                    auto stats = a1_h1d->calcStatistics();

                    statsJ["maxBin"] = static_cast<qint64>(stats.maxBin);
                    statsJ["maxValue"] = stats.maxValue;
                    statsJ["mean"] = stats.mean;
                    statsJ["sigma"] = stats.sigma;
                    statsJ["entryCount(a1)"] = stats.entryCount;
                    statsJ["fwhm"] = stats.fwhm;
                    statsJ["fwhmCenter"] = stats.fwhmCenter;
                }

                histosArray.append(statsJ);
            }

            sinkJ["histoStats"] = histosArray;
            sinksArray.append(sinkJ);
        }
    }

    return sinksArray;
}

QJsonArray collect_h2d_stats(const MVMEContext &mvmeContext)
{
    QJsonArray sinksArray;

    auto analysis = mvmeContext.getAnalysis();
    auto a2aState = analysis->getA2AdapterState();

    for (const auto &a1_op: a2aState->operatorMap.hash.keys())
    {
        if (auto sink = qobject_cast<analysis::Histo2DSink *>(a1_op))
        {
            auto dir = analysis->getParentDirectory(sink->shared_from_this());
            auto data = analysis::get_runtime_h2dsink_data(*a2aState, sink);

            QJsonObject sinkJ;
            sinkJ["id"] = sink->getId().toString();
            sinkJ["name"] = sink->objectName();
            sinkJ["userlevel"] = sink->getUserLevel();
            sinkJ["className"] = getClassName(a1_op);

            // TODO: calculate stats and store in sinkJ["stats"]
            if (const auto a1_h2d = sink->getHisto())
            {
                auto stats = a1_h2d->calcGlobalStatistics();
            }


            sinksArray.append(sinkJ);
        }
    }

    return sinksArray;
}
} // end anon namespace

static QTextStream qout(stdout);

int main(int argc, char *argv[])
{
    // FIXME: fix stuff so that a QCoreApplication can be used here
    QApplication app(argc, argv);

    auto args = app.arguments();

    if (args.size() < 2)
    {
        qout << "Usage: " << args[0] << " <listfile> [<analysis>]" << endl;
        return 1;
    }

    auto listfileFilename = args[1];
    auto analysisFilename = args.size() > 2 ? args[2] : QString();

    mvme_init(args[0]);

    MVMEContext mvmeContext;

    auto& replayHandle = context_open_listfile(
        &mvmeContext, listfileFilename,
        analysisFilename.isEmpty() ? OpenListfileFlags::LoadAnalysis : 0);

    if (!analysisFilename.isEmpty())
    {
        // FIXME: this calls into gui_read_json_file() which uses QMessageBox
        // for error reporting
        if (!mvmeContext.loadAnalysisConfig(analysisFilename))
            return 1;
    }

    DAQControl daqControl(&mvmeContext);
    daqControl.startDAQ();

    QObject::connect(&mvmeContext, &MVMEContext::mvmeStreamWorkerStateChanged,
            [&app] (MVMEStreamWorkerState state)
            {
                if (state == MVMEStreamWorkerState::Idle)
                    app.quit();
            });

    int ret = app.exec();


    qout << ">>>>> Begin LogBuffer:" << endl;
    for (const auto &line: mvmeContext.getLogBuffer())
        qout << line << endl;
    qout << "<<<<< End LogBuffer:" << endl;

    auto analysis = mvmeContext.getAnalysis();
    auto a2aState = analysis->getA2AdapterState();

    for (const auto &a1_op: a2aState->operatorMap.hash.keys())
    {
        if (auto sink = qobject_cast<analysis::Histo1DSink *>(a1_op))
        {
            auto dir = analysis->getParentDirectory(sink->shared_from_this());
            auto data = analysis::get_runtime_h1dsink_data(*a2aState, sink);
            qout << sink->getUserLevel() << " ";
            if (dir)
                qout << dir->objectName() << " ";
            qout << sink->objectName() << " " << a1_op << ":" << endl;
            for (auto hi=0; hi<data->histos.size; ++hi)
            {
                const auto &a2_h1d = data->histos[hi];

                qout << "  " << a2_h1d.entryCount;
                if (a2_h1d.underflow)
                    qout << ", underflow=" << *a2_h1d.underflow;
                if (a2_h1d.overflow)
                    qout << ", overflow=" << *a2_h1d.overflow;
                if (const auto a1_h1d = sink->getHisto(hi))
                {
                    auto stats = a1_h1d->calcStatistics();
                    qout << ", maxBin=" << stats.maxBin
                        << ", maxValue=" << stats.maxValue
                        << ", mean=" << stats.mean
                        << ", sigma=" << stats.sigma
                        << ", entryCount=" << stats.entryCount
                        << ", fwhm=" << stats.fwhm
                        << ", fwhmCenter=" << stats.fwhmCenter
                        ;
                }
                qout << endl;
            }
        }
    }

    if (auto streamWorker = mvmeContext.getMVMEStreamWorker())
    {
        const auto counters = streamWorker->getCounters();

        auto duration_s = counters.startTime.msecsTo(counters.stopTime) / 1000.0;
        auto data_mb = counters.bytesProcessed / Megabytes(1);
        auto rate_mbs = data_mb / duration_s;
        qout << "elapsed=" << duration_s << " s"
            << ", data=" << data_mb << " MB"
            << ", rate=" << rate_mbs << " MB/s" << endl;
    }

    QJsonObject reportJ;
    reportJ["h1dSinks"] = collect_h1d_stats(mvmeContext);
    reportJ["h2dSinks"] = collect_h2d_stats(mvmeContext);

    QJsonDocument doc(reportJ);
    qout << doc.toJson() << endl;

    mvme_shutdown();
    return ret;
}
