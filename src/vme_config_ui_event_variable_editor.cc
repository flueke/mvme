#include "vme_config_ui_event_variable_editor.h"

#include "qt_util.h"
#include "util/qt_logview.h"
#include "vme_config_scripts.h"
#include "vme_config_ui_variable_editor.h"
#include "vme_script.h"
#include "vme_script_variables.h"

#include <QCheckBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QPushButton>
#include <qnamespace.h>

struct InitScriptCandidate
{
    VMEScriptConfig *initScript;

    // The names of changed variables which affect the script
    QSet<QString> affectedVarNames;

    // The names of changed variables referenced by the script but overriden by
    // the scripts own variable table.
    QSet<QString> scriptOverrides;

    // The names of changed variables referenced by the script but overriden by
    // the modules variable table.
    QSet<QString> moduleOverrides;

    bool isAffected() const { return !affectedVarNames.isEmpty(); }
};

struct EventVariableEditor::Private
{
    EventVariableEditor *q;
    EventVariableEditor::RunScriptCallback runScriptCallback;
    EventConfig *eventConfig;
    QLineEdit *le_eventName;
    VariableEditorWidget *varEditor;
    QPlainTextEdit *logView;
    QCheckBox *cb_autoRun;
    QCheckBox *cb_showInternalVariables;
    QPushButton *pb_runScripts;
    QDialogButtonBox *bb;

    QSet<QString> changedVariableNames;
    QSet<QString> deletedVariableNames;

    QVector<InitScriptCandidate> getAffectedScriptCandidates() const;
    void logAffectedScripts();
    void runAffectedScripts();
    void loadFromEvent();
    void save();
    void saveAndClose();
};

// Note about why focus policy is manually set for some of the Widgets:
//
// When modifying a variables value in the table widget and there are module
// init scripts depending on the variable and the auto-run option is checked,
// then the object actually executing the init scripts displays a
// QProgressDialog for each of the scripts.
// This means the variable editor loses and regains focus. Some of the time
// changing the same variable value again by typing on the keyboard works but
// at other times keyboard focus was taken away from the table widget.
// Setting most of the other widgets focus policy to Qt::NoFocus seems to fix
// this.

EventVariableEditor::EventVariableEditor(
    EventConfig *eventConfig,
    RunScriptCallback runScriptCallback,
    QWidget *parent)
: QWidget(parent)
, d(std::make_unique<Private>())
{
    assert(runScriptCallback);

    *d = {};
    d->q = this;
    d->runScriptCallback = runScriptCallback;
    d->eventConfig = eventConfig;
    d->varEditor = new VariableEditorWidget;
    d->logView = make_logview().release();

    d->le_eventName = new QLineEdit;
    d->le_eventName->setReadOnly(true);
    d->le_eventName->setFocusPolicy(Qt::NoFocus);
    {
        auto pal = d->le_eventName->palette();
        pal.setBrush(QPalette::Base, QColor(239, 235, 231));
        d->le_eventName->setPalette(pal);
    }

    d->cb_autoRun = new QCheckBox(QSL("Auto-run dependent module init scripts on modification"));
    d->cb_autoRun->setChecked(true);
    d->cb_autoRun->setFocusPolicy(Qt::NoFocus);

    d->cb_showInternalVariables = new QCheckBox(QSL("Show system and internal variables"));
    d->cb_showInternalVariables->setChecked(false);
    d->cb_showInternalVariables->setFocusPolicy(Qt::NoFocus);

    d->pb_runScripts = new QPushButton(QIcon(":/script-run.png"), "&Run Module Init Scripts");
    d->pb_runScripts->setFocusPolicy(Qt::NoFocus);
    auto pb_runScriptsLayout = make_hbox<0, 0>();
    pb_runScriptsLayout->addWidget(d->pb_runScripts);
    pb_runScriptsLayout->addStretch(1);

    auto topGroupBox = new QGroupBox;
    auto topFormLayout = new QFormLayout(topGroupBox);
    topFormLayout->setContentsMargins(2, 2, 2, 2);
    topFormLayout->addRow("Event", d->le_eventName);
    topFormLayout->addRow(d->cb_autoRun);
    topFormLayout->addRow(d->cb_showInternalVariables);
    topFormLayout->addRow(pb_runScriptsLayout);

    auto leftGroupBox = new QGroupBox(QSL("Event Variables"));
    auto leftGroupBoxLayout = make_vbox(leftGroupBox);
    leftGroupBoxLayout->addWidget(d->varEditor);

    auto rightGroupBox = new QGroupBox(QSL("Log View"));
    auto rightGroupBoxLayout = make_vbox(rightGroupBox);
    rightGroupBoxLayout->addWidget(d->logView);

    auto splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(leftGroupBox);
    splitter->addWidget(rightGroupBox);

    d->bb = new QDialogButtonBox(QDialogButtonBox::Apply |
                                 QDialogButtonBox::Ok |
                                 QDialogButtonBox::Cancel);
    //d->bb->button(QDialogButtonBox::Apply)->setText(QSL("Save"));
    //d->bb->button(QDialogButtonBox::Save)->setText(QSL("Save and Close"));

    auto widgetLayout = make_vbox<4, 4>(this);
    widgetLayout->addWidget(topGroupBox);
    widgetLayout->addWidget(splitter, 1);
    widgetLayout->addWidget(d->bb);

    // VariableEditorWidget signals
    connect(d->varEditor, &VariableEditorWidget::variabledAdded,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableAdded:" << varName << var.value;
                if (var)
                {
                    setWindowModified(true);
                    d->changedVariableNames.insert(varName);
                    d->logAffectedScripts();
                    if (d->cb_autoRun->isChecked())
                        d->runAffectedScripts();
                }
            });

    connect(d->varEditor, &VariableEditorWidget::variableValueChanged,
            this, [this] (const QString &varName, const vme_script::Variable &var)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableValueChanged:" << varName << var.value;

                setWindowModified(true);
                d->changedVariableNames.insert(varName);
                d->logAffectedScripts();
                if (d->cb_autoRun->isChecked())
                    d->runAffectedScripts();
            });

