#include "mvlc/mvlc_trigger_io_util.h"
#include "util/qt_str.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io
{

QTextStream &print_front_panel_io_table(QTextStream &out, const TriggerIO &ioCfg)
{
    auto nim_io_state_string = [](const IO &io) -> QString
    {
        QString result;

        if (io.direction == IO::Direction::in)
            result = QSL("input");
        else if (io.activate)
            result = QSL("output");
        else
            result = QSL("inactive");

        return result;
    };

    auto ecl_io_state_string = [](const IO &io) -> QString
    {
        QString result;

        if (io.activate)
            result = QSL("output");
        else
            result = QSL("inactive");

        return result;
    };

    out << "###########################################################" << endl;
    out << "#                 MVLC NIM/TTL signals                    #" << endl;
    out << "###########################################################" << endl;
    out << endl;

    left(out);

    out << qSetFieldWidth(12) << QSL("# Idx")
        << qSetFieldWidth(16) << QSL("State") << QSL("Name")
        << qSetFieldWidth(0) << endl;

    for (ssize_t nim = NIM_IO_Count - 1; nim >= 0; nim--)
    {
        auto &io = ioCfg.l0.ioNIM.at(nim);

        out << qSetFieldWidth(12)
            << QSL("  %1").arg(nim, 2, 10, QLatin1Char(' '))
            << qSetFieldWidth(16)
            << nim_io_state_string(io)
            << ioCfg.l0.unitNames.at(nim + Level0::NIM_IO_Offset)
            << qSetFieldWidth(0) << endl;
    }

    out << endl;
    out << "###########################################################" << endl;
    out << "#                 MVLC LVDS signals                       #" << endl;
    out << "###########################################################" << endl;
    out << endl;

    out << qSetFieldWidth(12) << QSL("# Idx")
        << qSetFieldWidth(16) << QSL("State") << QSL("Name")
        << qSetFieldWidth(0) << endl;

    for (ssize_t idx = ECL_OUT_Count - 1; idx >= 0; idx--)
    {
        auto &io = ioCfg.l3.ioECL.at(idx);

        out << qSetFieldWidth(12)
            << QSL("  %1").arg(idx, 1, 10, QLatin1Char(' '))
            << qSetFieldWidth(16)
            << ecl_io_state_string(io)
            << ioCfg.l3.unitNames.at(idx + Level3::ECL_Unit_Offset)
            << qSetFieldWidth(0) << endl;
    }

    return out;
}

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec
