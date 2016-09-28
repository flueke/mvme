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
    WriteAbs,
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
    AddressMode blockAddressMode = AddressMode::A32;
    uint32_t blockAddress = 0;
};

QString to_string(CommandType commandType);
CommandType commandType_from_string(const QString &str);
QString to_string(AddressMode addressMode);
QString to_string(DataWidth dataWidth);
QString to_string(const Command &cmd);
QString format_hex(uint32_t value);

struct VMEScript
{
    QVector<Command> commands;
};

Command add_base_address(Command cmd, uint32_t baseAddress);

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

#ifndef QSL
#define QSL QStringLiteral
#endif

class SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

}

//int main(int argc, char *argv[]);

#endif /* __VME_SCRIPT_QT_H__ */

