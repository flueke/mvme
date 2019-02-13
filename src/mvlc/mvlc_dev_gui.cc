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

#include <iostream>

#include "ui_mvlc_dev_ui.h"

#include "mvlc_script.h"
#include "qt_util.h"
#include "vme_debug_widget.h"

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

//
// MVLCDataReader
//
MVLCDataReader::MVLCDataReader(QObject *parent)
    : QObject(parent)
    , m_doQuit(false)
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

MVLCDataReader::Stats MVLCDataReader::getStats() const
{
    Stats result;
    {
        QMutexLocker guard(&m_statsMutex);
        result = m_stats;
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

    while (!m_doQuit)
    {
        m_readBuffer.resize(ReadBufferSize);
        size_t bytesTransferred = 0u;

        auto error = read_bytes(&m_impl, DataPipe,
                                m_readBuffer.data(), m_readBuffer.size(),
                                &bytesTransferred);

        // FIXME: this is very, very slow
        m_readBuffer.resize(bytesTransferred);

        {
            QMutexLocker guard(&m_statsMutex);

            ++m_stats.numberOfAttemptedReads;
            m_stats.totalBytesReceived += bytesTransferred;

            if (error)
            {
                if (is_timeout(error))
                    ++m_stats.numberOfTimeouts;
                else
                    ++m_stats.numberOfErrors;
            }
        }
    }

    qDebug() << __PRETTY_FUNCTION__ << "left readout loop";

    emit stopped();
}

void MVLCDataReader::stop()
{
    m_doQuit = true;
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
    // FIXME: use an enum an an array of counter for the stats
    QVector<QLabel *> readerStatLabels;
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
        auto l = new QFormLayout(ui->frame_readerStats);

        auto statNames =
        {
            "totalBytesReceived",
            "numberOfAttemptedReads",
            "numberOfTimeouts",
            "numberOfErrors",
        };

        for (auto statName: statNames)
        {
            auto label = new QLabel();
            m_d->readerStatLabels.push_back(label);
            l->addRow(statName, label);
        }
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
            auto scriptText = ui->te_scriptInput->toPlainText();
            auto cmdList = mvlc::script::parse(scriptText);
            auto cmdBuffer = mvlc::script::to_mvlc_command_buffer(cmdList);

            if (ui->cb_scriptLogRequest->isChecked())
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
            res = read_response(&m_d->mvlc->getImpl(), SuperResponseHeaderType,
                                responseBuffer);
            if (!res)
            {
                logMessage("Error reading response from MVLC: " + res.toString());
                return;
            }

            if (ui->cb_scriptLogMirror->isChecked())
            {
                logBuffer(responseBuffer, "Mirror response from MVLC");
            }

            res = check_mirror(cmdBuffer, responseBuffer);

            if (!res)
            {
                logMessage("Error: mirror check failed: " + res.toString());
                return;
            }

            if (ui->cb_scriptReadStack->isChecked())
            {
                logMessage("Attempting to read stack response...");

                res = read_response(&m_d->mvlc->getImpl(), StackResponseHeaderType,
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
            logMessage("Script parse error: " + e.toString());
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
        // FIXME: this never gets called
        // what does get called in MVLCDataReader::readoutLoop
        // and it should be running in a different thread.
        // at least the UI is still responsive.
        //
        // What does happen here?
        // Thread starts, enters qt event loop, emit started,
        // this causes readoutLoop() to be run probably directly from the event
        // pump. This then means that no more processing of the emission of the
        // started signal() is done because the threads event pump is stuck in
        // our readoutLoop().

        qDebug() << "readout thread started";
        ui->pb_readerStart->setEnabled(false);
        ui->pb_readerStop->setEnabled(true);
        ui->le_readoutStatus->setText("Running");
    });

    connect(&m_d->readoutThread, &QThread::finished,
            this, [this] ()
    {
        qDebug() << "readout thread finished";
        ui->pb_readerStart->setEnabled(true);
        ui->pb_readerStop->setEnabled(false);
        ui->le_readoutStatus->setText("Stopped");
    });

    ui->pb_readerStop->setEnabled(false);

    auto updateTimer = new QTimer(this);
    updateTimer->setInterval(1000);

    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        auto stats = m_d->dataReader->getStats();
        auto &labels = m_d->readerStatLabels;

        labels[0]->setText(QString::number(stats.totalBytesReceived));
        labels[1]->setText(QString::number(stats.numberOfAttemptedReads));
        labels[2]->setText(QString::number(stats.numberOfTimeouts));
        labels[3]->setText(QString::number(stats.numberOfErrors));
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

    MVLCDevGUI gui;
    gui.show();

    return app.exec();
}
