#include "mvlc/trigger_io_dso_sim.h"

#include <QGroupBox>
#include <QSplitter>
#include <QProgressDialog>
#include <QtConcurrent>

#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/trigger_io_dso_ui.h"
#include "mvlc/trigger_io_sim_ui.h"


namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace mesytec::mvme_mvlc::trigger_io_dso;

struct DSOSimWidget::Private
{
    DSOSimWidget *q;

    VMEScriptConfig *trigIOScript;
    mvlc::MVLC mvlc;
    DSOSetup dsoSetup;
    Sim sim;

    std::chrono::milliseconds dsoInterval;
    std::atomic<bool> stopSampling;

    DSOControlWidget *dsoControlWidget;
    TraceSelectWidget *traceSelectWidget;
    DSOPlotWidget *dsoPlotWidget;

    void on_trigger_io_modified()
    {
        qDebug() << __PRETTY_FUNCTION__;

        auto trigIO = parse_trigger_io_script_text(this->trigIOScript->getScriptContents());
        this->traceSelectWidget->setTriggerIO(trigIO);
        this->sim.trigIO = trigIO;

        // Update the names in the plot widget by reassigning the trace list.
        on_traceSelection_changed(this->traceSelectWidget->getSelection());
    }

    void on_traceSelection_changed(const QVector<PinAddress> &selection)
    {
        qDebug() << __PRETTY_FUNCTION__
            << "selection size =" << selection.size();

        Snapshot traces;
        traces.reserve(selection.size());
        QStringList traceNames;
        traceNames.reserve(selection.size());

        for (const auto &pa: selection)
        {
            if (auto trace = lookup_trace(this->sim, pa))
            {
                traces.push_back(*trace); // copy the trace

                QString name = QSL("%1 (%2)")
                    .arg(pin_path(this->sim.trigIO, pa))
                    .arg(pin_user_name(this->sim.trigIO, pa))
                    ;

                traceNames.push_back(name);
            }
        }

        std::reverse(std::begin(traces), std::end(traces));
        std::reverse(std::begin(traceNames), std::end(traceNames));

        this->dsoPlotWidget->setTraces(
            traces, this->dsoSetup.preTriggerTime, traceNames);
    }

    void startDSO(
        const DSOSetup &dsoSetup,
        const std::chrono::milliseconds &interval)
    {
        this->dsoSetup = dsoSetup;
        this->dsoInterval = interval;
        // Simulate up to twice the time interval between the pre and post
        // trigger times.
        SampleTime simMaxtime((dsoSetup.postTriggerTime + dsoSetup.preTriggerTime) * 2);

        QProgressDialog progressDialog;
        progressDialog.setLabelText(QSL("Waiting for sample..."));
        progressDialog.setMinimum(0);
        progressDialog.setMaximum(0);

        QFutureWatcher<std::error_code> watcher;

        QObject::connect(
            &watcher, &QFutureWatcher<std::error_code>::finished,
            &progressDialog, &QDialog::close);

        QObject::connect(
            &progressDialog, &QProgressDialog::canceled,
            q, [this] () { stopSampling = true; });

        stopSampling = false;
        this->dsoControlWidget->setDSOActive(true);

        std::vector<u32> sampleBuffer;

        auto futureResult = QtConcurrent::run(
            acquire_dso_sample, this->mvlc, this->dsoSetup,
            std::ref(sampleBuffer),
            std::ref(stopSampling));

        watcher.setFuture(futureResult);
        progressDialog.exec();

        this->dsoControlWidget->setDSOActive(false);

        this->sim.sampledTraces = fill_snapshot_from_mvlc_buffer(sampleBuffer);

        qDebug() << __PRETTY_FUNCTION__ << "starting sim";
        QElapsedTimer elapsed;
        elapsed.start();
        simulate(this->sim, simMaxtime);
        qDebug() << __PRETTY_FUNCTION__ << "sim finished" << elapsed.elapsed();

        on_traceSelection_changed(this->traceSelectWidget->getSelection());
    }

    void stopDSO()
    {
    }
};

DSOSimWidget::DSOSimWidget(
    VMEScriptConfig *trigIOScript,
    mvlc::MVLC mvlc,
    QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->trigIOScript = trigIOScript;
    d->mvlc = mvlc;

    d->dsoControlWidget = new DSOControlWidget;
    d->traceSelectWidget = new TraceSelectWidget;
    d->dsoPlotWidget = new DSOPlotWidget;

    auto gb_dsoControl = new QGroupBox("DSO Control");
    auto l_dsoControl = make_hbox<0, 0>(gb_dsoControl);
    l_dsoControl->addWidget(d->dsoControlWidget);

    auto gb_traceSelect = new QGroupBox("Trace Selection");
    auto l_traceSelect = make_hbox<0, 0>(gb_traceSelect);
    l_traceSelect->addWidget(d->traceSelectWidget);

    auto w_left = new QWidget;
    auto l_left = make_vbox<0, 0>(w_left);
    l_left->addWidget(gb_dsoControl, 0);
    l_left->addWidget(gb_traceSelect, 1);


    auto splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(w_left);
    splitter->addWidget(d->dsoPlotWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    auto widgetLayout = make_hbox(this);
    widgetLayout->addWidget(splitter);

    setWindowTitle("Trigger IO DSO");

    connect(d->trigIOScript, &VMEScriptConfig::modified,
            this, [this] () {
                d->on_trigger_io_modified();
            });

    connect(d->traceSelectWidget, &TraceSelectWidget::selectionChanged,
            this, [this] (const QVector<PinAddress> &selection) {
                d->on_traceSelection_changed(selection);
            });

    connect(d->dsoControlWidget, &DSOControlWidget::startDSO,
            this, [this] (const DSOSetup &setup,
                          const std::chrono::milliseconds &interval) {
                d->startDSO(setup, interval);
            });
}

DSOSimWidget::~DSOSimWidget()
{
#if 0
    d->readerQuit = true;

    if (d->readerThread.joinable())
        d->readerThread.join();
#endif
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
