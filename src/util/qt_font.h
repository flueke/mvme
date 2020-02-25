#ifndef __MVME_UTIL_QT_FONT_H__
#define __MVME_UTIL_QT_FONT_H__

#include <cmath>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QString>
#include <QTextEdit>

inline int calculate_tabstop_width(const QFont &font, int tabstop)
{
    QFontMetricsF metrics(font);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    auto stopWidth = tabstop * metrics.horizontalAdvance(' ');
#else
    auto stopWidth = tabstop * metrics.width(' ');
#endif
    return std::ceil(stopWidth);
}

template<typename TextEdit>
inline void set_tabstop_width(TextEdit *textEdit, int tabstop)
{
    auto pixelWidth = calculate_tabstop_width(textEdit->font(), tabstop);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    textEdit->setTabStopDistance(pixelWidth);
#else
    textEdit->setTabStopWidth(pixelWidth);
#endif
}

inline QFont make_monospace_font()
{
    QFont baseFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setFixedPitch(true);
    return baseFont;
}

#endif /* __QT_FONT_H__ */
