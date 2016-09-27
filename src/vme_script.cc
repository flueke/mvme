#include "vme_script.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QTextEdit>
#include <QApplication>
//#include <QFile>

namespace vme_script
{

AddressMode parseAddressMode(const QString &str)
{
    if (str.compare(QSL("a16"), Qt::CaseInsensitive) == 0)
        return AddressMode::A16;

    if (str.compare(QSL("a24"), Qt::CaseInsensitive) == 0)
        return AddressMode::A24;

    if (str.compare(QSL("a32"), Qt::CaseInsensitive) == 0)
        return AddressMode::A32;

    throw "invalid address mode";
}

DataWidth parseDataWidth(const QString &str)
{
    if (str.compare(QSL("d16"), Qt::CaseInsensitive) == 0)
        return DataWidth::D16;

    if (str.compare(QSL("d32"), Qt::CaseInsensitive) == 0)
        return DataWidth::D32;

    throw "invalid data width";
}

uint32_t parseAddress(const QString &str)
{
    bool ok;
    uint32_t ret = str.toUInt(&ok, 0);

    if (!ok)
        throw "invalid address";

    return ret;
}

uint32_t parseValue(const QString &str)
{
    bool ok;
    uint32_t ret = str.toUInt(&ok, 0);

    if (!ok)
        throw "invalid value";

    return ret;
}

Command parseRead(const QStringList &args, int lineNumber)
{
    auto usage = QSL("read <address_mode> <data_width> <address>");

    if (args.size() != 4)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = CommandType::Read;

    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);

    return result;
}

Command parseWrite(const QStringList &args, int lineNumber)
{
    auto usage = QSL("write <address_mode> <data_width> <address> <value>");

    if (args.size() != 5)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = CommandType::Write;
    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);
    result.value = parseValue(args[4]);

    return result;
}

Command parseWait(const QStringList &args, int lineNumber)
{
    auto usage = QSL("wait <delay>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;

    result.type = CommandType::Wait;

    static const QRegularExpression re("^(\\d+)([[:alpha:]]*)$");

    auto match = re.match(args[1]);

    if (!match.hasMatch())
        throw "Invalid delay";

    bool ok;
    result.delay_ms = match.captured(1).toUInt(&ok);

    auto unitString = match.captured(2);

    if (unitString == QSL("s"))
        result.delay_ms *= 1000;
    else if (unitString == QSL("ns"))
        result.delay_ms /= 1000;
    else if (!unitString.isEmpty() && unitString != QSL("ms"))
        throw "Invalid delay";

    return result;
}

Command parseMarker(const QStringList &args, int lineNumber)
{
    auto usage = QSL("marker <value>");

    if (args.size() != 2)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = CommandType::Marker;
    result.value = parseValue(args[1]);

    return result;
}

static const QMap<QString, CommandType> blockTransferTypes =
{
    { QSL("blt"),       CommandType::BLT },
    { QSL("bltfifo"),   CommandType::BLTFifo },
    { QSL("mblt"),      CommandType::MBLT },
    { QSL("mbltfifo"),  CommandType::MBLTFifo },

    { QSL("bltcount"),       CommandType::BLTCount },
    { QSL("bltfifocount"),   CommandType::BLTFifoCount},
    { QSL("mbltcount"),      CommandType::MBLTCount },
    { QSL("mbltfifocount"),  CommandType::MBLTFifoCount },
};

Command parseBlockTransfer(const QStringList &args, int lineNumber)
{
    auto usage = QString("%1 <address_mode> <address> <transfer_count>").arg(args[0]);

    if (args.size() != 4)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = blockTransferTypes[args[0]];
    result.addressMode = parseAddressMode(args[1]);
    result.address = parseAddress(args[2]);
    result.transfers = parseValue(args[3]);

    return result;
}

Command parseBlockTransferCountRead(const QStringList &args, int lineNumber)
{
    auto usage = (QString("%1 <register_address_mode> <register_data_width> <register_address>"
                         "<count_mask> <block_address_mode> <block_address>")
                  .arg(args[0]));

    if (args.size() != 7)
        throw ParseError(QString("Invalid number of arguments. Usage: %1").arg(usage), lineNumber);

    Command result;
    result.type = blockTransferTypes[args[0]];
    result.addressMode = parseAddressMode(args[1]);
    result.dataWidth = parseDataWidth(args[2]);
    result.address = parseAddress(args[3]);
    result.countMask = parseValue(args[4]);
    result.blockCountAddressMode = parseAddressMode(args[5]);
    result.blockCountAddress = parseAddress(args[6]);

    return result;
}

typedef Command (*CommandParser)(const QStringList &args, int lineNumber);

static const QMap<QString, CommandParser> commandParsers =
{
    { QSL("read"), parseRead },
    { QSL("write"), parseWrite },
    { QSL("wait"), parseWait },
    { QSL("marker"), parseMarker },

    { QSL("blt"), parseBlockTransfer },
    { QSL("bltfifo"), parseBlockTransfer },
    { QSL("mblt"), parseBlockTransfer },
    { QSL("mbltfifo"), parseBlockTransfer },

    { QSL("bltcount"), parseBlockTransferCountRead },
    { QSL("bltfifocount"), parseBlockTransferCountRead },
    { QSL("mbltcount"), parseBlockTransferCountRead },
    { QSL("mbltfifocount"), parseBlockTransferCountRead },
};

Command parse_line(const QString &line, int lineNumber)
{
    Command result;
    auto parts = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);

    if (parts.isEmpty())
        throw ParseError(QSL("Empty command"));

    auto fn = commandParsers.value(parts[0].toLower(), nullptr);
    
    if (!fn)
        throw ParseError(QString(QSL("No such command \"%1\"")).arg(parts[0]));

    try
    {
        return fn(parts, lineNumber);
    }
    catch (const char *message)
    {
        throw ParseError(message, lineNumber);
    }
}

