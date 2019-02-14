#include "mvlc/mvlc_dev_gui.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QThread>
#include <QTimer>

#include <ftd3xx.h> // XXX

#include <iostream>
#include <cmath>

#include "ui_mvlc_dev_ui.h"

#include "mvlc_script.h"
#include "qt_util.h"
#include "vme_debug_widget.h"
#include "util/counters.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

//
// MVLCObject
//
MVLCObject::MVLCObject(QObject *parent)
    : QObject(parent)
{
}

MVLCObject::MVLCObject(const USB_Impl &impl, QObject *parent)
    : QObject(parent)
    , m_impl(impl)
{
    if (is_open(m_impl))
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    //TODO:
    //Lock the open/close lock so that no one can inc the refcount while we're doing things.
    //if (refcount of impl would go down to zero)
    //    close_impl();

}

void MVLCObject::connect()
{
    if (isConnected()) return;

    setState(Connecting);

    err_t error;
    m_impl = open_by_index(0, &error);
    m_impl.readTimeout_ms = 500;

    if (!is_open(m_impl))
    {
        emit errorSignal("Error connecting to MVLC", make_usb_error(error));
        setState(Disconnected);
    }
    else
    {
        setState(Connected);
    }
}

void MVLCObject::disconnect()
{
    if (!isConnected()) return;
    close(m_impl);
    setState(Disconnected);
}

void MVLCObject::setState(const State &newState)
{
    if (m_state != newState)
    {
        auto prevState = m_state;
        m_state = newState;
        emit stateChanged(prevState, newState);
    }
};

FixedSizeBuffer make_buffer(size_t capacity)
{
    FixedSizeBuffer result
    {
        .data = std::make_unique<u8[]>(capacity),
        .capacity = capacity,
        .used = 0
    };

    return result;
}

const char *reader_stat_name(ReaderStats::CounterEnum counter)
{
    switch (counter)
    {
        case ReaderStats::TotalBytesReceived:
            return "TotalBytesReceived";
        case ReaderStats::NumberOfAttemptedReads:
            return "NumberOfAttemptedReads";
        case ReaderStats::NumberOfTimeouts:
            return "NumberOfTimeouts";
        case ReaderStats::NumberOfErrors:
            return "NumberOfErrors";

        case ReaderStats::CountersCount:
            return "INVALID COUNTER";
    }

    return "UNKNOWN COUNTER";
}

//
// MVLCDataReader
//
MVLCDataReader::MVLCDataReader(QObject *parent)
    : QObject(parent)
    , m_doQuit(false)
    , m_nextBufferRequested(false)
    , m_readBuffer(make_buffer(USBSingleTransferMaxBytes))
{
    qDebug() << ">>> created" << this;
}

#if 0
MVLCDataReader::MVLCDataReader(const USB_Impl &impl, QObject *parent)
    : QObject(parent)
    , m_impl(impl)
{
    m_readBuffer.reserve(ReadBufferSize);
    m_impl.readTimeout_ms = ReadTimeout_ms;
    qDebug() << ">>> created" << this;
}
#endif

MVLCDataReader::~MVLCDataReader()
{
    qDebug() << ">>> destroyed" << this;
}

ReaderStats MVLCDataReader::getStats() const
{
    ReaderStats result;
    {
        QMutexLocker guard(&m_statsMutex);
        result = m_stats;
    }
    return result;
}

ReaderStats MVLCDataReader::getAndResetStats()
{
    ReaderStats result;
    {
        QMutexLocker guard(&m_statsMutex);
        result = m_stats;
        m_stats = {};
    }
    return result;
}

void MVLCDataReader::resetStats()
{
    QMutexLocker guard(&m_statsMutex);
    m_stats = {};
}

void MVLCDataReader::setImpl(const USB_Impl &impl)
{
    m_impl = impl;
    m_impl.readTimeout_ms = ReadTimeout_ms;
}

