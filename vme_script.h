#ifndef __VME_SCRIPT_H__
#define __VME_SCRIPT_H__

#include <vector>

#if 0
class QFile;
class QTextStream;
class QString;
#endif

namespace vme_script
{

struct VMEScriptCommand
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
    };

    Type type = Invalid;
};

struct VMEScript
{
    std::vector<VMEScriptCommand> commands;
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

#endif /* __VME_SCRIPT_H__ */
