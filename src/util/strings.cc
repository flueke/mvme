#include "strings.h"
#include "typedefs.h"
#include <QStringList>

#ifndef QSL
#define QSL(str) QStringLiteral(str)
#endif

static const QStringList PositivePrefixes =
{
    QSL(""),
    QSL("k"), // kilo
    QSL("M"), // mega
    QSL("G"), // giga
    QSL("T"), // tera
    QSL("P"), // peta
    QSL("E"), // exa
};

#if 0
static const QStringList NegativePrefixes =
{
    QSL(""),
    QSL("m"), // milli
    QSL("µ"), // micro
    QSL("n"), // nano
    QSL("p"), // pico
    QSL("f"), // femto
    QSL("a"), // atto
};
#endif

QString format_number(double value, const QString &unit,  UnitScaling scaling,
                      int fieldWidth, char format, int precision, QChar fillChar)
{
    const double factor = (scaling == UnitScaling::Binary) ? 1024.0 : 1000.0;

    s32 prefixIndex = 0;

    if (value >= 0)
    {
        while (value >= factor)
        {
            value /= factor;
            prefixIndex++;

            if (prefixIndex == PositivePrefixes.size() - 1)
                break;
        }
    }

    QString result(QSL("%1 %2%3")
                   .arg(value, fieldWidth, format, precision, fillChar)
                   .arg(PositivePrefixes[prefixIndex])
                   .arg(unit)
                  );

    return result;
}
