#include <QApplication>
#include <QTimer>
#include <QJsonDocument>

#include "analysis_bench.h"
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

    int ret = 0;

    try
    {
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

        ret = app.exec();


        qout << ">>>>> Begin LogBuffer:" << endl;
        for (const auto &line: mvmeContext.getLogBuffer())
            qout << line << endl;
        qout << "<<<<< End LogBuffer:" << endl;

        if (auto dropped = mvmeContext.getDAQStats().droppedBuffers)
            throw std::runtime_error(QSL("droppedBuffers=%1, expected 0!").arg(dropped).toStdString());

        auto reportJ = make_analysis_benchmark_info(mvmeContext);

        const auto &countersJ = reportJ["StreamProcessorCounters"].toObject();

        qout << "elapsed=" << countersJ["elapsed_s"].toDouble() << " s"
            << ", data=" << countersJ["data_mb"].toDouble() << " MB"
            << ", rate=" << countersJ["rate_mbs"].toDouble() << " MB/s" << endl;

        auto reportFilename = QSL("mvme_replay_bench-%1-%2.json")
            .arg(QFileInfo(listfileFilename).baseName())
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
            ;

        QFile reportOut(reportFilename);
        if (!reportOut.open(QIODevice::WriteOnly))
            throw std::runtime_error(QSL("cannot open output file %1").arg(reportFilename).toStdString());

        QJsonDocument doc(reportJ);

        if (reportOut.write(doc.toJson()) <= 0)
            throw std::runtime_error(QSL("write error: %1").arg(reportOut.errorString()).toStdString());
    }
    catch (const std::runtime_error &e)
    {
        qout << "Error: " << e.what() << endl;
        ret = 1;
    }

    mvme_shutdown();
    return ret;
}
