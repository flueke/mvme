#include "mvlc/mvlc_dev_gui.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTimer>

#include <iostream>

#include "qt_util.h"
#include "vme_debug_widget.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

struct MVLCGuiContext
{
    ~MVLCGuiContext();
    void logMessage(const QString &str);

    mesytec::mvlc::usb::USB_Impl mvlc;
};

MVLCGuiContext::~MVLCGuiContext()
{
    // TODO: make sure all threads have been stopped
    close(mvlc);
}

void MVLCGuiContext::logMessage(const QString &str)
{
    std::cout << str.toStdString() << std::endl;
}

struct MVLCDevGUI::Private
{
    MVLCDevGUI *q;

    QWidget *centralWidget;
    QGridLayout *centralLayout;

    QToolBar *toolbar;
    QStatusBar *statusbar;
    QPlainTextEdit *te_scriptInput,
                   *te_scriptLog;

    QAction *act_showLog,
            *act_showVMEDebug,
            *act_loadScript
            ;

    QComboBox *combo_scriptType;

    //std::unique_ptr<QPlainTextEdit> logView;
};

MVLCDevGUI::MVLCDevGUI(QWidget *parent)
    : QMainWindow(parent)
    , m_d(std::make_unique<Private>())
{
    m_d->q = this;

    setObjectName(QSL("MVLC Dev Tool"));
    setWindowTitle(objectName());

    m_d->toolbar = new QToolBar(this);
    m_d->statusbar = new QStatusBar(this);
    m_d->centralWidget = new QWidget(this);
    m_d->centralLayout = new QGridLayout(m_d->centralWidget);

    setCentralWidget(m_d->centralWidget);
    addToolBar(m_d->toolbar);
    setStatusBar(m_d->statusbar);

    // logView
    //m_d->logView = std::make_unique<QPlainTextEdit>();
    //m_d->logView->show();
    //{
    //    auto lv = m_d->logView.get();
    //    lv->setWindowTitle("Log View");
    //    lv->setFont(make_monospace_font());
    //    lv->setReadOnly(true);
    //}

    // script input, raw buffer out, script options below
    {
        m_d->te_scriptInput = new QPlainTextEdit();
        m_d->te_scriptLog = new QPlainTextEdit();

        for (auto te: { m_d->te_scriptInput, m_d->te_scriptLog })
        {
            te->setFont(make_monospace_font());
        }

        auto splitter = new QSplitter(this);

        {
            auto w = new QWidget(this);
            auto l = make_layout<QVBoxLayout>(w);
            l->addWidget(new QLabel("Script Input"));
            l->addWidget(m_d->te_scriptInput);
            splitter->addWidget(w);
        }

        {
            auto w = new QWidget(this);
            auto l = make_layout<QVBoxLayout>(w);
            l->addWidget(new QLabel("Output"));
            l->addWidget(m_d->te_scriptLog);
            splitter->addWidget(w);
        }

        auto controlsWidget = new QWidget(this);
        auto controlsLayout = new QHBoxLayout(controlsWidget);

        m_d->centralLayout->addWidget(splitter, 0, 0);
        m_d->centralLayout->addWidget(controlsWidget, 1, 0);
        m_d->centralLayout->setRowStretch(0, 1);
    }

    // Code to run on entering the event loop
    QTimer::singleShot(0, [this]() {
        this->raise(); // Raise this main window
    });
}

MVLCDevGUI::~MVLCDevGUI()
{
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MVLCDevGUI gui;
    gui.show();

    return app.exec();
}
