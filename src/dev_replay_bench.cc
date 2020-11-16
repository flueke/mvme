#include <QApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <qnamespace.h>

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

            QJsonObject histoJ;

            histoJ["entryCount(a2)"] = data->histo.entryCount;
            histoJ["underflow"] = data->histo.underflow;
            histoJ["overflow"] = data->histo.overflow;

            if (const auto a1_h2d = sink->getHisto())
            {
                auto stats = a1_h2d->calcGlobalStatistics();
                histoJ["maxBinX"] = static_cast<qint64>(stats.maxBinX);
                histoJ["maxBinY"] = static_cast<qint64>(stats.maxBinY);
                histoJ["maxX"] = stats.maxX;
                histoJ["maxY"] = stats.maxY;
                histoJ["maxZ"] = stats.maxZ;
                histoJ["entryCount(a1)"] = stats.entryCount;
            }

            sinkJ["histo"] = histoJ;
            sinksArray.append(sinkJ);
        }
    }

    return sinksArray;
}

QJsonObject collect_streamproc_counters(const MVMEStreamProcessorCounters &counters)
{
    QJsonObject streamProcCounters;

    auto elapsed_s = counters.startTime.msecsTo(counters.stopTime) / 1000.0;
    auto data_mb = static_cast<double>(counters.bytesProcessed) / Megabytes(1);
    auto rate_mbs = data_mb / elapsed_s;

    streamProcCounters["startTime"] = counters.startTime.toString(Qt::ISODate);
    streamProcCounters["stopTime"] = counters.stopTime.toString(Qt::ISODate);
    streamProcCounters["bytesProcessed"] = static_cast<qint64>(counters.bytesProcessed);
    streamProcCounters["buffersProcessed"] = static_cast<qint64>(counters.buffersProcessed);
    streamProcCounters["buffersWithErrors"] = static_cast<qint64>(counters.buffersWithErrors);
    streamProcCounters["totalEvents"] = static_cast<qint64>(counters.eventSections);
    streamProcCounters["invalidEventIndices"] = static_cast<qint64>(counters.invalidEventIndices);
    streamProcCounters["elapsed_s"] = elapsed_s;
    streamProcCounters["data_mb"] = data_mb;
    streamProcCounters["rate_mbs"] = rate_mbs;

    return streamProcCounters;
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

    /*auto& replayHandle =*/ context_open_listfile(
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


    QJsonObject reportJ;
    reportJ["H1DSinks"] = collect_h1d_stats(mvmeContext);
    reportJ["H2DSinks"] = collect_h2d_stats(mvmeContext);

    if (auto streamWorker = mvmeContext.getMVMEStreamWorker())
    {
         auto countersJ = collect_streamproc_counters(streamWorker->getCounters());
         reportJ["StreamProcessorCounters"] = countersJ;

         qout << "elapsed=" << countersJ["elapsed_s"].toDouble() << " s"
             << ", data=" << countersJ["data_mb"].toDouble() << " MB"
             << ", rate=" << countersJ["rate_mbs"].toDouble() << " MB/s" << endl;
    }


    QJsonDocument doc(reportJ);
    qout << doc.toJson() << endl;

    mvme_shutdown();
    return ret;
}
