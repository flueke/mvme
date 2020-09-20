#include <QCoreApplication>
#include <QUdpSocket>
#include <QHostInfo>
#include <mesytec-mvlc/mesytec-mvlc.h> // for constants

using namespace mesytec;

using mvlc::u8;
using mvlc::u16;
using mvlc::u32;

void do_write(QUdpSocket &sock, const std::vector<u32> &buffer, const QHostAddress &address, u16 port)
{
    const qint64 bytesToTransfer = buffer.size() * sizeof(u32);
    auto res = sock.writeDatagram(
        reinterpret_cast<const char *>(buffer.data()),
        buffer.size() * sizeof(u32),
        address, port);

    if (res != bytesToTransfer)
        throw std::runtime_error("do_write failed: " + sock.errorString().toStdString());
}

void read_packet_from_any(QUdpSocket &sock, std::vector<u32> &dest, size_t maxWordsToRead)
{
    if (maxWordsToRead == 0)
    {
        dest.resize(0);
        return;
    }

    dest.resize(maxWordsToRead);
    qint64 maxBytesToRead = maxWordsToRead * sizeof(u32);
    auto res = sock.readDatagram(
        reinterpret_cast<char *>(dest.data()), maxBytesToRead,
        nullptr /* address */, nullptr /* port */);

    if (res == -1)
        throw std::runtime_error("read_packet_from_any failed: " + sock.errorString().toStdString());

    // FIXME: silently discards additional bytes in case wordsTransferred isn't divisible by sizeof(u32)

    auto wordsTransferred = res / sizeof(u32);

    dest.resize(wordsTransferred);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc <= 1)
    {
        qDebug().noquote() << "Usage:" << argv[0] << " <mvlcHost>";
        return 1;
    }

    auto hostInfo = QHostInfo::fromName(argv[1]);

    if (hostInfo.addresses().isEmpty())
    {
        qDebug().noquote() << "DNS error: " << hostInfo.errorString();
        return 2;
    }

    auto destHost = hostInfo.addresses()[0];
    auto destPort = mvlc::eth::CommandPort;

    QUdpSocket cmdSock;

    if (!cmdSock.bind(QHostAddress::Any, 0, QAbstractSocket::DefaultForPlatform))
    {
        qDebug() << "bind failed" << cmdSock.errorString();
        return 42;
    }

    // Recreate the first part of the MVLC connect sequence here: read the trigger registers.
    u32 referenceWord = 1;
    mvlc::SuperCommandBuilder cmdList;

    u8 stackId = 0;
    u16 addr = mvlc::stacks::get_trigger_register(stackId);
    cmdList.addReferenceWord(referenceWord++);
    cmdList.addReadLocal(addr);
    auto request = make_command_buffer(cmdList);

    do_write(cmdSock, request, destHost, destPort);

    std::vector<u32> receiveBuffer(10240);
    read_packet_from_any(cmdSock, receiveBuffer, receiveBuffer.size());

    qDebug() << "received" << receiveBuffer.size() << "words from MVLC ("
        << receiveBuffer.size() * sizeof(u32) << "bytes).";
}
