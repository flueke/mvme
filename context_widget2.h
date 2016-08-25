#ifndef CONTEXT_WIDGET2_H
#define CONTEXT_WIDGET2_H

#include <QWidget>

namespace Ui {
class ContextWidget2;
}

class ContextWidget2 : public QWidget
{
        Q_OBJECT

    public:
        explicit ContextWidget2(QWidget *parent = 0);
        ~ContextWidget2();

    private:
        Ui::context_widget2 *ui;
};

#endif // CONTEXT_WIDGET2_H
