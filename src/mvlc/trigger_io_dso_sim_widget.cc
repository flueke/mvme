#include "mvlc/trigger_io_dso_sim_widget.h"

#include <QGroupBox>
#include <QSplitter>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QPushButton>
#include <chrono>
#include <thread>
#include <yaml-cpp/yaml.h>

#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/trigger_io_dso_ui.h"
#include "mvlc/trigger_io_sim_ui.h"
#include "yaml-cpp/emittermanip.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace mesytec::mvme_mvlc::trigger_io_dso;

namespace
{

struct DSOSimGuiState
{
    DSOSetup dsoSetup;
    std::chrono::milliseconds dsoInterval;
    QVector<PinAddress> traceSelection;
    // TODO: somehow store the expanded state of the trace tree
};

void to_yaml(YAML::Emitter &out, const DSOSetup &dsoSetup)
{
    out << YAML::BeginMap;

    out << YAML::Key << "preTriggerTime"
        << YAML::Value << dsoSetup.preTriggerTime;

    out << YAML::Key << "postTriggerTime"
        << YAML::Value << dsoSetup.postTriggerTime;

    out << YAML::Key << "nimTriggers"
        << YAML::Value << dsoSetup.nimTriggers.to_ulong();

    out << YAML::Key << "irqTriggers"
        << YAML::Value << dsoSetup.irqTriggers.to_ulong();

    out << YAML::EndMap;
}

DSOSetup dso_setup_from_yaml(const YAML::Node &node)
{
    DSOSetup result;

    result.preTriggerTime = node["preTriggerTime"].as<u16>();
    result.postTriggerTime = node["postTriggerTime"].as<u16>();
    result.nimTriggers = node["nimTriggers"].as<unsigned long>();
    result.irqTriggers = node["irqTriggers"].as<unsigned long>();

    return result;
}

void to_yaml(YAML::Emitter &out, const QVector<PinAddress> &traceSelection)
{
    out << YAML::BeginSeq;

    for (const auto &pa: traceSelection)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq;

        for (const auto &value: pa.unit)
            out << value;

        out << static_cast<int>(pa.pos);

        out << YAML::EndSeq;
    }

    out << YAML::EndSeq;
}

QVector<PinAddress> trace_selection_from_yaml(const YAML::Node &node)
{
    QVector<PinAddress> result;

    for (const auto &yPinAddress: node)
    {
        PinAddress pa;

        for (size_t i=0; i<pa.unit.size(); ++i)
            pa.unit[i] = yPinAddress[i].as<unsigned>();

        pa.pos = static_cast<PinPosition>(yPinAddress[pa.unit.size()].as<int>());

        result.push_back(pa);
    }

    return result;
}

QString to_yaml(const DSOSimGuiState &guiState)
{
    YAML::Emitter out;

    out << YAML::BeginMap;

    out << YAML::Key << "DSOSetup"
        << YAML::Value;
    to_yaml(out, guiState.dsoSetup);

    out << YAML::Key << "DSOInterval"
        << YAML::Value << guiState.dsoInterval.count();

    out << YAML::Key << "TraceSelection"
        << YAML::Value;
    to_yaml(out, guiState.traceSelection);

    assert(out.good());

    return QString(out.c_str());
}

DSOSimGuiState dso_sim_gui_state_from_yaml(const QString &yamlString)
{
    DSOSimGuiState result;

    YAML::Node yRoot = YAML::Load(yamlString.toStdString());

    if (!yRoot) return result;

    result.dsoSetup = dso_setup_from_yaml(yRoot["DSOSetup"]);
    result.dsoInterval = std::chrono::milliseconds(yRoot["DSOInterval"].as<s64>());
    result.traceSelection = trace_selection_from_yaml(yRoot["TraceSelection"]);

    return result;
}

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
 * filled on every invocation of run_dso_and_sim. Also traces are copied to the
 * plot widget.
 */

struct DSO_Sim_Result
{
    std::error_code ec;
    std::vector<u32> dsoBuffer;
    Sim sim;
};

DSO_Sim_Result run_dso_and_sim(
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

bool is_trigger_pin(const PinAddress &pa, const DSOSetup &dsoSetup)
{
    if (pa.unit[0] == 0 && pa.pos == PinPosition::Input)
    {
        int nimTraceIndex = pa.unit[1] - Level0::NIM_IO_Offset;

        if (0 <= nimTraceIndex && nimTraceIndex < static_cast<int>(dsoSetup.nimTriggers.size()))
            return dsoSetup.nimTriggers.test(nimTraceIndex);

        int irqTraceIndex = pa.unit[1] - Level0::IRQ_Inputs_Offset;

        if (0 <= irqTraceIndex && irqTraceIndex < static_cast<int>(dsoSetup.irqTriggers.size()))
            return dsoSetup.irqTriggers.test(irqTraceIndex);
    }

    return false;
}

} // end anon namespace

struct DSOSimWidget::Private
{
    const char *GUIStateFileName = "mvlc_dso_sim_gui_state.yaml";
    size_t GUIStateFileMaxSize = Megabytes(1);

    DSOSimWidget *q;

    VMEScriptConfig *trigIOScript;
    mvlc::MVLC mvlc;
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

