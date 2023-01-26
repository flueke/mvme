#include "util/qt_gui_io.h"

#include <QFile>
#include <QMessageBox>
#include <QTextStream>

#include "util/qt_str.h"

namespace mesytec::mvme::util
{

QString gui_read_file_into_string(const QString &filename)
{

    QFile infile(filename);

    if (!infile.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(
            0, QSL("File read error"),
            QSL("Error opening file '%1' for reading: %2")
                .arg(filename).arg(infile.errorString()));
        return {};
    }

    QTextStream instream(&infile);
    return instream.readAll();
}

}