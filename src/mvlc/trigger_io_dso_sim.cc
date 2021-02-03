#include "mvlc/trigger_io_dso_sim.h"

#include <QGroupBox>
#include <QSplitter>
#include <QProgressDialog>
#include <QtConcurrent>
#include <chrono>
#include <thread>

#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/trigger_io_dso_ui.h"
#include "mvlc/trigger_io_sim_ui.h"

/* How the DSOSimWidget currently works:
 * - On clicking 'start' in the DSO control widget the DSOSimWidget runs both
 *   the dso sampling and the sim in a separate thread using QtConcurrent run.
 * - The widget uses a QFutureWatcher to keep track of the status of the async
 *   operation.
 * - Once sampling and simulation are done the local result is updated and the
 *   new traces are copied into the plot widget based on the current trace
 *   selection.
 * - If a non-zero interval is set the async operation is restarted via a
 *   QTimer.
 *
 * The code is not focused on performance. A new Sim structure is created and
 * filled on every invocation of run_dso_sim. Also traces are copied to the
 * plot widget.
 */



namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace mesytec::mvme_mvlc::trigger_io_dso;

namespace
{

struct DSO_Sim_Result
{
    std::error_code ec;
    std::vector<u32> dsoBuffer;
    Sim sim;
};

DSO_Sim_Result run_dso_sim(
    mvlc::MVLC mvlc,
    DSOSetup dsoSetup,
    TriggerIO trigIO,
    SampleTime simMaxTime,
    std::atomic<bool> &cancel)
{
    qDebug() << __PRETTY_FUNCTION__ << "enter";

    DSO_Sim_Result result = {};
    // Immediately copy the trigIO config into the result. The widget reuses
    // that copy for it's internal state once this function returns.
    result.sim.trigIO = trigIO;

    if (auto ec = acquire_dso_sample(mvlc, dsoSetup, result.dsoBuffer, cancel))
    {
        result.ec = ec;
        return result;
    }

    result.sim.sampledTraces = fill_snapshot_from_mvlc_buffer(
        result.dsoBuffer);

    if (cancel || result.sim.sampledTraces.empty())
        return result;

    simulate(result.sim, simMaxTime);

    qDebug() << __PRETTY_FUNCTION__ << "leave";
    return result;
}

} // end anon namespace

struct DSOSimWidget::Private
{
    DSOSimWidget *q;

    VMEScriptConfig *trigIOScript;
    mvlc::MVLC mvlc;
    DSOSetup dsoSetup;
    std::chrono::milliseconds dsoInterval;
    std::atomic<bool> cancelDSO;
    DSO_Sim_Result lastResult;

    QFutureWatcher<DSO_Sim_Result> resultWatcher;

    DSOControlWidget *dsoControlWidget;
    TraceSelectWidget *traceSelectWidget;
    DSOPlotWidget *dsoPlotWidget;

    void onTriggerIOModified()
    {
        qDebug() << __PRETTY_FUNCTION__;

        auto trigIO = parse_trigger_io_script_text(this->trigIOScript->getScriptContents());
        this->traceSelectWidget->setTriggerIO(trigIO);
        this->lastResult.sim.trigIO = trigIO;

        // Update the names in the plot widget by rebuilding the trace list
        // from the current selection.
        updatePlotTraces(this->traceSelectWidget->getSelection());
    }

    void updatePlotTraces(
        const QVector<PinAddress> &selection)
    {
        qDebug() << __PRETTY_FUNCTION__
            << "selection size =" << selection.size();

        Snapshot traces;
        traces.reserve(selection.size());
        QStringList traceNames;
        traceNames.reserve(selection.size());

        for (const auto &pa: selection)
        {
            if (auto trace = lookup_trace(this->lastResult.sim, pa))
            {
                traces.push_back(*trace); // copy the trace

                QString name = QSL("%1 (%2)")
                    .arg(pin_path(this->lastResult.sim.trigIO, pa))
                    .arg(pin_user_name(this->lastResult.sim.trigIO, pa))
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
        if (this->resultWatcher.isRunning())
            return;

        this->cancelDSO = false;
        this->dsoControlWidget->setDSOActive(true);

        this->dsoSetup = dsoSetup;
        this->dsoInterval = interval;

        runDSO();
    }

    void stopDSO()
    {
        this->cancelDSO = true;
    }

    void runDSO()
    {
        auto future = QtConcurrent::run(
            run_dso_sim,
            this->mvlc,
            this->dsoSetup,
            this->lastResult.sim.trigIO,
            getSimMaxTime(),
            std::ref(this->cancelDSO));

        this->resultWatcher.setFuture(future);
    }

    void onDSOSimRunFinished()
    {
        this->lastResult = this->resultWatcher.result();

        updatePlotTraces(this->traceSelectWidget->getSelection());

        if (!this->cancelDSO && this->dsoInterval != std::chrono::milliseconds::zero())
            QTimer::singleShot(this->dsoInterval, q, [this] () { runDSO(); });
        else
            this->dsoControlWidget->setDSOActive(false);
    }

    SampleTime getSimMaxTime() const
    {
        // Simulate up to twice the time interval between the pre and post
        // trigger times.
        SampleTime simMaxTime((dsoSetup.postTriggerTime + dsoSetup.preTriggerTime) * 2);
        return simMaxTime;
    }


};

DSOSimWidget::DSOSimWidget(
    VMEScriptConfig *trigIOScript,
    mvlc::MVLC &mvlc,
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
                d->onTriggerIOModified();
            });

    connect(d->traceSelectWidget, &TraceSelectWidget::selectionChanged,
            this, [this] (const QVector<PinAddress> &selection) {
                d->updatePlotTraces(selection);
            });

    connect(d->dsoControlWidget, &DSOControlWidget::startDSO,
            this, [this] (const DSOSetup &setup,
                          const std::chrono::milliseconds &interval) {
                d->startDSO(setup, interval);
            });

    connect(d->dsoControlWidget, &DSOControlWidget::stopDSO,
            this, [this] () {
                d->stopDSO();
            });

    connect(&d->resultWatcher, &QFutureWatcher<DSO_Sim_Result>::finished,
            this, [this] () {
                d->onDSOSimRunFinished();
            });

    d->onTriggerIOModified(); // initial data pull from the script
}

DSOSimWidget::~DSOSimWidget()
{
    d->cancelDSO = true;
    d->resultWatcher.waitForFinished();
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
