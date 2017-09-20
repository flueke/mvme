#ifndef __VMUSB_UTIL_H__
#define __VMUSB_UTIL_H__

#include "databuffer.h"
#include <QTextStream>

// Note: Assumption: VMUSBs HeaderOptMask option is not used!
void format_vmusb_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber);

#endif /* __VMUSB_UTIL_H__ */