void MVLCDataReader::readoutLoop()
{
    m_doQuit = false;
    resetStats();

    emit started();

    qDebug() << __PRETTY_FUNCTION__ << "entering readout loop";
    qDebug() << __PRETTY_FUNCTION__ << "executing in" << QThread::currentThread();
    qDebug() << __PRETTY_FUNCTION__ << "read timeout is "
        << m_impl.readTimeout_ms << "ms";

    while (!m_doQuit)
    {
        size_t bytesTransferred = 0u;

        auto error = read_bytes(&m_impl, DataPipe,
                                m_readBuffer.data.get(), m_readBuffer.capacity,
                                &bytesTransferred);

        m_readBuffer.used = bytesTransferred;

        if (error == FT_DEVICE_NOT_CONNECTED)
        {
            emit message("Lost connection to MVLC. Leaving readout loop.");
            break;
        }

        {
            QMutexLocker guard(&m_statsMutex);

            ++m_stats.counters[ReaderStats::NumberOfAttemptedReads];
            m_stats.counters[ReaderStats::TotalBytesReceived] += bytesTransferred;

            if (error)
            {
                if (is_timeout(error))
                    ++m_stats.counters[ReaderStats::NumberOfTimeouts];
                else
                    ++m_stats.counters[ReaderStats::NumberOfErrors];
            }
        }

        if (m_nextBufferRequested)
        {
            QVector<u8> bufferCopy;
            bufferCopy.reserve(m_readBuffer.used);
            std::copy(m_readBuffer.data.get(),
                      m_readBuffer.data.get() + m_readBuffer.used,
                      std::back_inserter(bufferCopy));
            emit bufferReady(bufferCopy);
            m_nextBufferRequested = false;
        }
    }

    qDebug() << __PRETTY_FUNCTION__ << "left readout loop";

    emit stopped();
}

void MVLCDataReader::stop()
{
    m_doQuit = true;
}

void MVLCDataReader::requestNextBuffer()
{
    m_nextBufferRequested = true;
}

//
// MVLCDevGUI
//
struct MVLCDevGUI::Private
{
    MVLCDevGUI *q;

    QWidget *centralWidget;
    QToolBar *toolbar;
    QStatusBar *statusbar;

    //QAction *act_showLog,
    //        *act_showVMEDebug,
    //        *act_loadScript
    //        ;

    MVLCObject *mvlc;
    QThread readoutThread;
    MVLCDataReader *dataReader;

    QVector<QLabel *> readerStatLabels;
    QLabel *l_statRunDuration,
           *l_statReadRate;

    QDateTime tReaderStarted,
              tReaderStopped,
              tLastUpdate;

    ReaderStats prevReaderStats = {};
};

