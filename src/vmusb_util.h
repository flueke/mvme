#ifndef __VMUSB_UTIL_H__
#define __VMUSB_UTIL_H__

#include "libmvme_export.h"
#include "databuffer.h"
#include <QTextStream>

// Note: Assumption: VMUSBs HeaderOptMask option is not used!
LIBMVME_EXPORT void format_vmusb_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber);

#endif /* __VMUSB_UTIL_H__ */
