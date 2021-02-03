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
    public:
        DSOPlotWidget(QWidget *parent = nullptr);
        ~DSOPlotWidget() override;

        void setTraces(
            const Snapshot &snapshot,
            unsigned preTriggerTime = 0,
            const QStringList &names = {});

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
        // Emitted on pressing the start button. If the interval is 0 only one
        // snapshot should be acquired from the DSO. Otherwise the DSO is
        // restarted using the same setup after the interval has elapsed.
        void startDSO(
            const DSOSetup &dsoSetup,
            const std::chrono::milliseconds &interval);

        // Emitted on pressing the stop button.
        void stopDSO();

    public:
        DSOControlWidget(QWidget *parent = nullptr);
        ~DSOControlWidget() override;

        DSOSetup getDSOSetup() const;
        std::chrono::milliseconds getInterval() const;

    public slots:
        // Load the DSOSetup and the interval into the GUI.
        void setDSOSetup(
            const DSOSetup &setup,
            const std::chrono::milliseconds &interval = {});

        // Notify the widget about the current state of the DSO sampler. If
        // enabled most of the UI except for the stop button will be disabled.
        void setDSOActive(bool active);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io_dso
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_UI_H__ */
