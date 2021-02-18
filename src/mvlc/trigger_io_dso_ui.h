#ifndef __MVME_MVLC_TRIGGER_IO_DSO_UI_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_UI_H__

#include <QWidget>
#include <chrono>
#include "libmvme_export.h"
#include "mvlc/trigger_io_dso.h"

class QwtPlot;

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_dso
{

// Plot widget for DSO or simulated traces.
class LIBMVME_EXPORT DSOPlotWidget: public QWidget
{
    Q_OBJECT
    signals:
        void traceClicked(const Trace &trace, const QString &name);

    public:
        DSOPlotWidget(QWidget *parent = nullptr);
        ~DSOPlotWidget() override;

        void setTraces(
            const Snapshot &snapshot,
            unsigned preTriggerTime = 0,
            const QStringList &names = {});

        void setPostTriggerTime(double postTriggerTime); // FIXME: move into setTraces()?

        // Set the vector element to true if the trace with the corresponding
        // index should be treated as a trigger trace, e.g. drawn in a
        // different style/color. Must be called after setTraces to have an
        // effect.
        void setTriggerTraceInfo(const std::vector<bool> &triggerTraces);

        void setXInterval(double xMin, double xMax);
        void setXAutoScale();

        QwtPlot *getQwtPlot();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// GUI controls to specify a DSOSetup and a DSO poll interval.
class LIBMVME_EXPORT DSOControlWidget: public QWidget
{
    Q_OBJECT
    signals:
        // Emitted on pressing the start button.
        // Use getPre/PostTriggerTime() and getInterval() to query for the DSO
        // parameters. If the interval is 0 only one snapshot should be
        // acquired from the DSO. Otherwise the DSO is restarted using the same
        // setup after the interval has elapsed.
        void startDSO();

        // Emitted on pressing the stop button.
        void stopDSO();

    public:
        DSOControlWidget(QWidget *parent = nullptr);
        ~DSOControlWidget() override;

        unsigned getPreTrigerTime();
        unsigned getPostTriggerTime();
        std::chrono::milliseconds getInterval() const;

    public slots:
        // Load the pre- and postTriggerTimes and the interval into the GUI.
        void setDSOSettings(
            unsigned preTriggerTime,
            unsigned postTriggerTime,
            const std::chrono::milliseconds &interval = {});

        // Notify the widget about the current state of the DSO sampler.
        void setDSOActive(bool active);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io_dso
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_UI_H__ */
