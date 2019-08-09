#include <chrono>
#include <iostream>
#include <QString>
#include <vector>

#include "typedefs.h"
#include "mvlc/mvlc_constants.h"
#include "mvlc/mvlc_trigger_io.h"

using namespace mesytec::mvlc;

struct Write { u16 reg; u16 val; const QString comment; };

template<typename Out>
Out &print_writes(Out &out, const std::vector<Write> &writes)
{
    QString buffer;

    for (const auto &w: writes)
    {
        auto line = QString("write_local 0x%1 0x%2%3")
            .arg(w.reg, 4, 16, QLatin1Char('0'))
            .arg(w.val, 4, 16, QLatin1Char('0'))
            .arg(w.comment.isEmpty() ? QString() : QString("    # %1").arg(w.comment));
        out << line.toStdString() << std::endl;
    }

    return out;
}

Write select_unit(u8 level, u8 unit, QString comment = {})
{
    if (comment.isEmpty())
    {
        comment = QString("select level=%1, unit=%2")
            .arg(static_cast<unsigned>(level))
            .arg(static_cast<unsigned>(unit));
    }

    return Write { 0x0200, static_cast<u16>((level << 8) | unit), comment };
}

int main(int argc, char *argv[])
{
    std::vector<Write> writes;


    std::chrono::milliseconds period(100);

    u8 stackId = 1;
    u8 level = 0; u8 unit = 0; // l0, timer0
    writes.emplace_back(select_unit(level, unit));
    writes.emplace_back(Write{ 0x0302, static_cast<u16>(stacks::TimerUnits::ms), "timer period base" }); // timer period base
    writes.emplace_back(Write{ 0x0304, 0, "timer delay" }); // delay
    writes.emplace_back(Write{ 0x0306, static_cast<u16>(period.count()), "timer period" }); // timer period value

    level = 3; unit = 0; // l3, stack_start0
    writes.emplace_back(select_unit(level, unit));
    writes.emplace_back(Write{ 0x0300, 1 });        // activate stack output
    writes.emplace_back(Write{ 0x0302, stackId });  // start the stack with the given stackId
    writes.emplace_back(Write{ 0x0380, 0 });        // connect to level0, timer0

    level = 2; unit = 0;
    writes.emplace_back(select_unit(level, unit));
    writes.emplace_back(Write{ 0x0380, 0 });

    level = 3; unit = 16; // l3, NIM-IO 0
    writes.emplace_back(select_unit(level, unit));
    writes.emplace_back(Write{ 0x0300 + 10, 1, "dir=output"});
    writes.emplace_back(Write{ 0x0300 + 16, 1, "activate cell"});
    writes.emplace_back(Write{ 0x0380, 0 });

    print_writes(std::cout, writes);
    return 0;
}
