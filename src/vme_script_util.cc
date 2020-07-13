#include <QRegularExpression>
#include "vme_script_util.h"

namespace vme_script
{

/* Adapted from the QSyntaxHighlighter documentation. */
void SyntaxHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression reComment("#.*$");
    static const QRegExp reMultiStart("/\\*");
    static const QRegExp reMultiEnd("\\*/");

    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::blue);

    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
    {
        startIndex = text.indexOf(reMultiStart);
    }

    while (startIndex >= 0)
    {
        int endIndex = text.indexOf(reMultiEnd, startIndex);
        int commentLength;
        if (endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + reMultiEnd.matchedLength() + 3;
        }
        setFormat(startIndex, commentLength, commentFormat);
        startIndex = text.indexOf(reMultiStart, startIndex + commentLength);
    }

    QRegularExpressionMatch match;
    int index = text.indexOf(reComment, 0, &match);
    if (index >= 0)
    {
        int length = match.capturedLength();
        setFormat(index, length, commentFormat);
    }
}

} // end namespace vme_script

