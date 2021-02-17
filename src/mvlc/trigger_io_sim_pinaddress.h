#ifndef __MVME_MVLC_TRIGGER_IO_SIM_PINADDRESS_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_PINADDRESS_H__

#include <QString>
#include "libmvme_export.h"
#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

enum class PinPosition
{
    Input,
    Output
};

struct LIBMVME_EXPORT PinAddress
{
    PinAddress() {}

    PinAddress(const UnitAddress &unit_, const PinPosition &pos_)
        : unit(unit_)
        , pos(pos_)
    {}

    PinAddress(const PinAddress &) = default;
    PinAddress &operator=(const PinAddress &) = default;

    inline bool operator==(const PinAddress &o) const
    {
        return unit == o.unit && pos == o.pos;
    }

    inline bool operator!=(const PinAddress &o) const
    {
        return !(*this == o);
    }

    UnitAddress unit = { 0, 0, 0 };
    PinPosition pos = PinPosition::Input;
};

LIBMVME_EXPORT QStringList pin_path_list(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_path(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_name(const TriggerIO &trigIO, const PinAddress &pa);
LIBMVME_EXPORT QString pin_user_name(const TriggerIO &trigIO, const PinAddress &pa);

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_PINADDRESS_H__ */
