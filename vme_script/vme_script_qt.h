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
    Invalid,
    A16,
    A24,
    A32
};

enum class DataWidth
{
    Invalid,
    D16,
    D32
};

struct Command
{
    CommandType type = CommandType::Invalid;
    AddressMode addressMode = AddressMode::Invalid;
    DataWidth dataWidth = DataWidth::Invalid;
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t transfers = 0;
    uint32_t delay_ms = 0;
    uint32_t countMask = 0;
    AddressMode blockCountAddressMode = AddressMode::Invalid;
    uint32_t blockCountAddress = 0;
};

QString to_string(const Command &cmd);

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

class SyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

    protected:
        virtual void highlightBlock(const QString &text) override;
};

}

int main(int argc, char *argv[]);

#endif /* __VME_SCRIPT_QT_H__ */

