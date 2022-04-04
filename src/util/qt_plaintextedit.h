#ifndef __MVME_QT_PLAINTEXTEDIT_H__
#define __MVME_QT_PLAINTEXTEDIT_H__

// Source: https://stackoverflow.com/a/33585505/17562886

#include <QPlainTextEdit>
#include <QMouseEvent>

namespace mesytec
{
namespace mvme
{
namespace util
{

class PlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT

private:
    QString clickedAnchor;

public:
    explicit PlainTextEdit(QWidget *parent = 0);

    void mousePressEvent(QMouseEvent *e)
    {
        clickedAnchor = (e->button() & Qt::LeftButton) ? anchorAt(e->pos()) :
                                                         QString();
        QPlainTextEdit::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e)
    {
        if (e->button() & Qt::LeftButton && !clickedAnchor.isEmpty() &&
            anchorAt(e->pos()) == clickedAnchor)
        {
            emit linkActivated(clickedAnchor);
        }

        QPlainTextEdit::mouseReleaseEvent(e);
    }

signals:
    void linkActivated(QString);
};

} // end namespace util
} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_QT_PLAINTEXTEDIT_H__ */
