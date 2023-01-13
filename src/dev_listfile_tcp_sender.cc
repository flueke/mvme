/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <cassert>
#include <chrono>
#include <getopt.h>
#include <iostream>
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QTcpSocket>
#include <QtEndian>

#include "mvme_listfile_utils.h"
#include "listfile_replay.h"

using std::cout;
using std::cerr;
using std::endl;

namespace
{

static const size_t ReadBufferSize = Megabytes(1);

struct Context
{
    using ClockType = std::chrono::high_resolution_clock;

    QTcpSocket socket;
    u64 bytesWritten = 0;
    u64 writeCount = 0;
    ClockType::time_point startTime;
    ClockType::time_point endTime;
};

void send_data(Context &context, const u8 *data, size_t size)
{
    assert(size > 0);

    auto bytesWritten = context.socket.write(
        reinterpret_cast<const char *>(data),
        static_cast<qint64>(size));

    if (bytesWritten != (qint64) size)
    {
        throw QString("socket.write failed: %1").arg(context.socket.errorString());
    }

    if (!context.socket.waitForBytesWritten())
    {
        throw QString("context.socket.waitForBytesWritten failed: %1").arg(context.socket.errorString());
    }

    context.bytesWritten += bytesWritten;
    context.writeCount++;
}

void send_data_with_size_prefix(Context &context, const u8 *data, size_t size)
{
    assert(size <= std::numeric_limits<u32>::max());
    assert(size > 0);

    u32 sizeBigEndian = qToBigEndian(static_cast<u32>(size));

    send_data(context, reinterpret_cast<const u8 *>(&sizeBigEndian), sizeof(sizeBigEndian));
    send_data(context, data, size);
}

void send_mvme_buffer(Context &context, DataBuffer &buffer)
{
    send_data_with_size_prefix(context, buffer.data, buffer.used);
}

void process_listfile(Context &context, ListfileReplayHandle &input)
{
    assert(input.listfile);
    assert(input.format == ListfileBufferFormat::MVMELST);

    auto listfile = std::make_unique<ListFile>(input.listfile.get());

    auto preamble = listfile->getPreambleBuffer();

    if (!preamble.isEmpty())
    {
        send_data_with_size_prefix(context, preamble.data(), preamble.size());
    }


    DataBuffer sectionBuffer(ReadBufferSize);

    while (true)
    {
        sectionBuffer.used = 0;

        s32 numSections = listfile->readSectionsIntoBuffer(&sectionBuffer);

        if (numSections <= 0)
            break;

        assert(sectionBuffer.used >= sizeof(u32)); // expecting at least the section header

        send_mvme_buffer(context, sectionBuffer);
    }

    /* Send a zero length prefix to indicate end of listfile, then wait for a reply. */
    //qDebug() << __PRETTY_FUNCTION__ << "sending zero length and waiting for final reply";

    u32 zero = 0u;
    send_data(context, reinterpret_cast<const u8 *>(&zero), sizeof(zero));

#if 0
    while (context.socket.bytesAvailable() < static_cast<qint64>(sizeof(zero)))
    {
        if (!context.socket.waitForReadyRead())
        {
            throw (QString("waitForReadyRead (final acknowledge) failed: %1")
                   .arg(context.socket.errorString()));
        }
    }

    qint64 bytesReceived = context.socket.read(reinterpret_cast<char *>(&zero), sizeof(zero));

    if (bytesReceived != static_cast<qint64>(sizeof(zero)))
    {
        throw (QString("final read failed: %1")
               .arg(context.socket.errorString()));
    }
#endif
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
        cout << "Example: " << argv[0] << " --listfile infile.mvmelst --host example.com --port 1234" << endl;
        cout << "The program will connect to given host and port and send the specified listfile over the connected socket." << endl;
        cout << "Listfile may be a flat mvmelst file or a zip produced by mvme." << endl;

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

        context.startTime = Context::ClockType::now();

        process_listfile(context, openResult);
        context.socket.disconnectFromHost();

        context.endTime = Context::ClockType::now();

        std::chrono::duration<double> secondsElapsed = context.endTime - context.startTime;
        double mbWritten = static_cast<double>(context.bytesWritten) / Megabytes(1);
        double mbPerSecond = mbWritten / secondsElapsed.count();

        cout << "Number of socket writes: " << context.writeCount << endl;
        cout << "MB written: " << mbWritten << ", " << context.bytesWritten << " bytes" << endl;
        cout << "Rate: " << mbPerSecond << " MB/s" << endl;
        cout << "Elapsed seconds: " << secondsElapsed.count() << endl;
    }
    catch (const QString &e)
    {
        qDebug() << e << '\n';
        return 1;
    }

    return 0;
}
