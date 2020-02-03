#include "vme_config_ui_event_variable_editor.h"

#include "qt_util.h"
#include "util/qt_logview.h"
#include "vme_config_ui_variable_editor.h"
#include "vme_script.h"
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
    QDialogButtonBox *bb;

    QSet<QString> changedVariableNames;

    QVector<VMEScriptConfig *> getAffectedScripts();
    void logAffectedScripts();
    void runAffectedScripts();
    void loadFromEvent();
    void saveAndRun();
    void saveAndClose();
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

    d->bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    d->bb->button(QDialogButtonBox::Save)->setText("Save and Close");
    d->bb->button(QDialogButtonBox::Apply)->setText("Run Scripts");

    auto widgetLayout = make_vbox(this);
    widgetLayout->addLayout(topFormLayout);
    widgetLayout->addWidget(splitter, 1);
    widgetLayout->addWidget(d->bb);

    // VariableEditorWidget signals
    connect(d->varEditor, &VariableEditorWidget::variabledAdded,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableAdded:" << varName << var.value;
                d->changedVariableNames.insert(varName);
                d->logAffectedScripts();
            });

    connect(d->varEditor, &VariableEditorWidget::variableValueChanged,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableValueChanged:" << varName << var.value;

                d->changedVariableNames.insert(varName);
                d->logAffectedScripts();

            });

    connect(d->varEditor, &VariableEditorWidget::variableDeleted,
            this, [this] (const QString &varName)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableDeleted:" << varName;
            });

    // QDialogButtonBox signals
    connect(d->bb, &QDialogButtonBox::clicked,
            this, [this] (QAbstractButton *button)
            {
                if (button == d->bb->button(QDialogButtonBox::Save))
                    d->saveAndClose();
                else if (button == d->bb->button(QDialogButtonBox::Apply))
                    d->saveAndRun();
                else if (button == d->bb->button(QDialogButtonBox::Cancel))
                    this->close();
                else
                    assert(false);
            });

    resize(800, 600);
    d->loadFromEvent();
}

EventVariableEditor::~EventVariableEditor()
{
}

QVector<VMEScriptConfig *> EventVariableEditor::Private::getAffectedScripts()
{
    QVector<VMEScriptConfig *> affectedInitScripts;

    for (auto moduleConfig: eventConfig->getModuleConfigs())
    {
        for (auto initScript: moduleConfig->getInitScripts())
        {
            auto varRefs = vme_script::collect_variable_references(
                initScript->getScriptContents());
            if (changedVariableNames.intersects(varRefs))
                affectedInitScripts.push_back(initScript);
        }
    }

    return affectedInitScripts;
}

void EventVariableEditor::Private::logAffectedScripts()
{
    auto affectedInitScripts = getAffectedScripts();

    logView->clear();

    if (!affectedInitScripts.isEmpty())
    {
        logView->appendPlainText("Module Init Scripts affected by variable changes:");

        for (auto initScript: affectedInitScripts)
            logView->appendPlainText(QSL("*") + initScript->getObjectPath());
    }
}

void EventVariableEditor::Private::runAffectedScripts()
{
    auto affectedInitScripts = getAffectedScripts();

    if (affectedInitScripts.isEmpty())
        return;

    // TODO
    // collect symbol tables for each script
    // replace the events symbol table with the symbol table from the varEditor
    // parse the script using the modified symbol tables
    // emit runScript with the parsed script
}

void EventVariableEditor::Private::loadFromEvent()
{
    q->setWindowTitle(QSL("Variable Editor for Event '%1'")
                      .arg(eventConfig->objectName()));

    le_eventName->setText(eventConfig->objectName());
    varEditor->setVariables(eventConfig->getVariables());
    changedVariableNames.clear();
    logView->clear();
}

void EventVariableEditor::Private::saveAndRun()
{
}

void EventVariableEditor::Private::saveAndClose()
{
}
