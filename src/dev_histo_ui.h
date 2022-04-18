#ifndef __DEV_HISTO_UI_H__
#define __DEV_HISTO_UI_H__

#include <QwtPickerMachine>
#include <QObject>

class NewIntervalPickerMachine: public QObject, public QwtPickerMachine
{
    //Q_OBJECT
    signals:
        void canceled();

    public:
        NewIntervalPickerMachine(QObject *parent = nullptr)
            : QObject(parent)
            , QwtPickerMachine(SelectionType::PolygonSelection)
        {
        }

        ~NewIntervalPickerMachine() override;

        virtual QList<Command> transition(
            const QwtEventPattern &eventPattern, const QEvent *event) override;
};
#endif /* __DEV_HISTO_UI_H__ */
