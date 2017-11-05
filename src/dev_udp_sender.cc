#include "util.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QThread>
#include <QUdpSocket>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc <= 2)
    {
        qDebug().noquote() << "Usage: dev_udp_sender <destHost> <destPort>";
        return 42;
    }

    auto hostInfo = QHostInfo::fromName(argv[1]);

    if (hostInfo.addresses().isEmpty())
    {
        qDebug().noquote() << "DNS error: " << hostInfo.errorString();
        return 42;
    }

    auto destHost = hostInfo.addresses()[0];
    auto destPort = QString(argv[2]).toUInt();

    qDebug() << destHost << destPort;

    QUdpSocket sendSock;

    if (!sendSock.bind(QHostAddress::LocalHost, 0, QAbstractSocket::DefaultForPlatform))
    {
        qDebug() << "bind failed" << sendSock.errorString();
        return 42;
    }

    //static const size_t OutBufferSize = 65500;
    //static const size_t OutBufferSize = 1u << 15;
    static const size_t OutBufferSize = 1440;

    static const int BurstSize = 50;

    char outBuffer[OutBufferSize] = {};
    ssize_t outBufferUsed = OutBufferSize;
    static const int loopCount = 1 << 20;
    QElapsedTimer timer;
    double totalBytes = 0.0;
    u64 totalPackets = 0.0;

    qDebug() << "OutBufferSize =" << OutBufferSize;

    timer.start();

    for (int i = 0; i < loopCount; i++)
    {
        if (i == loopCount - 1)
        {
            memcpy(outBuffer, "QUIT", 4);
        }

        s64 byteSent = sendSock.writeDatagram(
            outBuffer, outBufferUsed, destHost, destPort);

        if (byteSent != outBufferUsed)
        {
            qDebug() << "send error" << sendSock.errorString();
            return 43;
        }

        totalBytes += byteSent;
        totalPackets++;

        if (i > 0 && (i % BurstSize) == 0)
        {
            QThread::usleep(1);
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