#if 0
    connect(d->varEditor, &VariableEditorWidget::variableDeleted,
            this, [this] (const QString &varName)
            {
                qDebug() << __PRETTY_FUNCTION__ << "variableDeleted:" << varName;
            });
#endif

    // run scripts button
    connect(d->pb_runScripts, &QPushButton::clicked,
            this, [this] () { d->runAffectedScripts(); });


    // QDialogButtonBox signals
    connect(d->bb, &QDialogButtonBox::clicked,
            this, [this] (QAbstractButton *button)
            {
                if (button == d->bb->button(QDialogButtonBox::Apply))
                    d->save();
                else if (button == d->bb->button(QDialogButtonBox::Ok))
                    d->saveAndClose();
                else if (button == d->bb->button(QDialogButtonBox::Cancel))
                    this->close();
                else
                    assert(false);
            });

    // show/hide interal variables
    connect(d->cb_showInternalVariables, &QCheckBox::stateChanged,
            this, [this] (int state)
            {
                d->varEditor->setHideInternalVariables(!(state == Qt::Checked));
            });

    resize(1000, 800);
    d->loadFromEvent();
}

EventVariableEditor::~EventVariableEditor()
{
}

QVector<InitScriptCandidate> EventVariableEditor::Private::getAffectedScriptCandidates() const
{
    QVector<InitScriptCandidate> result;

    for (auto moduleConfig: eventConfig->getModuleConfigs())
    {
        auto moduleDefinedNames = moduleConfig->getVariables().symbolNameSet();
        moduleDefinedNames.intersect(changedVariableNames);

        for (auto initScript: moduleConfig->getInitScripts())
        {
            InitScriptCandidate candidate = {};
            candidate.initScript = initScript;

            try
            {
                auto scriptReferencedNames = vme_script::collect_variable_references(initScript->getScriptContents());
                auto effectiveChangedNames = changedVariableNames;
                effectiveChangedNames.intersect(scriptReferencedNames);

                auto scriptDefinedNames = initScript->getVariables().symbolNameSet();
                for (auto name: scriptDefinedNames)
                {
                    if (effectiveChangedNames.contains(name))
                        candidate.scriptOverrides.insert(name);
                }
                effectiveChangedNames.subtract(scriptDefinedNames); // remove vars defined in the innermost script scope

                for (auto name: moduleDefinedNames)
                {
                    if (effectiveChangedNames.contains(name))
                        candidate.moduleOverrides.insert(name);
                }
                effectiveChangedNames.subtract(moduleDefinedNames); // remove vars defined at module scope

                candidate.affectedVarNames = effectiveChangedNames;

                result.push_back(candidate);
            }
            catch (const vme_script::ParseError &e)
            { }
        }
    }

    return result;
}

