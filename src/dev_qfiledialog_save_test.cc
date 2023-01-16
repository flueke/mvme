#include <QApplication>
#include <QFileDialog>
#include <QDebug>

static const QString fileFilters = "Foobar Files (*.foobar);; All Files (*.*)";
static const QString fileExtension = ".foobar";

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Using the blocking getSaveFilenAme()
    {
        auto filename = QFileDialog::getSaveFileName(
            nullptr,
            "Save a .foobar file (getSaveFileName())",
            {},             // startDir
            fileFilters,
            nullptr,        // selectedFilter
            {}              // QFileDialog::Options
            );

        qDebug() << "getSaveFileName: filename =" << filename;
    }

    // QFileDialog with setDefaultSuffix()
    {
        QFileDialog fd(
            nullptr,
            "Save a .foobar file (QFileDialog instance)",
            {},             // startDir
            fileFilters);
        fd.setDefaultSuffix(fileExtension);
        fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

        if (fd.exec() == QFileDialog::Accepted)
        {
            auto selectedFiles = fd.selectedFiles();
            qDebug() << "selectedFiles =" << selectedFiles;
        }
    }

    int ret = app.exec();
    return ret;
}