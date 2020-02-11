#ifndef __MVME_MVLC_TRIGGER_IO_UTIL_H__
#define __MVME_MVLC_TRIGGER_IO_UTIL_H__

#include <QTextStream>
#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io
{

QTextStream &print_front_panel_io_table(QTextStream &out, const TriggerIO &ioCfg);

} // end namespace trigger_io
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_UTIL_H__ */
