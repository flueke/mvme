#include "util/qt_monospace_textedit.h"

#include <QPlainTextEdit>
#include <QTextEdit>

#include "util/qt_font.h"

namespace
{

template<typename T>
std::unique_ptr<T> impl(const QFont &font)
{
    auto result = std::make_unique<T>();
    result->setFont(font);
    result->setLineWrapMode(T::NoWrap);
    set_tabstop_width(result.get(), 4);

    return result;
}

template<typename T>
std::unique_ptr<T> impl(qreal pointSizeDelta)
{
    auto font = make_monospace_font();
    font.setPointSizeF(font.pointSizeF() + pointSizeDelta);

    return impl<T>(font);
}

} // end anon namespace

namespace mvme
{
namespace util
{

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit()
{
    return impl<QPlainTextEdit>(0.0);
}

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit(qreal pointSizeDelta)
{
    return impl<QPlainTextEdit>(pointSizeDelta);
}

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit(const QFont &font)
{
    return impl<QPlainTextEdit>(font);
}

std::unique_ptr<QTextEdit> make_monospace_textedit()
{
    return impl<QTextEdit>(0.0);
}

std::unique_ptr<QTextEdit> make_monospace_textedit(qreal pointSizeDelta)
{
    return impl<QTextEdit>(pointSizeDelta);
}

std::unique_ptr<QTextEdit> make_monospace_textedit(const QFont &font)
{
    return impl<QTextEdit>(font);
}

} // end namespace util
} // end namespace mvme
