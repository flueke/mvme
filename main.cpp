#include <QApplication>
#include "mvme.h"
#include "util.h"
#include <QDebug>

int main(int argc, char *argv[])
{

    QString testInput =
        "0x1234 10\n"
        "0x1235 11\r"
        "0x1236 12 a comment\n"
        "0x1237 13 # another comment 0xdead\n"
        "0x1238 14 // and yet another one\n"
        "42 0x42\n\r"
        "ending with a comment and no newline 42";

    QTextStream stream(&testInput);

    auto numbers = parseStackFile(stream);

    qDebug() << numbers;
    qDebug() << numbers.size();



    QApplication a(argc, argv);

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion("0.2.0");


    mvme w;
    w.show();
    
    return a.exec();
}
