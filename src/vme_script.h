#ifndef __VME_SCRIPT_QT_H__
#define __VME_SCRIPT_QT_H__

#include <cstdint>
#include <QVector>
#include <QSyntaxHighlighter>

class QFile;
class QTextStream;
class QString;

namespace vme_script
{
enum class CommandType
{
    Invalid,
    Read,
    Write,
    Wait,
    Marker,

    BLT,
    BLTFifo,
    MBLT,
    MBLTFifo,

    BLTCount,
    BLTFifoCount,
    MBLTCount,
    MBLTFifoCount,
};

enum class AddressMode
{
    A16 = 1,
    A24,
    A32
};

enum class DataWidth
{
    D16 = 1,
    D32
};

struct Command
{
    CommandType type = CommandType::Invalid;
    AddressMode addressMode = AddressMode::A32;
    DataWidth dataWidth = DataWidth::D16;
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t transfers = 0;
    uint32_t delay_ms = 0;
    uint32_t countMask = 0;
    AddressMode blockCountAddressMode = AddressMode::A32;
    uint32_t blockCountAddress = 0;
};

QString to_string(CommandType commandType);
QString to_string(AddressMode addressMode);
QString to_string(DataWidth dataWidth);
QString to_string(const Command &cmd);

QString format_hex(uint32_t value);

struct VMEScript
{
    QVector<Command> commands;
};

struct ParseError
{
    ParseError(const QString &message, int lineNumber = -1)
        : message(message)
        , lineNumber(lineNumber)
    {}

    QString message;
    int lineNumber;
};

VMEScript parse(QFile *input);
VMEScript parse(const QString &input);
VMEScript parse(QTextStream &input);

#define QSL QStringLiteral

static const QMap<CommandType, QString> commandTypeToString =
{
    { CommandType::Read,            QSL("read") },
    { CommandType::Write,           QSL("write") },
    { CommandType::Wait,            QSL("wait") },
    { CommandType::Marker,          QSL("marker") },
    { CommandType::BLT,             QSL("blt") },
    { CommandType::BLTFifo,         QSL("bltfifo") },
    { CommandType::MBLT,            QSL("mblt") },
    { CommandType::MBLTFifo,        QSL("mbltfifo") },
    { CommandType::BLTCount,        QSL("bltcount") },
    { CommandType::BLTFifoCount,    QSL("bltfifocount") },
    { CommandType::MBLTCount,       QSL("mbltcount") },
    { CommandType::MBLTFifoCount,   QSL("mbltfifocount") },
};

static const QMap<AddressMode, QString> addressModeToString = 
{
    { AddressMode::A16,         QSL("a16") },
    { AddressMode::A24,         QSL("a24") },
    { AddressMode::A32,         QSL("a32") },
};

static const QMap<DataWidth, QString> dataWidthToString = 
{
    { DataWidth::D16,           QSL("d16") },
    { DataWidth::D32,           QSL("d32") },
};

class SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

}

int main(int argc, char *argv[]);

#endif /* __VME_SCRIPT_QT_H__ */

