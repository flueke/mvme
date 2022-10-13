#include <QApplication>
#include <QMainWindow>
#include <QStandardPaths>
#include <QFileDialog>

#include "mvme_session.h"
#include "util/qt_logview.h"
#include "histo1d_widget.h"

static const QString HistoFileFilter = "*.txt";
using Logger = std::function<void (const QString &)>;

void null_logger(const QString &);

Histo1DPtr read_indian_style_histo_from_file(const QString &filepath, Logger logger = null_logger)
{
    QFile f(filepath);

    if (!f.open(QIODevice::ReadOnly))
        return {};

    QTextStream fs(&f);

    std::vector<std::pair<s64, double>> binsAndValues;
    s64 maxBinNumber = 0;

    while (!fs.atEnd())
    {
        s64 bin = 0;
        double value = 0.0;
        fs >> bin >> value;
        if (fs.status() == QTextStream::Ok)
        {
            binsAndValues.emplace_back(std::make_pair(bin, value));
            maxBinNumber = std::max(maxBinNumber, bin);
        }
    }

    s64 histoBins = 1;

    while (histoBins < maxBinNumber)
        histoBins <<= 1;

    auto fn = QFileInfo(filepath).fileName();

    logger(QSL("file '%1': entries=%2, maxBinNumber=%3 -> histoBins=%4")
           .arg(fn).arg(binsAndValues.size()).arg(maxBinNumber).arg(histoBins));

    auto histo = std::make_shared<Histo1D>(histoBins, 0, histoBins);
    histo->setObjectName(fn);
    histo->setTitle(fn);

    for (auto & [bin, val]: binsAndValues)
        histo->setBinContent(bin, val);

    return histo;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/window_icon.png"));
    mvme_init("mvme_histo_viewer");

    auto logView = make_logview().release();
    auto mainWin = new QMainWindow;
    mainWin->setAttribute(Qt::WA_DeleteOnClose);
    mainWin->setCentralWidget(logView);
    auto tb = mainWin->addToolBar("toolbar");

    auto logger = [=] (const QString &str)
    {
        logView->appendPlainText(str);
    };

    auto open_histo_file = [=]
    {
        auto path = QSettings().value("LastHistoDir").toString();

        if (path.isEmpty())
            path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

        auto fn = QFileDialog::getOpenFileName(mainWin, "Open Histogram Text File", path, HistoFileFilter);

        if (fn.isEmpty())
            return;

        auto histo = read_indian_style_histo_from_file(fn, logger);

        if (!histo)
            return;

        QSettings().setValue("LastHistoDir", QFileInfo(fn).dir().path());

        //auto histo = std::make_shared<Histo1D>(8192, 0, 8192);
        auto hw = new Histo1DWidget(histo);
        hw->setAttribute(Qt::WA_DeleteOnClose);
        hw->show();
    };

    tb->addAction("Open Indian-Style Histogram", open_histo_file);



    mainWin->resize(800, 400);
    mainWin->show();
    int ret = app.exec();
    mvme_shutdown();
    return ret;
}