MVLCDevGUI::MVLCDevGUI(QWidget *parent)
    : QMainWindow(parent)
    , m_d(std::make_unique<Private>())
    , ui(new Ui::MVLCDevGUI)
{
    assert(m_d->dataReader == nullptr);
    m_d->q = this;
    m_d->mvlc = new MVLCObject(this);

    setObjectName(QSL("MVLC Dev Tool"));
    setWindowTitle(objectName());

    m_d->toolbar = new QToolBar(this);
    m_d->statusbar = new QStatusBar(this);
    m_d->centralWidget = new QWidget(this);
    ui->setupUi(m_d->centralWidget);

    setCentralWidget(m_d->centralWidget);
    addToolBar(m_d->toolbar);
    setStatusBar(m_d->statusbar);

    for (auto te: { ui->te_scriptInput, ui->te_log })
    {
        auto font = make_monospace_font();
        font.setPointSize(8);
        te->setFont(font);
    }

    new vme_script::SyntaxHighlighter(ui->te_scriptInput->document());
    static const int SpacesPerTab = 4;
    ui->te_scriptInput->setTabStopWidth(calculate_tab_width(
            ui->te_scriptInput->font(), SpacesPerTab));

    // Reader stats ui setup
    {
        auto l = new QFormLayout(ui->gb_readerStats);

        for (int counterType = 0;
             counterType < ReaderStats::CountersCount;
             counterType++)
        {
            auto name = reader_stat_name(
                static_cast<ReaderStats::CounterEnum>(counterType));
            auto label = new QLabel();
            m_d->readerStatLabels.push_back(label);
            l->addRow(name, label);
        }

        m_d->l_statRunDuration = new QLabel();
        l->addRow("Run Duration", m_d->l_statRunDuration);

        m_d->l_statReadRate = new QLabel();
        l->addRow("Read Rate", m_d->l_statReadRate);
    }

    // Interactions

    connect(m_d->mvlc, &MVLCObject::stateChanged,
            this, [this] (const MVLCObject::State &oldState,
                          const MVLCObject::State &newState)
    {
        switch (newState)
        {
            case MVLCObject::Disconnected:
                ui->le_usbStatus->setText("Disconnected");
                break;
            case MVLCObject::Connecting:
                ui->le_usbStatus->setText("Connecting...");
                break;
            case MVLCObject::Connected:
                ui->le_usbStatus->setText("Connected");
                logMessage("Connected to MVLC");
                break;
        }

        ui->pb_runScript->setEnabled(newState == MVLCObject::Connected);
        ui->pb_usbReconnect->setEnabled(newState != MVLCObject::Connecting);
    });


    connect(m_d->mvlc, &MVLCObject::errorSignal,
            this, [this] (const QString &msg, const MVLCError &error)
    {
        logMessage(msg + ": " + error.toString());
    });


    connect(ui->pb_runScript, &QPushButton::clicked,
            this, [this] ()
    {
        try
        {
            bool logRequest = ui->cb_scriptLogRequest->isChecked();
            bool logMirror  = ui->cb_scriptLogMirror->isChecked();

            auto scriptText = ui->te_scriptInput->toPlainText();
            auto cmdList = mvlc::script::parse(scriptText);
            auto cmdBuffer = mvlc::script::to_mvlc_command_buffer(cmdList);

            if (logRequest)
            {
                logBuffer(cmdBuffer, "Outgoing Request Buffer");
            }

            auto res = write_buffer(&m_d->mvlc->getImpl(), cmdBuffer);
            if (!res)
            {
                logMessage("Error writing to MVLC: " + res.toString());
                return;
            }

            QVector<u32> responseBuffer;
            res = read_response(&m_d->mvlc->getImpl(), //SuperResponseHeaderType,
                                responseBuffer);
            if (!res)
            {
                logMessage("Error reading response from MVLC: " + res.toString());
                return;
            }

            if (logMirror)
            {
                logBuffer(responseBuffer, "Mirror response from MVLC");
            }

            res = check_mirror(cmdBuffer, responseBuffer);

            if (!res)
            {
                // TODO: display buffers and differences side-by-side
                logMessage("Error: mirror check failed: " + res.toString());
                return;
            }

            if (!logRequest && !logMirror)
            {
                // Log a short message if none of the buffers where logged.
                logMessage(QString("Sent %1 words, received %2 words.")
                           .arg(cmdBuffer.size())
                           .arg(responseBuffer.size()));
            }

            if (ui->cb_scriptReadStack->isChecked())
            {
                logMessage("Attempting to read stack response...");

                res = read_response(&m_d->mvlc->getImpl(), //StackResponseHeaderType,
                                    responseBuffer);

                if (!res && !is_timeout(res))
                {
                    logMessage("Error reading from MVLC: " + res.toString());
                    return;
                }
                else if (responseBuffer.isEmpty())
                {
                    logMessage("Did not receive a stack response from MVLC");
                    return;
                }

                if (is_timeout(res))
                    logMessage("Received response but ran into a read timeout");

                logBuffer(responseBuffer, "Stack response from MVLC");
            }
        }
        catch (const mvlc::script::ParseError &e)
        {
            logMessage("MVLC Script parse error: " + e.toString());
        }
        catch (const vme_script::ParseError &e)
        {
            logMessage("Embedded VME Script parse error: " + e.toString());
        }
    });


    connect(ui->pb_clearLog, &QPushButton::clicked,
            this, &MVLCDevGUI::clearLog);


    connect(ui->pb_usbReconnect, &QPushButton::clicked,
            this, [this] ()
    {
        m_d->mvlc->disconnect();
        m_d->mvlc->connect();
    });


    connect(ui->pb_readCmdPipe, &QPushButton::clicked,
            this, [this] ()
    {
        static const int ManualCmdRead_WordCount = 1024;
        QVector<u32> readBuffer;
        readBuffer.resize(ManualCmdRead_WordCount);

        auto err = read_words(&m_d->mvlc->getImpl(), CommandPipe, readBuffer);
        if (!readBuffer.isEmpty())
            logBuffer(readBuffer, "Results of manual read from Command Pipe");
        if (err)
            logMessage("Read error: " + make_usb_error(err).toString());
    });


    connect(ui->pb_readDataPipe, &QPushButton::clicked,
            this, [this] ()
    {
        static const int ManualDataRead_WordCount = 8192;
        QVector<u32> readBuffer;
        readBuffer.resize(ManualDataRead_WordCount);

        auto err = read_words(&m_d->mvlc->getImpl(), DataPipe, readBuffer);
        if (!readBuffer.isEmpty())
            logBuffer(readBuffer, "Results of manual read from Data Pipe");
        if (err)
            logMessage("Read error: " + make_usb_error(err).toString());
    });

    //
    // MVLCDataReader and readout thread
    //

    m_d->readoutThread.setObjectName("MVLC Readout");
    m_d->dataReader = new MVLCDataReader();
    m_d->dataReader->moveToThread(&m_d->readoutThread);

    connect(this, &MVLCDevGUI::enterReadoutLoop,
            m_d->dataReader, &MVLCDataReader::readoutLoop);

    connect(m_d->dataReader, &MVLCDataReader::stopped,
            &m_d->readoutThread, &QThread::quit);

    connect(ui->pb_readerStart, &QPushButton::clicked,
            this, [this] ()
    {
        assert(!m_d->readoutThread.isRunning());

        logMessage("Starting readout");
        // Udpate the readers copy of the usb impl handler thingy
        m_d->dataReader->setImpl(m_d->mvlc->getImpl());
        m_d->readoutThread.start();
        emit enterReadoutLoop();
    });

    connect(ui->pb_readerStop, &QPushButton::clicked,
            this, [this] ()
    {
        assert(m_d->readoutThread.isRunning());

        logMessage("Stopping readout");
        // sets the atomic flag to make the reader break out of the loop.
        m_d->dataReader->stop();
    });

    connect(&m_d->readoutThread, &QThread::started,
            this, [this] ()
    {
        qDebug() << "readout thread started";
        ui->pb_readerStart->setEnabled(false);
        ui->pb_readerStop->setEnabled(true);
        ui->le_readoutStatus->setText("Running");
        ui->pb_usbReconnect->setEnabled(false);
        ui->pb_readDataPipe->setEnabled(false);

        m_d->tReaderStarted = QDateTime::currentDateTime();
        m_d->tReaderStopped = {};
    });

    connect(&m_d->readoutThread, &QThread::finished,
            this, [this] ()
    {
        qDebug() << "readout thread finished";
        ui->pb_readerStart->setEnabled(true);
        ui->pb_readerStop->setEnabled(false);
        ui->le_readoutStatus->setText("Stopped");
        ui->pb_usbReconnect->setEnabled(true);
        ui->pb_readDataPipe->setEnabled(true);
        m_d->tReaderStopped = QDateTime::currentDateTime();
    });

    ui->pb_readerStop->setEnabled(false);

    // Reset Reader Stats
    connect(ui->pb_readerResetStats, &QPushButton::clicked,
            this, [this] ()
    {
        auto now = QDateTime::currentDateTime();
        m_d->tReaderStarted = now;
        m_d->tReaderStopped = {};
        m_d->tLastUpdate    = now;
        m_d->prevReaderStats = {};
        m_d->dataReader->resetStats();
    });

    // Request that the reader copies and send out the next buffer it receives.
    connect(ui->pb_readerRequestBuffer, &QPushButton::clicked,
            this, [this] ()
    {
        m_d->dataReader->requestNextBuffer();
    });

    connect(m_d->dataReader, &MVLCDataReader::bufferReady,
            this, [this] (QVector<u8> buffer)
    {
        logMessage(QString("Received data buffer containing %1 words (%2 bytes).")
                   .arg(buffer.size() / sizeof(u32))
                   .arg(buffer.size()));

        int maxWords = ui->spin_logReaderBufferMaxWords->value();
        int maxBytes = maxWords > 0 ? maxWords * sizeof(u32) : buffer.size();
        maxBytes = std::min(maxBytes, buffer.size());

        logMessage(QString(">>> First %1 data words:").arg(maxBytes / sizeof(u32)));

        BufferIterator iter(buffer.data(), maxBytes);
        ::logBuffer(iter, [this] (const QString &line)
        {
            logMessage(line);
        });

        logMessage(QString("<<< End of buffer log"));
    });

    connect(m_d->dataReader, &MVLCDataReader::message,
            this, [this] (const QString &msg)
    {
        logMessage("Readout Thread: " + msg);
    });

    //
    // Periodic updates
    //
    auto updateTimer = new QTimer(this);
    updateTimer->setInterval(1000);

    // Pull ReaderStats from MVLCDataReader
    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        auto stats = m_d->dataReader->getStats();
        auto &labels = m_d->readerStatLabels;

        for (int counterType = 0;
             counterType < ReaderStats::CountersCount;
             counterType++)
        {
            QString text;
            size_t value = stats.counters[counterType];

            if (counterType == ReaderStats::TotalBytesReceived)
            {
                text = (QString("%1 B, %2 MB")
                        .arg(value)
                        .arg(value / (double)Megabytes(1)));
            }
            else
            {
                text = QString::number(stats.counters[counterType]);
            }

            m_d->readerStatLabels[counterType]->setText(text);
        }

        auto endTime = (m_d->readoutThread.isRunning()
                        ?  QDateTime::currentDateTime()
                        : m_d->tReaderStopped);

        s64 secondsElapsed = m_d->tReaderStarted.msecsTo(endTime) / 1000.0;
        auto durationString = makeDurationString(secondsElapsed);

        m_d->l_statRunDuration->setText(durationString);

        ReaderStats &prevStats = m_d->prevReaderStats;

        double dt = (m_d->tLastUpdate.isValid()
                     ? m_d->tLastUpdate.msecsTo(endTime)
                     : m_d->tReaderStarted.msecsTo(endTime)) / 1000.0;

        u64 deltaBytesRead = calc_delta0(
            stats.counters[ReaderStats::TotalBytesReceived],
            prevStats.counters[ReaderStats::TotalBytesReceived]);

        double bytesPerSecond = deltaBytesRead / dt;
        double mbPerSecond = bytesPerSecond / Megabytes(1);
        if (std::isnan(mbPerSecond))
            mbPerSecond = 0.0;

        m_d->l_statReadRate->setText(QString("%1 MB/s").arg(mbPerSecond));

        m_d->prevReaderStats = stats;
        m_d->tLastUpdate = QDateTime::currentDateTime();
    });

    // Poll the read queue size for both pipes
    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        u32 cmdQueueSize = 0;
        u32 dataQueueSize = 0;

        get_read_queue_size(&m_d->mvlc->getImpl(), CommandPipe, cmdQueueSize);
        get_read_queue_size(&m_d->mvlc->getImpl(), DataPipe, dataQueueSize);

        ui->le_usbCmdReadQueueSize->setText(QString::number(cmdQueueSize));
        ui->le_usbDataReadQueueSize->setText(QString::number(dataQueueSize));
    });

    updateTimer->start();

    // final init code
    resize(1000, 860);


    // Code to run on entering the event loop
    QTimer::singleShot(0, [this]() {
        this->raise(); // Raise this main window
        m_d->mvlc->connect();
    });
}

MVLCDevGUI::~MVLCDevGUI()
{
    m_d->dataReader->stop();
    m_d->readoutThread.quit();
    m_d->readoutThread.wait();
}

void MVLCDevGUI::logMessage(const QString &msg)
{
    ui->te_log->appendPlainText(msg);
    auto bar = ui->te_log->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MVLCDevGUI::logBuffer(const QVector<u32> &buffer, const QString &info)
{
    QStringList strBuffer;
    strBuffer.reserve(buffer.size() + 2);

    strBuffer << QString(">>> %1, size=%2").arg(info).arg(buffer.size());

    for (int i = 0; i < buffer.size(); i++)
    {
        u32 value = buffer.at(i);
        auto str = QString("%1: 0x%2 (%3 dec)")
            .arg(i, 3)
            .arg(value, 8, 16, QLatin1Char('0'))
            .arg(value)
            ;

        strBuffer << str;
    }

    strBuffer << "<<< " + info;

    ui->te_log->appendPlainText(strBuffer.join("\n"));
}

void MVLCDevGUI::clearLog()
{
    ui->te_log->clear();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<QVector<u8>>("QVector<u8>");

    MVLCDevGUI gui;
    gui.show();

    return app.exec();
}
