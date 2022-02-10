#include <QApplication>
#include <QSplashScreen>
#include <QTimer>
#include <spdlog/spdlog.h>

#include "multi_crate_mainwindow.h"
#include "mvme_session.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    mvme_init("mvme_multi_crate");
    app.setWindowIcon(QIcon(":/window_icon.png"));

    auto args = app.arguments();

    if (args.contains("--debug"))
        spdlog::set_level(spdlog::level::debug);

    if (args.contains("--trace"))
        spdlog::set_level(spdlog::level::trace);

#ifdef QT_NO_DEBUG
    QSplashScreen splash(QPixmap(":/splash-screen.png"),
                         Qt::CustomizeWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    auto font = splash.font();
    font.setPixelSize(22);
    splash.setFont(font);
    splash.showMessage(QSL(
            "mvme - VME Data Acquisition\n"
            "© 2015-2020 mesytec GmbH & Co. KG"
            ), Qt::AlignHCenter);
    splash.show();

    const int splashMaxTime = 3000;
    QTimer splashTimer;
    splashTimer.setInterval(splashMaxTime);
    splashTimer.setSingleShot(true);
    splashTimer.start();
    QObject::connect(&splashTimer, &QTimer::timeout, &splash, &QWidget::close);
#endif

    // create and show the gui
    MultiCrateMainWindow w;
    w.show();

    {
        QSettings settings;
        w.restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
        w.restoreState(settings.value("mainWindowState").toByteArray());
    }

#ifdef QT_NO_DEBUG
    splash.raise();
#endif

    // Code to run on entering the event loop for the first time.

    QTimer::singleShot(0, [&w]() {
        /*
        QSettings settings;

        // Open the last workspace or create a new one.
        if (settings.contains(QSL("LastWorkspaceDirectory")))
        {
            try
            {
                w.getContext()->openWorkspace(
                    settings.value(QSL("LastWorkspaceDirectory")).toString());

            } catch (const QString &e)
            {
                QMessageBox::warning(&w, QSL("Could not open workspace"),
                                     QString("Error opening last workspace: %1").arg(e));

                settings.remove(QSL("LastWorkspaceDirectory"));

                if (!w.createNewOrOpenExistingWorkspace())
                {
                    // canceled by user -> quit mvme
                    w.close();
                }
            }
        }
        else
        {
            if (!w.createNewOrOpenExistingWorkspace())
            {
                // canceled by user -> quit mvme
                w.close();
            }
        }
        */
    });

    int ret = app.exec();

    mvme_shutdown();

    return ret;
}