        auto dsoSetup = this->dsoControlWidget->getDSOSetup();

        Snapshot traces;
        QStringList traceNames;
        std::vector<bool> isTriggerTrace; // the evil bool vector :O

        traces.reserve(selection.size());
        traceNames.reserve(selection.size());

        for (const auto &pa: selection)
        {
            if (auto trace = lookup_trace(this->lastResult.sim, pa))
            {
                QString name = QSL("%1 (%2)")
                    .arg(pin_path(this->lastResult.sim.trigIO, pa))
                    .arg(pin_user_name(this->lastResult.sim.trigIO, pa))
                    ;

                bool isTrigger = is_trigger_pin(pa, dsoSetup);

                if (isTrigger)
                    name = QSL("<i>%1</i>").arg(name);

                traces.push_back(*trace); // copy the trace
                traceNames.push_back(name);
                isTriggerTrace.push_back(isTrigger);
            }
        }

        std::reverse(std::begin(traces), std::end(traces));
        std::reverse(std::begin(traceNames), std::end(traceNames));
        std::reverse(std::begin(isTriggerTrace), std::end(isTriggerTrace));

        this->dsoPlotWidget->setXInterval(
            -dsoSetup.preTriggerTime, getSimMaxTime().count());

        this->dsoPlotWidget->setTraces(
            traces, dsoSetup.preTriggerTime, traceNames);

        this->dsoPlotWidget->setTraceTriggerInfo(isTriggerTrace);
    }

    void startDSO()
    {
        if (this->resultWatcher.isRunning())
            return;

        this->cancelDSO = false;
        this->dsoControlWidget->setDSOActive(true);

        runDSO();
    }

    void stopDSO()
    {
        this->cancelDSO = true;
    }

    void runDSO()
    {
        auto future = QtConcurrent::run(
            run_dso_and_sim,
            this->mvlc,
            this->dsoControlWidget->getDSOSetup(),
            this->lastResult.sim.trigIO,
            getSimMaxTime(),
            std::ref(this->cancelDSO));

        this->resultWatcher.setFuture(future);
    }

    void onDSOSimRunFinished()
    {
        auto result = resultWatcher.result();

        if (!this->cancelDSO && !result.ec && result.dsoBuffer.size() > 2)
        {
            this->lastResult = result;

            updatePlotTraces(this->traceSelectWidget->getSelection());
        }

        auto interval = this->dsoControlWidget->getInterval();

        if (!this->cancelDSO && interval != std::chrono::milliseconds::zero())
            QTimer::singleShot(interval, q, [this] () { runDSO(); });
        else
            this->dsoControlWidget->setDSOActive(false);
    }

    SampleTime getSimMaxTime() const
    {
        auto dsoSetup = this->dsoControlWidget->getDSOSetup();
        // Simulate up to twice the time interval between the pre and post
        // trigger times.
        SampleTime simMaxTime((dsoSetup.postTriggerTime + dsoSetup.preTriggerTime) * 2);
        return simMaxTime;
    }

    void saveGUIState()
    {
        QFile outFile(GUIStateFileName);

        if (outFile.open(QIODevice::WriteOnly))
        {
            DSOSimGuiState state;
            state.dsoSetup = dsoControlWidget->getDSOSetup();
            state.dsoInterval = dsoControlWidget->getInterval();
            state.traceSelection = traceSelectWidget->getSelection();

            outFile.write(to_yaml(state).toUtf8());
        }
    }

    void loadGUIState()
    {
        QFile inFile(GUIStateFileName);

        if (inFile.open(QIODevice::ReadOnly))
        {
            auto yStr = QString::fromUtf8(inFile.read(GUIStateFileMaxSize));
            auto state = dso_sim_gui_state_from_yaml(yStr);

            this->dsoControlWidget->setDSOSetup(state.dsoSetup, state.dsoInterval);
            this->traceSelectWidget->setSelection(state.traceSelection);
        }
    }

    void onDebugButtonClicked()
    {
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

    auto pb_debug = new QPushButton("Debug");
    auto l_debugButton = make_hbox();
    l_debugButton->addWidget(pb_debug);
    l_debugButton->addStretch();

    auto w_left = new QWidget;
    auto l_left = make_vbox<0, 0>(w_left);
    l_left->addWidget(gb_dsoControl, 0);
    l_left->addWidget(gb_traceSelect, 1);
    l_left->addLayout(l_debugButton, 0);

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
            this, [this] () {
                d->startDSO();
            });

    connect(d->dsoControlWidget, &DSOControlWidget::stopDSO,
            this, [this] () {
                d->stopDSO();
            });

    connect(&d->resultWatcher, &QFutureWatcher<DSO_Sim_Result>::finished,
            this, [this] () {
                d->onDSOSimRunFinished();
            });

    connect(pb_debug, &QPushButton::clicked,
            this, [this] () {
                d->onDebugButtonClicked();
            });

    d->loadGUIState();
    d->onTriggerIOModified(); // initial data pull from the script
}

DSOSimWidget::~DSOSimWidget()
{
    d->cancelDSO = true;
    d->resultWatcher.waitForFinished();
    d->saveGUIState();
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
