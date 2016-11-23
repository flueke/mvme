#include "histo_util.h"

QString makeAxisTitle(const QString &title, const QString &unit)
{
    QString result;
    if (!title.isEmpty())
    {
        result = title;
        if (!unit.isEmpty())
        {
            result += QString(" [%1]").arg(unit);
        }
    }

    return result;
}
