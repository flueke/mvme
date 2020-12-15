#include "mvlc/trigger_io_scope_ui.h"
#include "mesytec-mvlc/util/threadsafequeue.h"
#include "mvlc/mvlc_trigger_io.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <QDebug>
#include <chrono>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_scope
{

struct OsciWidget::Private
{
    OsciWidget *q;

    mvlc::MVLC mvlc;

    QSpinBox *spin_preTriggerTime,
             *spin_postTriggerTime;

    std::vector<QCheckBox *> checks_triggerChannels;

    QPushButton *pb_start,
                *pb_stop;

    std::thread readerThread;
    std::atomic<bool> readerQuit;
    mvlc::ThreadSafeQueue<std::vector<u32>> readerQueue;
    std::future<std::error_code> readerFuture;
    QTimer refreshTimer;


    void start()
    {
        if (readerThread.joinable())
            return;

        OsciSetup setup;

        setup.preTriggerTime = spin_preTriggerTime->value();
        setup.postTriggerTime = spin_postTriggerTime->value();

        for (auto bitIdx=0u; bitIdx<checks_triggerChannels.size(); ++bitIdx)
            setup.triggerChannels.set(bitIdx, checks_triggerChannels[bitIdx]->isChecked());

        std::promise<std::error_code> promise;
        readerFuture = promise.get_future();
        readerQuit = false;

        readerThread = std::thread(
            reader, mvlc, std::ref(setup), std::ref(readerQueue), std::ref(readerQuit),
            std::move(promise));

        refreshTimer.setInterval(500);
        refreshTimer.start();
        pb_start->setEnabled(false);
        pb_stop->setEnabled(true);
    }

    void stop()
    {
        refreshTimer.stop();
        readerQuit = true;

        if (readerThread.joinable())
            readerThread.join();

        qDebug() << __PRETTY_FUNCTION__ << readerFuture.get().message().c_str();

        refresh();
    }

    void analyze(const std::vector<std::vector<u32>> &buffers);
    void analyze(const std::vector<u32> &buffer);

    void refresh()
    {
        std::vector<std::vector<u32>> buffers;

        while (true)
        {
            std::vector<u32> buffer = readerQueue.dequeue();

            if (buffer.empty())
                break;

            buffers.emplace_back(buffer);
        }

        qDebug() << __PRETTY_FUNCTION__ << "got" << buffers.size() << "buffers from readerQueue";

        analyze(buffers);

        auto fs = readerFuture.wait_for(std::chrono::milliseconds(1));

        if (fs == std::future_status::ready)
        {
            refreshTimer.stop();
            pb_start->setEnabled(true);
            pb_stop->setEnabled(false);
            if (readerThread.joinable())
                readerThread.join();
        }
    }
};

void OsciWidget::Private::analyze(const std::vector<std::vector<u32>> &buffers)
{
    for (const auto &buffer: buffers)
        analyze(buffer);
}

void OsciWidget::Private::analyze(const std::vector<u32> &buffer)
{
}

OsciWidget::OsciWidget(mvlc::MVLC &mvlc, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->mvlc = mvlc;

    d->spin_preTriggerTime = new QSpinBox;
    d->spin_postTriggerTime = new QSpinBox;

    for (auto spin: { d->spin_preTriggerTime, d->spin_postTriggerTime })
    {
        spin->setMinimum(0);
        spin->setMaximum(std::numeric_limits<u16>::max());
        spin->setSuffix(" ns");
    }

    auto channelsLayout = new QGridLayout;

    for (auto bit = 0u; bit < trigger_io::NIM_IO_Count; ++bit)
    {
        d->checks_triggerChannels.emplace_back(new QCheckBox);
        int col = trigger_io::NIM_IO_Count - bit - 1;
        channelsLayout->addWidget(new QLabel(QString::number(bit)), 0, col);
        channelsLayout->addWidget(d->checks_triggerChannels[bit], 1, col);
    }

    auto pb_triggersAll = new QPushButton("all");
    auto pb_triggersNone = new QPushButton("none");

    connect(pb_triggersAll, &QPushButton::clicked,
            this, [this] () { for (auto cb: d->checks_triggerChannels) cb->setChecked(true); });

    connect(pb_triggersNone, &QPushButton::clicked,
            this, [this] () { for (auto cb: d->checks_triggerChannels) cb->setChecked(false); });

    channelsLayout->addWidget(pb_triggersAll, 0, trigger_io::NIM_IO_Count);
    channelsLayout->addWidget(pb_triggersNone, 1, trigger_io::NIM_IO_Count);

    d->pb_start = new QPushButton("Start");
    d->pb_stop = new QPushButton("Stop");

    connect(d->pb_start, &QPushButton::clicked, this, [this] () { d->start(); });
    connect(d->pb_stop, &QPushButton::clicked, this, [this] () { d->stop(); });
    d->pb_stop->setEnabled(false);

    auto layout = new QFormLayout;
    layout->addRow("Pre Trigger Time", d->spin_preTriggerTime);
    layout->addRow("Post Trigger Time", d->spin_postTriggerTime);
    layout->addRow("Trigger Channels", channelsLayout);
    layout->addRow(d->pb_start);
    layout->addRow(d->pb_stop);

    setLayout(layout);
    setWindowTitle("Trigger IO Osci");

    connect(&d->refreshTimer, &QTimer::timeout,
            this, [this] () { d->refresh(); });
}

OsciWidget::~OsciWidget()
{

    d->readerQuit = true;

    if (d->readerThread.joinable())
        d->readerThread.join();
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
