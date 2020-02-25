#include "util/variablify.h"
#include <QRegularExpression>

QString variablify(QString str)
{
    QRegularExpression ReIsValidFirstChar("[a-zA-Z_]");
    QRegularExpression ReIsValidChar("[a-zA-Z0-9_]");

    for (int i = 0; i < str.size(); i++)
    {
        QRegularExpressionMatch match;

        if (i == 0)
        {
            match = ReIsValidFirstChar.match(str, i, QRegularExpression::NormalMatch,
                                             QRegularExpression::AnchoredMatchOption);
        }
        else
        {
            match = ReIsValidChar.match(str, i, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchoredMatchOption);
        }

        if (!match.hasMatch())
        {
            //qDebug() << "re did not match on" << str.mid(i);
            //qDebug() << "replacing " << str[i] << " with _ in " << str;
            str[i] = '_';
        }
        else
        {
            //qDebug() << "re matched: " << match.captured(0);
        }
    }

    return str;
}

