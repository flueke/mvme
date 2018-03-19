#include "data_filter_edit.h"
#include "qt_util.h"

//
// DataFilterEdit
//

static const int DataFilterEditWidthPadding = 6;
static const int DataFilterMaxBits = 32;

DataFilterEdit::DataFilterEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setAlignment(Qt::AlignRight);

    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);
    setFont(font);

    updateMaskAndWidth();
    setFilterString(generate_pretty_filter_string(getBitCount(), 'X'));
}

DataFilterEdit::DataFilterEdit(int bits, QWidget *parent)
    : DataFilterEdit(parent)
{
    setBitCount(bits);
    setFilterString(generate_pretty_filter_string(getBitCount(), 'X'));
}

DataFilterEdit::DataFilterEdit(const QString &filterString, QWidget *parent)
    : DataFilterEdit(parent)
{
    setFilterString(filterString);
}

DataFilterEdit::DataFilterEdit(const DataFilter &filter, QWidget *parent)
    : DataFilterEdit(parent)
{
    setFilter(filter);
}

void DataFilterEdit::setBitCount(int bits)
{
    if (bits > DataFilterMaxBits)
        throw std::length_error("maximum filter size of 32 exceeded");

    m_bits = bits;

    updateMaskAndWidth();
}

DataFilter DataFilterEdit::getFilter() const
{
    return makeFilterFromString(text());
}

void DataFilterEdit::setFilter(const DataFilter &filter)
{
    qDebug() << __PRETTY_FUNCTION__;
    auto tmpText = filter.getFilter();

    qDebug() << "  umodified filter text: " << tmpText;
    qDebug() << "  this bitCount: " << getBitCount();

    tmpText.replace(" ", "");

    tmpText = tmpText.right(getBitCount());

    while (tmpText.size() < getBitCount())
    {
        tmpText.prepend('X');
    }

    qDebug() << "  adjusted filter text: " << tmpText;

    setText(tmpText);
}

void DataFilterEdit::setFilterString(const QString &filterString)
{
    setFilter(makeFilterFromString(filterString));
}

QString DataFilterEdit::getFilterString() const
{
    return text();
}

void DataFilterEdit::updateMaskAndWidth()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto tmpText = text();
    auto mask = generate_pretty_filter_string(getBitCount(), 'N');

    qDebug() << "  text before mask change: " << tmpText;
    qDebug() << "  input mask string: " << mask;

    setInputMask(mask);

    qDebug() << "  text after mask change: " << text();

    tmpText.replace(" ", "");
    tmpText = tmpText.right(getBitCount());

    while (tmpText.size() < getBitCount())
    {
        tmpText.prepend('X');
    }

    qDebug() << "  new, adjusted text: " << tmpText;

    setText(tmpText);

    QFontMetrics fm(font());
    s32 padding = 6;
    s32 width = fm.width(inputMask()) + padding;
    setMinimumWidth(width);
}

DataFilterEdit *makeFilterEdit(u8 bits, QWidget *parent)
{
#if 1
    return new DataFilterEdit(parent);
#else
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);

    auto result = new DataFilterEdit(parent);
    result->setFont(font);
    result->setInputMask(generate_pretty_filter_string(bits, 'N'));
    result->setAlignment(Qt::AlignRight);

    QFontMetrics fm(font);
    s32 padding = 6;
    s32 width = fm.width(result->inputMask()) + padding;
    result->setMinimumWidth(width);

    return result;
#endif
}
