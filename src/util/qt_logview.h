#ifndef __MVME_UTIL_QT_LOGVIEW_H__
#define __MVME_UTIL_QT_LOGVIEW_H__

#include <memory>
#include <QPlainTextEdit>
#include "libmvme_export.h"

static const size_t LogViewMaximumBlockCount = 10 * 1000u;

std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_logview(size_t maxBlockCount = LogViewMaximumBlockCount);

#endif /* __MVME_UTIL_QT_LOGVIEW_H__ */
