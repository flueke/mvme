#include <cassert>
#include <getopt.h>
#include <iostream>
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QTcpSocket>
#include <QtEndian>

#include "mvme_listfile.h"

using std::cout;
using std::cerr;
using std::endl;

namespace
{

static u64 g_socketWriteCount = 0;

void send_data(QTcpSocket &socket, const u8 *data, size_t size)
{
    auto bytesWritten = socket.write(
        reinterpret_cast<const char *>(data),
        static_cast<qint64>(size));

    ++g_socketWriteCount;

    if (bytesWritten != (qint64) size)
    {
        throw QString("socket.write failed: %1").arg(socket.errorString());
    }

    if (!socket.waitForBytesWritten())
    {
        throw QString("socket.waitForBytesWritten failed: %1").arg(socket.errorString());
    }
}

void send_data_with_size_prefix(QTcpSocket &socket, const u8 *data, size_t size)
{
    assert(size <= std::numeric_limits<u32>::max());

    u32 sizeBigEndian = qToBigEndian(static_cast<u32>(size));

    send_data(socket, reinterpret_cast<const u8 *>(&sizeBigEndian), sizeof(sizeBigEndian));
    send_data(socket, data, size);
}

static const size_t ReadBufferSize = Megabytes(1);
//static const size_t ReadBufferSize = Kilobytes(128);

struct Context
{
    QTcpSocket socket;
};

void send_mvme_buffer(Context &context, DataBuffer &buffer)
{
    send_data_with_size_prefix(context.socket, buffer.data, buffer.used);
}

void process_listfile(Context &context, ListFile *listfile)
{
    auto preamble = listfile->getPreambleBuffer();

    if (!preamble.isEmpty())
    {
        send_data_with_size_prefix(context.socket, preamble.data(), preamble.size());
    }


    DataBuffer sectionBuffer(ReadBufferSize);

    while (true)
    {
        sectionBuffer.used = 0;

        s32 numSections = listfile->readSectionsIntoBuffer(&sectionBuffer);

        if (numSections <= 0)
            break;

        send_mvme_buffer(context, sectionBuffer);
    }
}

} // end anon namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString listfileFilename;
    QString destHost;
    u16 destPort = 0;
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
        if (opt_name == "host") { destHost = QString(optarg); }
        if (opt_name == "port") { destPort = QString(optarg).toUInt(); }
        if (opt_name == "help") { showHelp = true; }
    }

    if (showHelp
        || listfileFilename.isEmpty()
        || destHost.isEmpty()
        || destPort == 0)
    {
        cout << "Usage: " << argv[0] << " --listfile <filename> --host <desthost> --port <destport>" << endl;
        cout << "The program will connect to given host and port and send the given listfile over the connected socket." << endl;
        cout << "Example: " << argv[0] << " --listfile myfile.mvmelst --host example.com --port 1234" << endl;

        return showHelp ? 0 : 1;
    }

    try
    {
        auto openResult = open_listfile(listfileFilename);

        if (!openResult.listfile)
            return 1;

        Context context;

        context.socket.connectToHost(destHost, destPort);

        if (!context.socket.waitForConnected())
        {
            throw (QString("Error connecting to %1:%2: %3")
                   .arg(destHost)
                   .arg(destPort)
                   .arg(context.socket.errorString()));
        }


        process_listfile(context, openResult.listfile.get());
    }
    catch (const QString &e)
    {
        qDebug() << e << endl;
        return 1;
    }

    cout << "Number of socket writes: " << g_socketWriteCount << endl;

    return 0;
}