void EventVariableEditor::Private::logAffectedScripts()
{
    auto logger = [this](const QString &str)
    {
        logView->appendPlainText(str);
    };

    logView->clear();

    auto sortedVarNames = changedVariableNames.toList();
    std::sort(std::begin(sortedVarNames), std::end(sortedVarNames));
    auto symtab = varEditor->getVariables();

    logger(QSL("Modified variables: "));
    for (auto varName: sortedVarNames)
    {
        logger(QSL("  %1 = '%2'").arg(varName).arg(symtab[varName].value));
    }
    logger(QSL(""));

    auto candidates = getAffectedScriptCandidates();

    if (std::find_if(std::begin(candidates), std::end(candidates),
                     [] (const InitScriptCandidate &c)
                     {
                         return c.isAffected();
                     }) != std::end(candidates))
    {
        logView->appendPlainText("Module init scripts affected by variable changes:");

        for (const auto &c: candidates)
        {
            if (c.isAffected())
            {
                logView->appendPlainText(QSL("+ ") + c.initScript->getObjectPath());
                logView->appendPlainText(QSL("   vars: ") + c.affectedVarNames.toList().join(", "));
            }
        }
    }

    if (std::find_if(std::begin(candidates), std::end(candidates),
                     [] (const InitScriptCandidate &c)
                     {
                         return !c.scriptOverrides.isEmpty() || !c.moduleOverrides.isEmpty();
                     }) != std::end(candidates))
    {
        logView->appendPlainText("Unaffected init scripts:");

        for (const auto &c: candidates)
        {
            if (!c.scriptOverrides.isEmpty())
            {
                logView->appendPlainText(QSL("- ") + c.initScript->getObjectPath());
                logView->appendPlainText(QSL("   shadowed by a script variable: ") + c.scriptOverrides.toList().join(", "));
            }
            else if (!c.moduleOverrides.isEmpty())
            {
                logView->appendPlainText(QSL("- ") + c.initScript->getObjectPath());
                logView->appendPlainText(QSL("   shadowed by a module variable: ") + c.moduleOverrides.toList().join(", "));
            }
        }
    }
}

void EventVariableEditor::Private::runAffectedScripts()
{
    auto candidates = getAffectedScriptCandidates();

    if (std::find_if(std::begin(candidates), std::end(candidates),
                     [] (const InitScriptCandidate &c)
                     {
                         return c.isAffected();
                     }) == std::end(candidates))
    {
        return;
    }

    auto logger = [this](const QString &str)
    {
        logView->appendPlainText(str);
    };

    logger("");

    auto modifiedSymbolTable = varEditor->getVariables();
    bool seenScriptError = false;

    for (auto &c: candidates)
    {
        if (!c.isAffected())
            continue;

        // Collect the unmodified symbol tables for the script
        auto symtabsWithObject = mesytec::mvme::collect_symbol_tables_with_source(c.initScript);

        // Replace the symbol table coming from the parent EventConfig with the
        // modified one.
        for (auto &p: symtabsWithObject)
        {
            if (p.second == this->eventConfig)
            {
                p.first = modifiedSymbolTable;
                break;
            }
        }

        auto flatSymbolTables = mesytec::mvme::convert_to_symboltables(symtabsWithObject);

        // Determine the module base address
        u32 moduleBaseAddress = 0u;
        if (auto moduleConfig = qobject_cast<ModuleConfig *>(c.initScript->parent()))
            moduleBaseAddress = moduleConfig->getBaseAddress();

        try
        {
            // Parse using the modified symbol tables.
            auto parsedScript = vme_script::parse(
                c.initScript->getScriptContents(), flatSymbolTables, moduleBaseAddress);

            logger(QSL("Running script \"%1\"").arg(c.initScript->getObjectPath()));

            auto indented_logger = [logger] (const QString &str) { logger(QSL("  ") + str); };
            auto results = runScriptCallback(parsedScript, indented_logger);

            for (auto result: results)
            {
                if (results.size() > 1 && results[0].error.error() != VMEError::NotOpen)
                    indented_logger(format_result(result));

                if (result.error.isError())
                    seenScriptError = true;
            }
        }
        catch (const vme_script::ParseError &e)
        {
            logger(QSL("Error parsing script \"%1\": %2")
                   .arg(c.initScript->getObjectPath())
                   .arg(e.toString()));
            seenScriptError = true;
        }
    }

    if (!seenScriptError)
        changedVariableNames.clear();
}

void EventVariableEditor::Private::loadFromEvent()
{
    q->setWindowTitle(QSL("Variable Editor for Event '%1' [*]")
                      .arg(eventConfig->objectName()));

    le_eventName->setText(eventConfig->objectName());
    varEditor->setVariables(eventConfig->getVariables());
    varEditor->setHideInternalVariables(
        !(cb_showInternalVariables->checkState() == Qt::Checked));
    changedVariableNames.clear();
    logView->clear();
}

void EventVariableEditor::Private::save()
{
    eventConfig->setVariables(varEditor->getVariables());
    q->setWindowModified(false);
}

void EventVariableEditor::Private::saveAndClose()
{
    save();
    q->close();
}
