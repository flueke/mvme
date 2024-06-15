#include <QApplication>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <thread>

#include "multi_crate.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;
using namespace mesytec::mvme::multi_crate;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    spdlog::set_level(spdlog::level::info);

    #if 0
    {
        auto task = []
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        };

        auto watcher = new QFutureWatcher<void>();
        QObject::connect(watcher, &QFutureWatcher<void>::finished, [watcher]
        {
            spdlog::info("task finished");
            watcher->waitForFinished();
            watcher->deleteLater();
            qApp->quit();
        });
        QObject::connect(watcher, &QObject::destroyed, []
        {
            spdlog::info("watcher destroyed");
        });
        auto future = QtConcurrent::run(task);
        watcher->setFuture(future);
    }
    #else
    {
        auto task = []
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            //return 42;
        };

        auto watcher = make_watcher<void>([](const auto &)
        {
            spdlog::info("task finished");
            qApp->quit();
        });
        watcher->setFuture(QtConcurrent::run(task));
    }

    {
        auto task = []
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return 42;
        };

        auto watcher = make_watcher<int>([](const auto &future)
        {
            spdlog::info("task finished: {}", future.result());
            qApp->quit();
        });
        watcher->setFuture(QtConcurrent::run(task));
    }
    #endif

    return app.exec();
}
