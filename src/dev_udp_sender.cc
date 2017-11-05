#include <QCoreApplication>
#include <QHostInfo>
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

    static const char testData[] = "Hello, World!";
    static const int testDataSize = sizeof(testData);
    static const int loopCount = 1;


    for (int i = 0; i < loopCount; i++)
    {
        qint64 bytesWritten = sendSock.writeDatagram(testData, testDataSize, destHost, destPort);

        if (bytesWritten != testDataSize)
        {
            qDebug() << "send error" << sendSock.errorString();
            return 43;
        }
    }

    return 0;
}
