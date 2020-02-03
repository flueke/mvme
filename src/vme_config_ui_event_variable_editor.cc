#include "vme_config_ui_event_variable_editor.h"

#include "qt_util.h"
#include "util/qt_logview.h"
#include "vme_config_ui_variable_editor.h"
#include "vme_script_variables.h"

#include <QCheckBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QPushButton>

// react to variable modifications: add, remove, edit
// on edit figure out which

struct EventVariableEditor::Private
{
    EventVariableEditor *q;
    EventConfig *eventConfig;
    QLineEdit *le_eventName;
    VariableEditorWidget *varEditor;
    QPlainTextEdit *logView;
    QCheckBox *cb_autoRun;

    void loadFromEvent();
};

EventVariableEditor::EventVariableEditor(EventConfig *eventConfig, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    *d = {};
    d->q = this;
    d->eventConfig = eventConfig;
    d->varEditor = new VariableEditorWidget;
    d->logView = make_logview().release();

    d->le_eventName = new QLineEdit;
    d->le_eventName->setReadOnly(true);
    {
        auto pal = d->le_eventName->palette();
        pal.setBrush(QPalette::Base, QColor(239, 235, 231));
        d->le_eventName->setPalette(pal);
    }

    d->cb_autoRun = new QCheckBox("Auto-run dependent module scripts on modification");

    auto topFormLayout = new QFormLayout;
    topFormLayout->addRow("Event", d->le_eventName);
    topFormLayout->addRow(d->cb_autoRun);

    auto splitter = new QSplitter;
    splitter->addWidget(d->varEditor);
    splitter->addWidget(d->logView);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    bb->button(QDialogButtonBox::Apply)->setText("Apply and run");

    auto widgetLayout = make_vbox(this);
    widgetLayout->addLayout(topFormLayout);
    widgetLayout->addWidget(splitter, 1);
    widgetLayout->addWidget(bb);

    connect(d->varEditor, &VariableEditorWidget::variabledAdded,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableAdded:" << varName << var.value;
            });

    connect(d->varEditor, &VariableEditorWidget::variableValueChanged,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableValueChanged:" << varName << var.value;
            });

    connect(d->varEditor, &VariableEditorWidget::variableDeleted,
            this, [this] (const QString &varName)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableDeleted:" << varName;
            });

    resize(800, 600);
    d->loadFromEvent();
}

EventVariableEditor::~EventVariableEditor()
{
}

void EventVariableEditor::Private::loadFromEvent()
{
    q->setWindowTitle(QSL("Variable Editor for Event '%1'")
                      .arg(eventConfig->objectName()));

    le_eventName->setText(eventConfig->objectName());
    varEditor->setVariables(eventConfig->getVariables());
}
