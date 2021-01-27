#ifndef __MVME_MVLC_TRIGGER_IO_DSO_UI_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_UI_H__

#include <QWidget>
#include "libmvme_export.h"
#include "mvlc/trigger_io_dso.h"

class QwtPlot;

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_dso
{

class LIBMVME_EXPORT DSOPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        DSOPlotWidget(QWidget *parent = nullptr);
        ~DSOPlotWidget() override;

        void setSnapshot(const Snapshot &snapshot, unsigned preTriggerTime = 0,
                         const QStringList &names = {});

        QwtPlot *getQwtPlot();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT DSOControlWidget: public QWidget
{
    Q_OBJECT
    signals:
        void startDSO(
            const ScopeSetup &dsoSetup,
                const std::chrono::milliseconds &interval = {});

        void stopDSO();

    public:
        DSOControlWidget(QWidget *parent = nullptr);
        ~DSOControlWidget() override;


    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class ScopeWidget: public QWidget
{
    Q_OBJECT
    public:
        ScopeWidget(mvlc::MVLC &mvlc, QWidget *parent = nullptr);
        ~ScopeWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io_dso
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_UI_H__ */