VMEScript parse(QFile *input)
{
    QTextStream stream(input);
    return parse(stream);
}

VMEScript parse(const QString &input)
{
    QTextStream stream(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parse(stream);
}

VMEScript parse(QTextStream &input)
{
    VMEScript result;
    int lineNumber = 0;

    while (true)
    {
        auto line = input.readLine().simplified();
        ++lineNumber;

        if (line.isNull())
            break;

        if (line.startsWith('#'))
            continue;

        int startOfComment = line.indexOf('#');

        if (startOfComment > 0)
            line.resize(startOfComment);

        if (line.isEmpty())
            continue;

        result.commands.push_back(parse_line(line, lineNumber));
    }

    return result;
}

QString to_string(CommandType commandType)
{
    return commandTypeToString.value(commandType, QSL("unknown"));
}

QString to_string(AddressMode addressMode)
{
    return addressModeToString.value(addressMode, QSL("unknown"));
}

QString to_string(DataWidth dataWidth)
{
    return dataWidthToString.value(dataWidth, QSL("unknown"));
}

QString to_string(const Command &cmd)
{
    QString buffer;
    QString cmdStr = to_string(cmd.type);
    switch (cmd.type)
    {
        case CommandType::Invalid:
            return cmdStr;

        case CommandType::Read:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address));
            } break;

        case CommandType::Write:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address))
                    .arg(format_hex(cmd.value));
            } break;

        case CommandType::Wait:
            {
                buffer = QString("wait %1ms")
                    .arg(cmd.delay_ms);
            } break;

        case CommandType::Marker:
            {
                buffer = QString("marker %1")
                    .arg(cmd.value, 8, 16, QChar('0'));
            } break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
            {
                buffer = QString(QSL("%1 %2 %3 %4"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(format_hex(cmd.address))
                    .arg(cmd.transfers);
            } break;

        case CommandType::BLTCount:
        case CommandType::BLTFifoCount:
        case CommandType::MBLTCount:
        case CommandType::MBLTFifoCount:
            {
                buffer = QString(QSL("%1 %2 %3 %4 %5 %6 %7"))
                    .arg(cmdStr)
                    .arg(to_string(cmd.addressMode))
                    .arg(to_string(cmd.dataWidth))
                    .arg(format_hex(cmd.address))
                    .arg(format_hex(cmd.countMask))
                    .arg(to_string(cmd.blockCountAddressMode))
                    .arg(format_hex(cmd.blockCountAddress));
            } break;
    }
    return buffer;
}

QString format_hex(uint32_t value)
{
    if (value > 0xffff)
        return QString("0x%1").arg(value, 8, 16, QChar('0'));

    return QString("0x%1").arg(value, 4, 16, QChar('0'));
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression reComment("#.*$");

    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::blue);

    QRegularExpressionMatch match;
    int index = text.indexOf(reComment, 0, &match);
    if (index >= 0)
    {
        int length = match.capturedLength();
        setFormat(index, length, commentFormat);
    }
}

}

#if 0
int main(int argc, char *argv[])
{
    using namespace vme_script;

    QApplication app(argc, argv);
    QTextEdit textEdit;
    textEdit.setAcceptRichText(false);
    auto highLighter = new SyntaxHighlighter(&textEdit);
    textEdit.show();
    QFile inFile("test.vmescript");

    if (inFile.open(QIODevice::ReadOnly))
    {
        QTextStream in(&inFile);
        textEdit.setPlainText(in.readAll());
    }

    QObject::connect(&textEdit, &QTextEdit::textChanged, &textEdit, [&textEdit] {
        QTextStream out(stdout);
        QString text = textEdit.toPlainText();
        try
        {
            auto script = parse(text);
            for (auto command: script.commands)
            {
                out << to_string(command) << endl;
            }
        } catch (const ParseError &e)
        {
            out << "Line " << e.lineNumber << ": " << e.message << endl;
        }
    });
    return app.exec();
}
#endif
