#ifndef __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__
#define __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__

#include <memory>
#include <QtGlobal>

#include "libmvme_export.h"

class QFont;
class QPlainTextEdit;
class QTextEdit;

namespace mesytec
{
namespace mvme
{
namespace util
{

// These functions create a QPlainTextEdit or a QTextEdit respectively. The
// textedit is setup to use a monospace font, a tabstop width of 4 characters
// and it doesn't allow to wrap long lines.
// The pointSizeDelta argument can be used to add/subtract to/from the default
// font point size.
// Alternatively the font to use can be dircetly passed in.

std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit();
std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit(qreal pointSizeDelta);
std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit(const QFont &font);

std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit();
std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit(qreal pointSizeDelta);
std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit(const QFont &font);

} // end namespace util
} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__ */
