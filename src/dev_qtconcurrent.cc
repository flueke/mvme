#include <QDebug>
#include <QCoreApplication>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QUrl>
#include <vector>

QString to_lower(const QUrl &str)
{
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