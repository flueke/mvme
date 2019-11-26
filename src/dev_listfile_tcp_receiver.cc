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

#include "mvme_listfile_utils.h"

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
    destBuffer.ensureFreeSpace(size);
    destBuffer.used = 0;

    while (destBuffer.used < size)
    {
        if (context.socket->bytesAvailable() <= 0 && !context.socket->waitForReadyRead())
        {
            throw (QString("waitForReadyRead (data) failed: %1")
                   .arg(context.socket->errorString()));
        }

        // Note: read() returns 0 if no more data is available. could maybe also use that instead of testing for <= 0
        qint64 bytesReceived = context.socket->read(
            reinterpret_cast<char *>(destBuffer.asU8()),
            size - destBuffer.used);

        if (bytesReceived <= 0)
        {
            throw (QString("read data failed: %1")
                   .arg(context.socket->errorString()));
        }

        destBuffer.used += bytesReceived;
        context.bytesRead += bytesReceived;
        context.readCount++;
    }
}

void receive_and_write_listfile(Context &context)
{
    DataBuffer mvmeBuffer(ReadBufferSize);

    while (true)
    {
        u32 bufferSize = 0;

        while (context.socket->bytesAvailable() < static_cast<qint64>(sizeof(bufferSize)))
        {
            if (!context.socket->waitForReadyRead())
            {
                throw (QString("waitForReadyRead (bufferSize) failed"));
            }
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

        if (bufferSize == 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "received 0 buffer size => breaking out of receive loop";
            break;
        }

        //qDebug() << __PRETTY_FUNCTION__ << "incoming buffer size: " << bufferSize;

        receive_one_buffer(context, bufferSize, mvmeBuffer);

        if (context.outfile->isOpen() && context.outfile->write(
                reinterpret_cast<const char *>(mvmeBuffer.data),
                mvmeBuffer.used) != static_cast<qint64>(mvmeBuffer.used))
        {
            throw (QString("error writing to outfile %1: %2")
                   .arg(context.outfile->fileName())
                   .arg(context.outfile->errorString()));
        }
    }

#if 0
    /* Write a single reply containing a zero word (32 bit). This tells the
     * sender that we've received all the data and it may close the connection. */

    u32 zero = 0u;

    auto bytesWritten = context.socket->write(
        reinterpret_cast<const char *>(&zero),
        sizeof(zero));

    if (bytesWritten != static_cast<qint64>(sizeof(zero)))
    {
        throw QString("final socket write failed: %1").arg(context.socket->errorString());
    }

    if (!context.socket->waitForBytesWritten())
    {
        throw QString("final socket waitForBytesWritten failed: %1").arg(context.socket->errorString());
    }
#endif
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
        || listenHost.isEmpty()
        || listenPort == 0)
    {
        cout << "Usage: " << argv[0] << " --listfile <filename> --host <listenhost> --port <listport>" << endl;
        cout << "Example: " << argv[0] << " --listfile outfile.mvmelst --host example.com --port 1234" << endl;
        cout << "The program will listen on the given host and port and write received data to the specified listfile filename." << endl;
        cout << "If no output listfile filename is given the data will only be received but not written to disk." << endl;

        return showHelp ? 0 : 1;
    }

    try
    {
        QTcpServer server;

        if (!server.listen(QHostAddress(listenHost), listenPort))
        {
            throw (QString("Error listening for incoming connection: %1")
                   .arg(server.errorString()));
        }

        cout << "Waiting for incoming connection..." << endl;

        if (!server.waitForNewConnection(-1))
        {
            throw QString("Error waiting for incoming connection");
        }

        QFile outfile;

        if (!listfileFilename.isEmpty())
        {
            outfile.setFileName(listfileFilename);

            if (!outfile.open(QIODevice::WriteOnly))
            {
                throw (QString("Error opening output listfile %1 for writing: %2")
                       .arg(listfileFilename)
                       .arg(outfile.errorString()));
            }
        }

        Context context;
        context.socket = server.nextPendingConnection(); // note: socket is a QObject child of server
        context.outfile = &outfile;

        cout << "New client connection from "
            << context.socket->peerAddress().toString().toStdString()
            << ":" << context.socket->peerPort()
            << endl;

        context.startTime = Context::ClockType::now();

        receive_and_write_listfile(context);

        cout << "closing output file" << endl;

        if (outfile.isOpen())
            outfile.close();

        cout << "output file closed" << endl;

        context.endTime = Context::ClockType::now();

        std::chrono::duration<double> secondsElapsed = context.endTime - context.startTime;
        double mbRead = static_cast<double>(context.bytesRead) / Megabytes(1);
        double mbPerSecond = mbRead / secondsElapsed.count();

        cout << "Number of socket reads: " << context.readCount << endl;
        cout << "MB read: " << mbRead << ", " << context.bytesRead << " bytes" << endl;
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
