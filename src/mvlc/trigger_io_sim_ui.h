#ifndef __MVME_MVLC_TRIGGER_IO_SIM_UI_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_UI_H__

#include <memory>
#include <QDebug>
#include <QMetaType>
#include <QWidget>

#include "libmvme_export.h"
#include "mvlc/trigger_io_sim.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

class LIBMVME_EXPORT TraceSelectWidget: public QWidget
{
    Q_OBJECT

    signals:
        void selectionChanged(const QVector<PinAddress> &selection);

    public:
        TraceSelectWidget(QWidget *parent = nullptr);
        ~TraceSelectWidget() override;

        void setTriggerIO(const TriggerIO &trigIO);
        void setSelection(const QVector<PinAddress> &selection);
        QVector<PinAddress> getSelection() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

Q_DECLARE_METATYPE(mesytec::mvme_mvlc::trigger_io::PinAddress);

QDataStream &operator<<(
    QDataStream &out, const mesytec::mvme_mvlc::trigger_io::PinAddress &pin);
QDataStream &operator>>(
    QDataStream &in, mesytec::mvme_mvlc::trigger_io::PinAddress &pin);

QDebug operator<<(
    QDebug dbg, const mesytec::mvme_mvlc::trigger_io::PinAddress &pin);

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_UI_H__ */
