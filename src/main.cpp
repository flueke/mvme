/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "git_sha1.h"
#include "mvme.h"
#include "mvme_options.h"
#include "mvme_session.h"
#include <mesytec-mvlc/util/logging.h>

#include <QApplication>
#include <QMessageBox>
#include <QSplashScreen>
#include <QTimer>
#include <iostream>

#include <spdlog/sinks/basic_file_sink.h>

#ifdef MVME_ENABLE_PROMETHEUS
#include "mvme_prometheus.h"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/window_icon.png"));

    auto args = app.arguments();
    MVMEOptions options = {};

    if (args.contains("--help"))
    {
        std::cout << "mvme command line options:" << std::endl;
        std::cout << "  --offline       Start in offline mode. Connecting to a" << std::endl
                  << "                  VME controller will not be possible. Useful for replays."
                  << std::endl;
        return 0;
    }

    if (args.contains("--version"))
    {
        std::cout << "mvme - mesytec VME DAQ - version " << mvme_git_version() << "\n";
        return 0;
    }

    if (args.contains("--offline"))
        options.offlineMode = true;

    mvme_init(); // QSettings pick up the correct settings after this line.

    if (args.contains("--debug"))
        spdlog::set_level(spdlog::level::debug);

    if (args.contains("--trace"))
        spdlog::set_level(spdlog::level::trace);

    if (args.contains("--debug-mvlc"))
    {
        mesytec::mvlc::get_logger("mvlc_apiv2")->set_level(spdlog::level::debug);
        mesytec::mvlc::get_logger("mvlc")->set_level(spdlog::level::debug);
        mesytec::mvlc::get_logger("cmd_pipe_reader")->set_level(spdlog::level::debug);
    }

    // debug code here: write output of some mvlc loggers to mvlc_trace.log
    if (args.contains("--trace-mvlc"))
    {
        mesytec::mvlc::get_logger("mvlc_apiv2")->set_level(spdlog::level::trace);
        mesytec::mvlc::get_logger("mvlc")->set_level(spdlog::level::trace);
        mesytec::mvlc::get_logger("cmd_pipe_reader")->set_level(spdlog::level::trace);
        auto fs = std::make_shared<spdlog::sinks::basic_file_sink_mt>("mvlc_trace.log");
        mesytec::mvlc::get_logger("mvlc_apiv2")->sinks().push_back(fs);
        mesytec::mvlc::get_logger("mvlc")->sinks().push_back(fs);
        mesytec::mvlc::get_logger("cmd_pipe_reader")->sinks().push_back(fs);
    }

#ifdef MVME_ENABLE_PROMETHEUS
    // This variable is here to keep the prom context alive in main! This is to
    // avoid a hang when the internal civetweb instance is destroyed from within
    // a DLL (https://github.com/civetweb/civetweb/issues/264). By having this
    // variable on the stack the destructor is called from mvme.exe, not from
    // within libmvme.dll.
    std::shared_ptr<mesytec::mvme::PrometheusContext> prom;
    try
    {
        auto promBindAddress = QSettings().value("PrometheusBindAddress", "0.0.0.0:13802").toString().toStdString();
        prom = std::make_shared<mesytec::mvme::PrometheusContext>();
        prom->start(promBindAddress);
        std::cout << "Prometheus server listening on port " << prom->exposer()->GetListeningPorts().front() << "\n";
        mesytec::mvme::set_prometheus_instance(prom); // Register the prom object globally.
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error creating prometheus context: " << e.what() << ". Prometheus metrics not available!\n";
    }
#endif

#ifdef QT_NO_DEBUG
    QSplashScreen splash(QPixmap(":/splash-screen.png"),
                         Qt::CustomizeWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    auto font = splash.font();
    font.setPixelSize(22);
    splash.setFont(font);
    splash.showMessage(QSL(
            "mvme - VME Data Acquisition\n"
            "© 2015-2023 mesytec GmbH & Co. KG"
            ), Qt::AlignHCenter);
    splash.show();

    const int splashMaxTime = 3000;
    QTimer splashTimer;
    splashTimer.setInterval(splashMaxTime);
    splashTimer.setSingleShot(true);
    splashTimer.start();
    QObject::connect(&splashTimer, &QTimer::timeout, &splash, &QWidget::close);
#endif

    MVMEMainWindow w(options);
    w.show();
    w.restoreSettings();

#ifdef QT_NO_DEBUG
    splash.raise();
#endif

    // Code to run on entering the event loop for the first time.

    QTimer::singleShot(0, [&w]() {
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
    });

    int ret = app.exec();

    mvme_shutdown();

#ifdef MVME_ENABLE_PROMETHEUS
    if (prom) prom->stop();
#endif

    return ret;
}
