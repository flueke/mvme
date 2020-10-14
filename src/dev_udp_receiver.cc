/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "util.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QUdpSocket>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc <= 1)
    {
        qDebug().noquote() << "Usage: dev_udp_sender <listenPort>";
        return 42;
    }

    auto listenPort = QString(argv[1]).toUInt();

    QUdpSocket recvSock;

    if (!recvSock.bind(QHostAddress::Any, listenPort, QAbstractSocket::DefaultForPlatform))
    {
        qDebug() << "bind failed" << recvSock.errorString();
        return 42;
    }

    qDebug() << "listening on port" << listenPort;

    char recvBuffer[1u << 16] = {};
    QHostAddress srcAddress;
    u16 srcPort = 0;
    QElapsedTimer timer;
    double totalBytes = 0;
    u64 totalPackets = 0.0;

    while (true)
    {
        if (recvSock.waitForReadyRead())
        {
            if (!timer.isValid())
            {
                timer.start();
            }

            s64 bytesReceived = recvSock.readDatagram(
                recvBuffer,
                sizeof(recvBuffer),
                &srcAddress,
                &srcPort);

            if (bytesReceived < 0)
            {
                qDebug() << "recv error" << recvSock.errorString();
                break;
            }

            qDebug() << "received" << bytesReceived << "bytes";

            totalBytes += bytesReceived;
            totalPackets++;

            if (bytesReceived >= 4 && strncmp(recvBuffer, "QUIT", 4) == 0)
            {
                qDebug() << "QUIT";
                break;
            }
        }
    }

    double secondsElapsed = timer.elapsed() / 1000.0;
    double totalMb = totalBytes / Megabytes(1);
    double mbPerSecond = totalMb / secondsElapsed;
    double packetsPerSecond = totalPackets / secondsElapsed;

    qDebug() << "secondsElapsed" << secondsElapsed << "totalMb =" << totalMb
             << ", mbPerSecond =" << mbPerSecond
             << ", totalPackets =" << totalPackets
             << ", packetsPerSecond =" << packetsPerSecond;

    return 0;
}
