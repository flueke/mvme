#include <cassert>
#include <chrono>
#include <getopt.h>
#include <iostream>
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include "mvme_listfile.h"

using std::cout;
using std::cerr;
using std::endl;

namespace
{
static const size_t ReadBufferSize = Megabytes(1);

struct Context
{
    using ClockType = std::chrono::high_resolution_clock;

    QTcpSocket *socket;
    QFile *outfile;
    u64 bytesRead = 0;
    u64 readCount = 0;
    ClockType::time_point startTime;
    ClockType::time_point endTime;
};

void receive_one_buffer(Context &context, const u32 size, DataBuffer &destBuffer)
{
    destBuffer.ensureCapacity(size);
    destBuffer.used = 0;

    while (destBuffer.used < size)
    {
        if (context.socket->bytesAvailable() <= 0 && !context.socket->waitForReadyRead())
        {
            throw (QString("waitForReadyRead (data) failed"));
        }

        // Note: read() returns 0 if no more data is available. could maybe also use that instead of testing for <= 0
        qint64 bytesReceived = context.socket->read(reinterpret_cast<char *>(destBuffer.asU8()), size - destBuffer.used);

        if (bytesReceived <= 0)
        {
            throw (QString("read data failed: %1")
                   .arg(context.socket->errorString()));
        }

        context.bytesRead += bytesReceived;
        context.readCount++;
    }
}

void receive_and_write_listfile(Context &context)
{
    DataBuffer mvmeBuffer(ReadBufferSize);
    bool done = false;

    while (true)
    {
        u32 bufferSize = 0;

        while (context.socket->bytesAvailable() < static_cast<qint64>(sizeof(bufferSize)))
        {
            if (!context.socket->waitForReadyRead())
            {
                //throw (QString("waitForReadyRead (bufferSize) failed"));
                done = true;
                break;
            }
        }

        if (done)
        {
            break;
        }

        assert(context.socket->bytesAvailable() >= static_cast<qint64>(sizeof(bufferSize)));

        if (context.socket->read(reinterpret_cast<char *>(&bufferSize),
                                 sizeof(bufferSize)) != static_cast<qint64>(sizeof(bufferSize)))
        {
            throw (QString("read bufferSize failed: %1")
                   .arg(context.socket->errorString()));
        }
        context.bytesRead += sizeof(bufferSize);
        context.readCount++;

        bufferSize = qFromBigEndian(bufferSize);

        //qDebug() << __PRETTY_FUNCTION__ << "incoming buffer size: " << bufferSize;

        receive_one_buffer(context, bufferSize, mvmeBuffer);

        if (context.outfile->write(
                reinterpret_cast<const char *>(mvmeBuffer.data),
                mvmeBuffer.used) != static_cast<qint64>(mvmeBuffer.used))
        {
            throw (QString("error writing to outfile %1: %2")
                   .arg(context.outfile->fileName())
                   .arg(context.outfile->errorString()));
        }
    }
}

} // end anon namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString listfileFilename;
    QString listenHost;
    u16 listenPort = 0;
    bool showHelp = false;

    while (true)
    {
        static struct option long_options[] = {
            { "listfile",               required_argument,      nullptr,    0 },
            { "host",                   required_argument,      nullptr,    0 },
            { "port",                   required_argument,      nullptr,    0 },
            { "help",                   no_argument,            nullptr,    0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "listfile") { listfileFilename = QString(optarg); }
        if (opt_name == "host") { listenHost = QString(optarg); }
        if (opt_name == "port") { listenPort = QString(optarg).toUInt(); }
        if (opt_name == "help") { showHelp = true; }
    }

    if (showHelp
        || listfileFilename.isEmpty()
        || listenHost.isEmpty()
        || listenPort == 0)
    {
        cout << "Usage: " << argv[0] << " --listfile <filename> --host <listehost> --port <listport>" << endl;
        cout << "The program will listen on the given host and port and write received data to the given listfile." << endl;
        cout << "Example: " << argv[0] << " --listfile myfile.mvmelst --host example.com --port 1234" << endl;

        return showHelp ? 0 : 1;
    }

    try
    {
        QTcpServer server;
        server.listen(QHostAddress(listenHost), listenPort);

        if (!server.waitForNewConnection(-1))
        {
            throw QString("Error waiting for incoming connection");
        }

        QFile outfile(listfileFilename);
        if (!outfile.open(QIODevice::WriteOnly))
        {
            throw (QString("Error opening output listfile %1 for writing: %2")
                   .arg(listfileFilename)
                   .arg(outfile.errorString()));
        }

        Context context;
        context.socket = server.nextPendingConnection(); // note: socket is a qobject child of server
        context.outfile = &outfile;

        context.startTime = Context::ClockType::now();

        receive_and_write_listfile(context);

        context.endTime = Context::ClockType::now();

        std::chrono::duration<double> secondsElapsed = context.endTime - context.startTime;
        double mbRead = context.bytesRead / Megabytes(1);
        double mbPerSecond = mbRead / secondsElapsed.count();

        cout << "Number of socket reads: " << context.readCount << endl;
        cout << "MB read: " << mbRead << endl;
        cout << "Rate: " << mbPerSecond << " MB/s" << endl;
        cout << "Elapsed seconds: " << secondsElapsed.count() << endl;
    }
    catch (const QString &e)
    {
        qDebug() << e << endl;
        return 1;
    }

    return 0;
}
