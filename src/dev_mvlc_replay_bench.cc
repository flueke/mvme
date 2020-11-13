#include <QApplication>
#include <QTimer>

#include "mvme_session.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "daqcontrol.h"

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
    QApplication app(argc, argv); // FIXME: fix stuff so that a QCoreApplication can be used here

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

    mvme_shutdown();

    return ret;
}
