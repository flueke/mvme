#ifndef __MVME_MVLC_TRIGGER_IO_SCRIPT_H__
#define __MVME_MVLC_TRIGGER_IO_SCRIPT_H__

#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io
{

namespace gen_flags
{
    using Flag = u8;
    static const Flag Default = 0u;
    static const Flag MetaIncludeDefaultUnitNames = 1u << 0;
};

QString lookup_name(const TriggerIO &cfg, const UnitAddress &addr);

QString generate_trigger_io_script_text(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags = gen_flags::MetaIncludeDefaultUnitNames);

TriggerIO parse_trigger_io_script_text(const QString &text);

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io

#endif /* __MVME_MVLC_TRIGGER_IO_SCRIPT_H__ */
