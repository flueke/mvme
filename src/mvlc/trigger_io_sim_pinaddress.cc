#include "mvlc/trigger_io_sim_pinaddress.h"
#include "util/qt_str.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

QStringList pin_path_list(const TriggerIO &trigIO, const PinAddress &pa)
{
    if (pa.unit[0] == 0)
    {
        if (pa.pos == PinPosition::Input)
            return { "sampled", lookup_default_name(trigIO, pa.unit) };
        else
            return { "L0", lookup_default_name(trigIO, pa.unit) };
    }

    if (pa.unit[0] == 1 || pa.unit[0] == 2)
    {
        QStringList result = {
            QSL("L%1").arg(pa.unit[0]),
            QSL("LUT%1").arg(pa.unit[1])
        };

        if (pa.pos == PinPosition::Input)
        {
            if (pa.unit[2] < LUT::InputBits)
                result << QSL("in%1").arg(pa.unit[2]);
            else
                result << QSL("strobeIn");
        }
        else
        {
            if (pa.unit[2] < LUT::OutputBits)
                result << QSL("out%1").arg(pa.unit[2]);
            else
                result << QSL("strobeOut");
        }

        return result;
    }

    if (pa.unit[0] == 3)
    {
        if (pa.pos == PinPosition::Input)
            return { "L3in", lookup_default_name(trigIO, pa.unit) };
        else
            return { "L3out", lookup_default_name(trigIO, pa.unit) };
    }

    return {};
}

QString pin_path(const TriggerIO &trigIO, const PinAddress &pa)
{
    return pin_path_list(trigIO, pa).join('.');
}

QString pin_name(const TriggerIO &trigIO, const PinAddress &pa)
{
    auto parts = pin_path_list(trigIO, pa);
    if (!parts.isEmpty())
        return parts.back();
    return "<pinName>";
}

QString pin_user_name(const TriggerIO &trigIO, const PinAddress &pa)
{
    if (pa.unit[0] == 0)
        return lookup_name(trigIO, pa.unit);

    if (pa.unit[0] == 1)
    {
        if (pa.pos == PinPosition::Output)
            return lookup_name(trigIO, pa.unit);
        auto con =  Level1::StaticConnections[pa.unit[1]][pa.unit[2]];
        return lookup_name(trigIO, con.address);
    }

    if (pa.unit[0] == 2)
    {
        if (pa.pos == PinPosition::Output)
        {
            if (pa.unit[2] < LUT::OutputBits)
                return lookup_name(trigIO, pa.unit);
            return {}; // strobeOut
        }

        auto con = Level2::StaticConnections[pa.unit[1]][pa.unit[2]];

        if (!con.isDynamic)
            return lookup_name(trigIO, con.address);

        auto srcAddr = get_connection_unit_address(trigIO, pa.unit);
        return lookup_name(trigIO, srcAddr);
    }

    if (pa.unit[0] == 3)
    {
        if (pa.pos == PinPosition::Output)
            return lookup_name(trigIO, pa.unit);
        auto srcAddr = get_connection_unit_address(trigIO, pa.unit);
        return lookup_name(trigIO, srcAddr);
    }

    return "<pinUserName>";
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
