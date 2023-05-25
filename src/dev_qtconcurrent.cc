#include <QDebug>
#include <QCoreApplication>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QUrl>
#include <algorithm>
#include <execution>
#include <vector>

#if 0 // crash demo
QString to_lower(const QUrl &str)
{
    // Crash here because QtConcurrent::mapped() takes a reference to the
    // original input vector which was allocated on the stack and at this point
    // has been destroyed.
    qDebug() << __PRETTY_FUNCTION__ << str;
    return str.path().toLower();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto wrap = []
    {
        std::vector<QUrl> items = { QUrl("Keyboards"), QUrl("Mice"), QUrl("Cups") };
        qDebug() << __PRETTY_FUNCTION__ << items;

        auto f = QtConcurrent::mapped(items, to_lower);
        return f;
    };

    auto f = wrap();


    f.waitForFinished();
    qDebug() << f.resultCount();;

    return 1;
}
#endif // end crash demo

int main(int argc, char *argv[])
{
    for (int i=0; i<1000; ++i)
    {
        std::vector<QUrl> items = {QUrl("Keyboards"), QUrl("Mice"), QUrl("Cups")};
        std::vector<QString> results;
        results.resize(items.size());

        std::transform(
            std::execution::par_unseq,
            std::cbegin(items), std::cend(items),
            std::begin(results),
            [] (const QUrl &url) { return url.path().toLower(); });

        qDebug() << "results =" << results;
    }
}
