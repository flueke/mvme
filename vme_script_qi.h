#ifndef __VME_SCRIPT_QI_H__
#define __VME_SCRIPT_QI_H__

#include <vector>
#include <cstdint>

#if 0
class QFile;
class QTextStream;
class QString;
#endif

namespace vme_script
{

struct Command
{
    enum Type
    {
        Invalid,

        Read,
        Write,
        BLT,
        BLTFifo,
        MBLT,
        MBLTFifo,
        Wait,
        Marker,

        BLTCount,
        BLTFifofCount,

        MBLTCount,
        MBLTFifofCount,
    };

    enum AddressMode
    {
        A16,
        A24,
        A32
    };

    enum DataWidth
    {
        D16,
        D32
    };

    Command(Type t = Invalid)
        : type(t)
    {}

    Command &setAddressMode(AddressMode mode)
    { addressMode = mode; return *this; }

    Type type = Invalid;
    AddressMode addressMode = A32;
    DataWidth dataWidth = D16;
    uint32_t address;
    uint32_t value;
    uint32_t tranfers;
    uint32_t delay;
};

struct VMEScript
{
    std::vector<Command> commands;
};

struct ParseError
{
};

#if 0
VMEScript parse(QFile &input)
VMEScript parse(QTextStream &input);
VMEScript parse(const QString &input);
#endif

template<typename Iterator>
VMEScript parse(Iterator first, Iterator last);

}

int main(int argc, char *argv[]);

#endif /* __VME_SCRIPT_QI_H__ */
