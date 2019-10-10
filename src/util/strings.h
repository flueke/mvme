#ifndef __MVME_UTIL_STRINGS_H__
#define __MVME_UTIL_STRINGS_H__

#include "libmvme_export.h"

#include <QString>
#include "typedefs.h"

enum class UnitScaling
{
    Binary,
    Decimal,
};

QString
LIBMVME_EXPORT format_number(
    double value, const QString &unit,  UnitScaling scaling,
    int fieldWidth = 0, char format = 'g', int precision = -1,
    QChar fillChar = QLatin1Char(' '));

inline QString format_ipv4(u32 address)
{
    return QString("%1.%2.%3.%4")
        .arg((address >> 24) & 0xFF)
        .arg((address >> 16) & 0xFF)
        .arg((address >>  8) & 0xFF)
        .arg((address >>  0) & 0xFF);
}

#endif /* __MVME_UTIL_STRINGS_H__ */
