#ifndef __MVME_UTIL_QT_FONT_H__
#define __MVME_UTIL_QT_FONT_H__

#include <QFont>
#include <QString>
#include <QFontMetrics>

inline int calculate_tabstop_width(const QFont &font, int tabstop)
{
    QString spaces;
    for (int i = 0; i < tabstop; ++i) spaces += " ";
    QFontMetrics metrics(font);
    return metrics.width(spaces);
}

inline QFont make_monospace_font(QFont baseFont = QFont())
{
    baseFont.setFamily("Monospace");
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setFixedPitch(true);
    return baseFont;
}

#endif /* __QT_FONT_H__ */
