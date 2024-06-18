/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "analysis_ui_p.h"

#include <array>
#include <limits>
#include <boost/range/adaptor/indexed.hpp>

#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSignalMapper>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>

#include <memory>
#include <set>

#include <mesytec-mvlc/mesytec-mvlc.h>

#include "a2/a2_exprtk.h"
#include "a2_adapter.h"
#include "analysis_util.h"
#include "data_extraction_widget.h"
#include "data_filter_edit.h"
#include "data_filter.h"
#include "exportsink_codegen.h"
#include "globals.h"
#include "gui_util.h"
#include "histo_util.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "../mvme_qthelp.h"
#include "qt_util.h"
#include "rate_monitor_plot_widget.h"
#include "ui_eventwidget_p.h"
#include "util/qt_font.h"
#include "util/qt_layouts.h"
#include "util/variablify.h"
#include "vme_config.h"

using boost::adaptors::indexed;

namespace
{
    QDoubleSpinBox *make_calibration_spinbox(const QString &specialValueText = QSL("not set"),
                                             QWidget *parent = nullptr)
    {
        auto result = new QDoubleSpinBox(parent);

        result->setDecimals(8);
        result->setMinimum(-1e20);
        result->setMaximum(+1e20);
        result->setValue(result->minimum());
        result->setSpecialValueText(specialValueText);

        return result;
    }
}

namespace analysis
{
namespace ui
{

//
// FilterNameListDialog
//
FilterNameListDialog::FilterNameListDialog(const QString &filterName, const QStringList &names,
                                           QWidget *parent)
    : QDialog(parent)
    , m_editor(new CodeEditor(this))
    , m_bb(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_names(names)
{
    setWindowTitle(QSL("Name List for data exractor '%1'").arg(filterName));
    auto layout = new QGridLayout(this);

    auto label = new QLabel(
            QSL("Names can be assigned to individual parameters of a Filter Extractor.\n"
                "These names will be visible in the analysis user interface and are "
                "available when using the EventServer to export readout data.\n"
                "Write one name per line (or separate them by spaces). Names should be "
                "valid C++ identifiers."
                ));
    label->setWordWrap(true);

    layout->addWidget(label, 0, 0);
    layout->addWidget(m_editor, 1, 0);
    layout->addWidget(m_bb, 2, 0);

    layout->setRowStretch(1, 1);

    connect(m_bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QString editorText = names.join("\n");
    m_editor->setPlainText(editorText);
}

void FilterNameListDialog::accept()
{
    QString editorText = m_editor->toPlainText();
    m_names = editorText.split(QRegExp("\\s+"));
    QDialog::accept();
}

//
// AddEditExtractorDialog
//

AddEditExtractorDialog::AddEditExtractorDialog(std::shared_ptr<Extractor> ex, ModuleConfig *moduleConfig,
                                               ObjectEditorMode mode, EventWidget *eventWidget)
    : ObjectEditorDialog(eventWidget)
    , m_ex(ex)
    , m_module(moduleConfig)
    , m_eventWidget(eventWidget)
    , m_mode(mode)
    , m_parameterNames(ex->getParameterNames())
{
    Q_ASSERT(m_ex);
    Q_ASSERT(moduleConfig);

    add_widget_close_action(this);

    auto dataSources = get_data_extractor_templates(moduleConfig->getModuleMeta().typeName);
    m_defaultExtractors.clear();

    for (auto source: dataSources)
    {
        if (auto extractor = std::dynamic_pointer_cast<Extractor>(source))
            m_defaultExtractors.push_back(extractor);
    }

    auto loadTemplateButton = new QPushButton(QIcon(QSL(":/document_import.png")),
                                              QSL("Load Filter Template"));
    auto loadTemplateLayout = new QHBoxLayout;
    loadTemplateLayout->setContentsMargins(0, 0, 0, 0);
    loadTemplateLayout->addWidget(loadTemplateButton);
    loadTemplateLayout->addStretch();

    connect(loadTemplateButton, &QPushButton::clicked,
            this, &AddEditExtractorDialog::runLoadTemplateDialog);

    le_name = new QLineEdit;
    m_filterEditor = new DataExtractionEditor(m_ex->getFilter().getSubFilters());
    m_filterEditor->setMinimumHeight(125);
    m_filterEditor->setMinimumWidth(550);

    m_spinCompletionCount = new QSpinBox;
    m_spinCompletionCount->setMinimum(1);
    m_spinCompletionCount->setMaximum(std::numeric_limits<int>::max());

    if (!m_ex->objectName().isEmpty())
    {
        le_name->setText(QString("%1").arg(m_ex->objectName()));
    }

    m_spinCompletionCount->setValue(m_ex->getRequiredCompletionCount());

    pb_editNameList = new QPushButton(QIcon(QSL(":/pencil.png")), QSL("Edit Name List"));
    cb_noAddedRandom = new QCheckBox("Do not add a random in [0.0, 1.0)");
    cb_noAddedRandom->setChecked(m_ex->getOptions() & Extractor::Options::NoAddedRandom);

    cb_isSignedValue = new QCheckBox("Extract signed data values (two's complement, highest data bit is sign bit)");
    cb_isSignedValue->setChecked(m_ex->getOptions() & Extractor::Options::HighestBitIsSignBit);

    m_optionsLayout = new QFormLayout;
    m_optionsLayout->addRow(QSL("Name"), le_name);
    m_optionsLayout->addRow(QSL("Required Completion Count"), m_spinCompletionCount);
    m_optionsLayout->addRow(QSL("Parameter Names"), pb_editNameList);
    m_optionsLayout->addRow(QSL("No Added Random"), cb_noAddedRandom);
    m_optionsLayout->addRow(QSL("Signed Value"), cb_isSignedValue);

    connect(pb_editNameList, &QPushButton::clicked,
            this, &AddEditExtractorDialog::editNameList);

    if (m_defaultExtractors.size())
    {
        m_optionsLayout->addRow(loadTemplateLayout);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditExtractorDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditExtractorDialog::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QVBoxLayout(this);

    layout->addWidget(m_filterEditor);
    layout->addLayout(m_optionsLayout);
    layout->addLayout(buttonBoxLayout);

    layout->setStretch(0, 1);

    switch (mode)
    {
        case ObjectEditorMode::New:
            {
                setWindowTitle(QString("New  %1").arg(m_ex->getDisplayName()));

                // Histogram generation and calibration
                m_gbGenHistograms = new QGroupBox(QSL("Generate Histograms"));
                m_gbGenHistograms->setCheckable(true);
                m_gbGenHistograms->setChecked(true);

                le_unit = new QLineEdit;

                spin_unitMin = new QDoubleSpinBox;
                spin_unitMin->setDecimals(8);
                spin_unitMin->setMinimum(-1e20);
                spin_unitMin->setMaximum(+1e20);
                spin_unitMin->setValue(0.0);

                spin_unitMax = new QDoubleSpinBox;
                spin_unitMax->setDecimals(8);
                spin_unitMax->setMinimum(-1e20);
                spin_unitMax->setMaximum(+1e20);
                spin_unitMax->setValue(1 << 16); // TODO: find a better default value. Maybe input dependent (upperLimit)

                auto genHistogramsLayout = new QHBoxLayout(m_gbGenHistograms);
                genHistogramsLayout->setContentsMargins(0, 0, 0, 0);
                auto calibInfoFrame = new QFrame;
                genHistogramsLayout->addWidget(calibInfoFrame);
                auto calibInfoLayout = new QFormLayout(calibInfoFrame);
                calibInfoLayout->addRow(QSL("Unit Label"), le_unit);
                calibInfoLayout->addRow(QSL("Unit Min"), spin_unitMin);
                calibInfoLayout->addRow(QSL("Unit Max"), spin_unitMax);

                connect(m_gbGenHistograms, &QGroupBox::toggled, this, [calibInfoFrame](bool checked) {
                    calibInfoFrame->setEnabled(checked);
                });

                m_optionsLayout->insertRow(m_optionsLayout->rowCount(), m_gbGenHistograms);

                // Load data from the first template into the gui
                applyTemplate(0);
            } break;

        case ObjectEditorMode::Edit:
            {
                setWindowTitle(QString("Edit %1").arg(m_ex->getDisplayName()));
            } break;
    }
}

AddEditExtractorDialog::~AddEditExtractorDialog()
{
}

void AddEditExtractorDialog::runLoadTemplateDialog()
{
    auto templateList = new QListWidget;

    for (auto &ex: m_defaultExtractors)
    {
        templateList->addItem(ex->objectName());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QSL("Load Extraction Filter Template"));
    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setDefault(true);
    bb->button(QDialogButtonBox::Ok)->setText(QSL("Load"));
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(templateList, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(templateList);
    layout->addWidget(bb);

    if (dialog.exec() == QDialog::Accepted)
    {
        applyTemplate(templateList->currentRow());
    }
}

/** Loads the template with the given \c index into the GUI.
 *
 * The index is an index into a vector of Extractor instances obtained from
 * get_data_extractor_templates() cached in m_defaultExtractors.
 *
 * Assumes that the filter name loaded from the templates does consists of
 * sections split by '.' The last section is assumed to be the filter name,
 * while the prior sections are replaced by the concrete modules intance name.
 */
void AddEditExtractorDialog::applyTemplate(int index)
{
    if (0 <= index && index < m_defaultExtractors.size())
    {
        auto tmpl = m_defaultExtractors[index];
        m_filterEditor->setSubFilters(tmpl->getFilter().getSubFilters());
        QString name = m_module->objectName() + QSL(".") + tmpl->objectName().section('.', -1);
        le_name->setText(name);
        m_spinCompletionCount->setValue(tmpl->getRequiredCompletionCount());
    }
}

void AddEditExtractorDialog::editNameList()
{
    auto name = le_name->text();
    if (name.isEmpty()) name = QSL("New Filter");
    FilterNameListDialog dialog(name, m_parameterNames, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_parameterNames = dialog.getNames();
    }
}

void AddEditExtractorDialog::accept()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getServiceProvider());

    m_ex->setObjectName(le_name->text());
    m_ex->setEventId(m_module->getEventId());
    m_ex->setModuleId(m_module->getId());
    m_filterEditor->apply();
    m_ex->getFilter().setSubFilters(m_filterEditor->m_subFilters);
    m_ex->setRequiredCompletionCount(m_spinCompletionCount->value());
    m_ex->setParameterNames(m_parameterNames);

    Extractor::Options::opt_t options = 0;

    if (cb_noAddedRandom->isChecked())
        options |= Extractor::Options::NoAddedRandom;

    if(cb_isSignedValue->isChecked())
        options |= Extractor::Options::HighestBitIsSignBit;

    m_ex->setOptions(options);

    auto analysis = m_eventWidget->getServiceProvider()->getAnalysis();

    switch (m_mode)
    {
        case ObjectEditorMode::New:
            {
                m_ex->setEventId(m_module->getEventId());
                m_ex->setModuleId(m_module->getId());

                bool genHistos = m_gbGenHistograms->isChecked();

                if (genHistos)
                {
                    auto rawDisplay = make_raw_data_display(
                        m_ex, spin_unitMin->value(), spin_unitMax->value(),
                        // FIXME: missing title
                        QString(), le_unit->text());

                    add_raw_data_display(analysis, m_module->getEventId(), m_module->getId(), rawDisplay);
                }
                else
                {
                    analysis->addSource(m_ex);
                }
            } break;

        case ObjectEditorMode::Edit:
            {
                analysis->setSourceEdited(m_ex);
            } break;
    }

    analysis->beginRun(Analysis::KeepState, m_eventWidget->getVMEConfig());

    QDialog::accept();
}

void AddEditExtractorDialog::reject()
{
    qDebug() << __PRETTY_FUNCTION__;
    //m_eventWidget->uniqueWidgetCloses();
    QDialog::reject();
}

QComboBox *make_event_selection_combo(
    const QList<EventConfig *> &eventConfigs,
    const OperatorPtr &op,
    const DirectoryPtr &destDir,
    QWidget *parent)
{
    auto combo = new QComboBox(parent);

    combo->addItem("<unspecified>", QUuid());

    for (const auto &eventConfig: eventConfigs)
        combo->addItem(eventConfig->objectName(), eventConfig->getId());

    if (!op->getEventId().isNull())
    {
        int idx = combo->findData(op->getEventId());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
    else if (destDir && !destDir->getEventId().isNull())
    {
        int idx = combo->findData(destDir->getEventId());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
    else if (combo->count() == 2)
    {
        // "unspecified" plus one single event from the vme config
        // -> select the existing event
        combo->setCurrentIndex(1);
    }
    else
        combo->setCurrentIndex(0);

    return combo;
}

//
// MultiHitExtractorDialog
//

struct MultiHitExtractorDialog::Private
{
    std::shared_ptr<MultiHitExtractor> ex;
    ModuleConfig *mod;
    ObjectEditorMode mode;
    EventWidget *eventWidget;

    QComboBox *combo_shape;
    QLineEdit *le_name;
    DataFilterEdit *le_filterEdit;
    QSpinBox *spin_maxHits;
    QCheckBox *cb_noAddedRandom,
              *cb_isSignedValue;
    QLabel *label_info;

    std::vector<std::shared_ptr<Extractor>> getTemplateExtractors()
    {
        assert(mod);

        // Get the list of Extractors which do have a single subFilter.
        std::vector<std::shared_ptr<Extractor>> extractors;

        for (auto source: get_data_extractor_templates(mod->getModuleMeta().typeName))
        {
            if (auto extractor = std::dynamic_pointer_cast<Extractor>(source))
            {
                if (extractor->getFilter().getSubFilterCount() == 1)
                {
                    extractors.emplace_back(extractor);
                }
            }
        }

        return extractors;
    }

    void fillExtractorFromGui(MultiHitExtractor &dest)
    {
        dest.setEventId(mod->getEventId());
        dest.setModuleId(mod->getId());

        dest.setObjectName(le_name->text());
        dest.setShape(static_cast<MultiHitExtractor::Shape>(combo_shape->currentData().toInt()));
        dest.setFilter(le_filterEdit->getFilter());
        dest.setMaxHits(spin_maxHits->value());

        MultiHitExtractor::Options::opt_t options = 0;

        if (cb_noAddedRandom->isChecked())
            options |= MultiHitExtractor::Options::NoAddedRandom;

        if (cb_isSignedValue->isChecked())
            options |= MultiHitExtractor::Options::HighestBitIsSignBit;

        dest.setOptions(options);
    }
};

MultiHitExtractorDialog::MultiHitExtractorDialog(
    const std::shared_ptr<MultiHitExtractor> &ex,
    ModuleConfig *mod,
    ObjectEditorMode mode,
    EventWidget *eventWidget)
    : ObjectEditorDialog(eventWidget)
    , d(std::make_unique<Private>())
{
    *d = {};

    d->ex = ex;
    d->mod = mod;
    d->mode = mode;
    d->eventWidget = eventWidget;

    d->combo_shape = new QComboBox;
    d->combo_shape->addItem("Array per hit", MultiHitExtractor::Shape::ArrayPerHit);
    d->combo_shape->addItem("Array per address", MultiHitExtractor::Shape::ArrayPerAddress);
    d->combo_shape->setCurrentIndex(d->combo_shape->findData(d->ex->getShape()));

    d->le_name = new QLineEdit;
    d->le_name->setText(d->ex->objectName());

    d->le_filterEdit = new DataFilterEdit;
    d->le_filterEdit->setFilter(d->ex->getFilter());

    auto loadTemplateButton = new QPushButton(QIcon(QSL(":/document_import.png")),
                                              QSL("Load Template"));

    connect(loadTemplateButton, &QPushButton::clicked,
            this, &MultiHitExtractorDialog::runLoadTemplateDialog);

    auto l_filter = make_hbox<2>();
    l_filter->addWidget(d->le_filterEdit);
    l_filter->addWidget(loadTemplateButton);

    d->spin_maxHits = new QSpinBox;
    d->spin_maxHits->setMinimum(1);
    d->spin_maxHits->setMaximum(16);
    d->spin_maxHits->setValue(d->ex->getMaxHits());

    d->cb_noAddedRandom = new QCheckBox("Do not add a random in [0.0, 1.0)");
    d->cb_noAddedRandom->setChecked(d->ex->getOptions() & Extractor::Options::NoAddedRandom);

    d->cb_isSignedValue = new QCheckBox("Extract signed data values (two's complement, highest data bit is sign bit)");
    d->cb_isSignedValue->setChecked(d->ex->getOptions() & Extractor::Options::HighestBitIsSignBit);

    d->label_info = new QLabel;

    auto label_compat = new QLabel(QSL("<b>Note</b>: MultiHit extractors are currently"
                                       " not compatible with the EventServer for data export!"));
    label_compat->setWordWrap(true);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto l = new QFormLayout(this);
    l->addRow("Name", d->le_name);
    l->addRow("Extraction Filter", l_filter);
    l->addRow("Max hits per address", d->spin_maxHits);
    l->addRow("Output Shape", d->combo_shape);
    l->addRow("No Added Random", d->cb_noAddedRandom);
    l->addRow(QSL("Signed Value"), d->cb_isSignedValue);
    l->addRow("Info", d->label_info);
    l->addRow(label_compat);
    l->addRow(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &MultiHitExtractorDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &MultiHitExtractorDialog::reject);

    connect(d->le_filterEdit, &QLineEdit::textChanged,
            this, &MultiHitExtractorDialog::updateWidget);
    connect(d->combo_shape, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MultiHitExtractorDialog::updateWidget);
    connect(d->spin_maxHits, qOverload<int>(&QSpinBox::valueChanged),
            this, &MultiHitExtractorDialog::updateWidget);

    switch (d->mode)
    {
        case ObjectEditorMode::New:
            {
                setWindowTitle(QString("New  %1").arg(d->ex->getDisplayName()));
                auto templates = d->getTemplateExtractors();
                if (!templates.empty())
                    applyTemplate(templates[0]);
            } break;

        case ObjectEditorMode::Edit:
            setWindowTitle(QString("Edit %1").arg(d->ex->getDisplayName()));
            break;
    }

    updateWidget();
}

MultiHitExtractorDialog::~MultiHitExtractorDialog()
{
}

void MultiHitExtractorDialog::accept()
{
    AnalysisPauser pauser(d->eventWidget->getServiceProvider());

    d->fillExtractorFromGui(*d->ex);

    auto analysis = d->eventWidget->getServiceProvider()->getAnalysis();

    switch (d->mode)
    {
        case ObjectEditorMode::New:
            analysis->addSource(d->ex);
            break;

        case ObjectEditorMode::Edit:
            analysis->setSourceEdited(d->ex);
            break;
    }

    analysis->beginRun(Analysis::KeepState, d->eventWidget->getVMEConfig());

    QDialog::accept();
}

void MultiHitExtractorDialog::reject()
{
    QDialog::reject();
}

void MultiHitExtractorDialog::runLoadTemplateDialog()
{
    auto extractors = d->getTemplateExtractors();

    auto templateList = new QListWidget;

    for (auto &ex: extractors)
    {
        templateList->addItem(ex->objectName());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QSL("Load Extraction Filter Template"));
    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setDefault(true);
    bb->button(QDialogButtonBox::Ok)->setText(QSL("Load"));
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(templateList, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(templateList);
    layout->addWidget(bb);

    if (dialog.exec() == QDialog::Accepted)
    {
        if (templateList->currentRow() < static_cast<s32>(extractors.size()))
        {
            auto ex = extractors[templateList->currentRow()];
            applyTemplate(ex);
        }
    }
}

void MultiHitExtractorDialog::applyTemplate(const std::shared_ptr<Extractor> &tmpl)
{
    assert(tmpl->getFilter().getSubFilterCount() == 1);
    d->le_filterEdit->setFilter(tmpl->getFilter().getSubFilters().at(0));
    QString name = d->mod->objectName() + QSL(".") + tmpl->objectName().section('.', -1);
    d->le_name->setText(name);
}

void MultiHitExtractorDialog::updateWidget()
{
    MultiHitExtractor mex = {};
    d->fillExtractorFromGui(mex);
    mex.beginRun({}, {});

    // subtract one to account for the hitCounts array.
    s32 arrayCount = mex.getNumberOfOutputs() - 1;
    s32 arraySize = 0u;

    if (auto outPipe = mex.getOutput(0))
        arraySize = outPipe->getSize();

    auto arrayText = arrayCount > 1 ? "arrays, each" : "array";

    d->label_info->setText(QSL("Will generate %1 output %2 of size %3.")
                       .arg(arrayCount)
                       .arg(arrayText)
                       .arg(arraySize));
}

//
// AddEditOperatorDialog
//

AddEditOperatorDialog::AddEditOperatorDialog(OperatorPtr op,
                                             s32 userLevel,
                                             ObjectEditorMode mode,
                                             const DirectoryPtr &destDir,
                                             EventWidget *eventWidget)
    : ObjectEditorDialog(eventWidget)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_mode(mode)
    , m_destDir(destDir)
    , m_eventWidget(eventWidget)
    , m_opConfigWidget(nullptr)
{
    // Create a combo box for selecting the event this operator should be part of.
    // Start with "unassigned", fill in the events form the vme config.
    // Disable the ok button until an event is selected.
    // Also if no event is selected yet and and input is connected the inputs
    // source eventid can be used to select an event.
    auto eventGroupBox = new QGroupBox(QSL("Parent Event"));
    {
        m_eventSelectionCombo = make_event_selection_combo(
            eventWidget->getVMEConfig()->getEventConfigs(), op, destDir);

        auto l = make_hbox(eventGroupBox);
        l->addWidget(m_eventSelectionCombo);

        connect(m_eventSelectionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &AddEditOperatorDialog::onOperatorValidityChanged);
    }

    // TODO (maybe): refactor this into some factory or table lookup based on
    // qmetaobject if there are more operator specific widgets to handle
    if (auto rms = qobject_cast<RateMonitorSink *>(op.get()))
    {
        m_opConfigWidget = new RateMonitorConfigWidget(
            rms, userLevel, eventWidget->getServiceProvider(), this);
    }
    else if (auto cal = qobject_cast<CalibrationMinMax *>(op.get()))
    {
        m_opConfigWidget = new CalibrationMinMaxConfigWidget(
            cal, userLevel, eventWidget->getServiceProvider(), this);
    }
    else
    {
        m_opConfigWidget = new OperatorConfigurationWidget(
            op.get(), userLevel, eventWidget->getServiceProvider(), this);
    }

    switch (mode)
    {
        case ObjectEditorMode::New:
            setWindowTitle(QString("New  %1").arg(m_op->getDisplayName()));
            // This is a new operator, so either the name is empty or was auto generated.
            m_opConfigWidget->setNameEdited(false);
            break;

        case ObjectEditorMode::Edit:
            setWindowTitle(QString("Edit %1").arg(m_op->getDisplayName()));
            // We're editing an operator so we assume the name has been specified by the user.
            m_opConfigWidget->setNameEdited(true);
            break;
    }

    add_widget_close_action(this);

    connect(m_opConfigWidget, &AbstractOpConfigWidget::validityMayHaveChanged,
            this, &AddEditOperatorDialog::onOperatorValidityChanged);


    //
    // Slotgrid creation
    //

    const s32 slotCount = m_op->getNumberOfSlots();

    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        Slot *slot = m_op->getSlot(slotIndex);
        // Record the current slot input
        m_slotBackups.push_back({slot->inputPipe, slot->paramIndex});
    }

    auto slotFrame = new QFrame;
    auto slotGrid = new QGridLayout(slotFrame);
    m_slotGrid = slotGrid;
    slotGrid->setContentsMargins(0, 0, 0, 0);

    if (slotCount == 1 && !op->hasVariableNumberOfSlots())
    {
        slotGrid->setColumnStretch(0, 1);
        slotGrid->setColumnStretch(1, 0);
    }

    if (slotCount > 1 || op->hasVariableNumberOfSlots())
    {
        slotGrid->setColumnStretch(0, 0);
        slotGrid->setColumnStretch(1, 1);
        slotGrid->setColumnStretch(2, 0);
    }

    auto slotGroupBox = new QGroupBox(slotCount > 1 ? QSL("Inputs") : QSL("Input"));
    auto slotGroupBoxLayout = new QGridLayout(slotGroupBox);
    slotGroupBoxLayout->setContentsMargins(2, 2, 2, 2);
    slotGroupBoxLayout->addWidget(slotFrame, 0, 0, 1, 2);

    if (op->hasVariableNumberOfSlots())
    {
        m_addSlotButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
        m_addSlotButton->setToolTip(QSL("Add input"));

        m_removeSlotButton = new QPushButton(QIcon(QSL(":/list_remove.png")), QString());
        m_removeSlotButton->setToolTip(QSL("Remove last input"));
        m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);

        connect(m_addSlotButton, &QPushButton::clicked, this, [this] () {
            m_op->addSlot();
            repopulateSlotGrid();
            endInputSelect();
            m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);
        });

        connect(m_removeSlotButton, &QPushButton::clicked, this, [this] () {
            if (m_op->getNumberOfSlots() > 1)
            {
                m_op->removeLastSlot();
                repopulateSlotGrid();
                endInputSelect();
            }
            m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);
        });

        auto buttonLayout = new QHBoxLayout;
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->addStretch();
        buttonLayout->addWidget(m_addSlotButton);
        buttonLayout->addWidget(m_removeSlotButton);
        slotGroupBoxLayout->addLayout(buttonLayout, 1, 0, 1, 2);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditOperatorDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditOperatorDialog::reject);
    for (auto button_: m_buttonBox->buttons())
    {
        if (auto button = qobject_cast<QPushButton *>(button_))
        {
            button->setAutoDefault(false);
            button->setDefault(false);
            button->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        }
    }

    // Hack so that Qt does not focus the Ok or Cancel buttons when opening the
    // dialog.
    auto invisibleDefaultButton = new QPushButton;
    invisibleDefaultButton->setDefault(true);
    invisibleDefaultButton->setAutoDefault(true);

    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(invisibleDefaultButton);
    buttonBoxLayout->addWidget(m_buttonBox);

    invisibleDefaultButton->hide();

    auto layout = new QGridLayout(this);
    //layout->setContentsMargins(2, 2, 2, 2);

    s32 row = 0;
    // row, col, rowSpan, colSpan
    layout->addWidget(eventGroupBox, row++, 0);
    layout->addWidget(slotGroupBox, row++, 0);
    layout->addWidget(m_opConfigWidget, row++, 0, 1, 2);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(2, 1); // m_opConfigWidget may stretch

    // The widget is complete, now populate the slot grid.
    repopulateSlotGrid();
}

QString make_input_source_text(Slot *slot)
{
    Q_ASSERT(slot);

    return slot ? make_input_source_text(slot->inputPipe, slot->paramIndex) : QString();
}

QString make_input_source_text(Pipe *inputPipe, s32 paramIndex)
{
    Q_ASSERT(inputPipe);
    Q_ASSERT(inputPipe->source);

    QString result;

    if (inputPipe && inputPipe->source)
    {
        auto inputSource = inputPipe->source;
        result = inputSource->objectName();

        if (inputSource->hasVariableNumberOfOutputs())
        {
            result += "." + inputSource->getOutputName(inputPipe->sourceOutputIndex);
        }

        if (paramIndex != Slot::NoParamIndex && inputPipe->getSize() > 1)
        {
            result = (QSL("%1[%2]")
                          .arg(result)
                          .arg(paramIndex));
        }
    }

    return result;
}

void AddEditOperatorDialog::repopulateSlotGrid()
{
    // Clear the grid and the select buttons
    {
        while (QLayoutItem *child = m_slotGrid->takeAt(0))
        {
            if (auto widget = child->widget())
                delete widget;
            delete child;
        }

        Q_ASSERT(m_slotGrid->count() == 0);

        m_selectButtons.clear();
    }

    const s32 slotCount = m_op->getNumberOfSlots();
    s32 row = 0;
    s32 userLevel = m_userLevel;

    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        Slot *slot = m_op->getSlot(slotIndex);

        auto selectButton = new QPushButton(QSL("<select>"));
        selectButton->setCheckable(true);
        selectButton->setMouseTracking(true);
        selectButton->installEventFilter(this);
        m_selectButtons.push_back(selectButton);

        connect(selectButton, &QPushButton::toggled,
                this, [this, slot, slotIndex, userLevel](bool checked) {
            // Cancel any previous input selection. Has no effect if no input selection was active.
            m_eventWidget->endSelectInput();

            if (checked)
            {
                for (s32 buttonIndex = 0; buttonIndex < m_selectButtons.size(); ++buttonIndex)
                {
                    if (buttonIndex != slotIndex)
                    {
                        m_selectButtons[buttonIndex]->setChecked(false);
                    }
                }

                /* Tell the EventWidget that we want input for the current slot.
                 * The lambda is the callback for the EventWidget. This means
                 * inputSelected() will be called with the current slotIndex
                 * once input selection is complete. */
                m_eventWidget->selectInputFor(
                    slot, userLevel,
                    [this] (Slot *destSlot, Pipe *selectedPipe, s32 selectedParamIndex) {

                    this->inputSelectedForSlot(destSlot, selectedPipe, selectedParamIndex);
                });
            }

            m_inputSelectActive = checked;
        });

        auto clearButton  = new QPushButton(QIcon(":/dialog-close.png"), QString());

        connect(clearButton, &QPushButton::clicked, this, [this, slot, slotIndex]() {
            // End any active input selection if any of the clear buttons is
            // clicked. Seems to make the most sense in the UI.
            m_eventWidget->endSelectInput();

            for (auto button: m_selectButtons)
            {
                button->setChecked(false);
            }

            // Clear the slot
            slot->disconnectPipe();
            // Update the current select button to reflect the change
            m_selectButtons[slotIndex]->setText(QSL("<select>"));
            // Disable ok button as there's now at least one unset input
            bool enable_ok = required_inputs_connected_and_valid(m_op.get());
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enable_ok);
            m_opConfigWidget->inputSelected(slotIndex);
        });

        s32 col = 0;
        if (slotCount > 1 || m_op->hasVariableNumberOfSlots())
        {
            m_slotGrid->addWidget(new QLabel(slot->name), row, col++);
        }

        m_slotGrid->addWidget(selectButton, row, col++);
        m_slotGrid->addWidget(clearButton, row, col++);
        ++row;
    }

    // Update the slot select buttons in case we're editing a connected operator
    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        if (m_op->getSlot(slotIndex)->inputPipe)
        {
            if (auto selectButton = m_selectButtons.value(slotIndex, nullptr))
            {
                selectButton->setText(make_input_source_text(m_op->getSlot(slotIndex)));
            }
        }
    }

    onOperatorValidityChanged();
}

void AddEditOperatorDialog::closeEvent(QCloseEvent *event)
{
    if (m_opConfigWidget->hasPendingModifications())
    {
        auto response = QMessageBox::question(this, "Unsaved Changes",
                                              "There are unsaved changes. Do you want to discard them?",
                                              QMessageBox::Discard | QMessageBox::Cancel);

    if (response == QMessageBox::Cancel)
        event->ignore();
    else
        ObjectEditorDialog::closeEvent(event);
    }
}

void AddEditOperatorDialog::inputSelectedForSlot(
    Slot *destSlot,
    Pipe *selectedPipe,
    s32 selectedParamIndex)
{
    qDebug() << __PRETTY_FUNCTION__
        << "destSlot =" << destSlot
        << "selectedPipe =" << selectedPipe
        << "selectedParamIndex =" << selectedParamIndex
        << "pipeSourceOutputIndex =" << selectedPipe->sourceOutputIndex;

    assert(destSlot == m_op->getSlot(destSlot->parentSlotIndex));

    destSlot->connectPipe(selectedPipe, selectedParamIndex);

    s32 slotIndex = destSlot->parentSlotIndex;

    if (0 <= slotIndex && slotIndex < m_selectButtons.size())
    {
        auto selectButton = m_selectButtons[slotIndex];
        QSignalBlocker b(selectButton);
        selectButton->setChecked(false);
        selectButton->setText(make_input_source_text(destSlot));
    }

    // If no valid event has been selected yet, use the event of the newly
    // selected input pipe.
    if (m_eventSelectionCombo->currentData().toUuid().isNull())
    {
        auto eventId = selectedPipe->getSource()->getEventId();
        int idx = m_eventSelectionCombo->findData(eventId);
        if (idx >= 0)
            m_eventSelectionCombo->setCurrentIndex(idx);
    }

    m_op->setObjectFlags(ObjectFlags::NeedsRebuild);
    m_opConfigWidget->inputSelected(slotIndex);
    m_inputSelectActive = false;
    onOperatorValidityChanged();
}

void AddEditOperatorDialog::endInputSelect()
{
    m_opConfigWidget->inputSelected(-1);
    m_inputSelectActive = false;
    onOperatorValidityChanged();
}

void AddEditOperatorDialog::onOperatorValidityChanged()
{
    bool isValid = (m_opConfigWidget->isValid()
                    && required_inputs_connected_and_valid(m_op.get())
                    && !m_eventSelectionCombo->currentData().toUuid().isNull()
                   );

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isValid);
}

void AddEditOperatorDialog::accept()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getServiceProvider());
    m_opConfigWidget->configureOperator();

    auto analysis = m_eventWidget->getServiceProvider()->getAnalysis();

    m_op->setEventId(m_eventSelectionCombo->currentData().toUuid());

    switch (m_mode)
    {
        case ObjectEditorMode::New:
            {
                m_op->setUserLevel(m_userLevel);
                analysis->addOperator(m_op);

                if (m_destDir)
                {
                    m_destDir->push_back(m_op);
                }
            } break;

        case ObjectEditorMode::Edit:
            {
                analysis->setOperatorEdited(m_op);
            } break;
    }

    analysis->beginRun(Analysis::KeepState, m_eventWidget->getVMEConfig());

    QDialog::accept();
}

void AddEditOperatorDialog::reject()
{
    qDebug() << __PRETTY_FUNCTION__;


    switch (m_mode)
    {
        case ObjectEditorMode::New:
            {
                // The operator will not be added to the analysis. This means any slots
                // connected by the user must be disconnected again to avoid having
                // stale connections in the source operators.
                for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
                {
                    Slot *slot = m_op->getSlot(slotIndex);
                    slot->disconnectPipe();
                }
            } break;

        case ObjectEditorMode::Edit:
            {
                AnalysisPauser pauser(m_eventWidget->getServiceProvider());

                // Restore previous slot connections.

                if (m_op->hasVariableNumberOfSlots()
                    && (m_op->getNumberOfSlots() != m_slotBackups.size()))
                {
                    // Restore the original number of inputs.
                    while (m_op->removeLastSlot());

                    while (m_op->getNumberOfSlots() < m_slotBackups.size())
                    {
                        m_op->addSlot();
                    }
                }

                Q_ASSERT(m_op->getNumberOfSlots() == m_slotBackups.size());

                bool wasModified = false;

                for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
                {
                    Slot *slot = m_op->getSlot(slotIndex);
                    auto oldConnection = m_slotBackups[slotIndex];
                    if (slot->inputPipe != oldConnection.inputPipe
                        || slot->paramIndex != oldConnection.paramIndex)
                    {
                        wasModified = true;
                        slot->connectPipe(oldConnection.inputPipe, oldConnection.paramIndex);
                    }
                }

                if (wasModified)
                {
                    m_op->setObjectFlags(ObjectFlags::NeedsRebuild);
                    auto analysis = m_eventWidget->getAnalysis();
                    analysis->beginRun(Analysis::KeepState, m_eventWidget->getVMEConfig());
                }
            } break;
    }

    m_eventWidget->endSelectInput();
    QDialog::reject();
}

bool AddEditOperatorDialog::eventFilter(QObject *watched, QEvent *event)
{
    auto button = qobject_cast<QPushButton *>(watched);
    if (button && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        && !m_inputSelectActive)
    {
        // On slot select button mouse enter/leave events highlight/unhighlight
        // the corresponding nodes in the analysis trees.
        if (auto slot = m_op->getSlot(m_selectButtons.indexOf(button)))
        {
            m_eventWidget->highlightInputOf(slot, event->type() == QEvent::Enter);
        }
    }

    // Do not filter the event out.
    return false;
}

/* Resizes the widget to have a minimum size when it's first shown.
 *
 * When instead using setMinimumSize() directly the widget can be resized
 * smaller to what its child layout would normally allow. This causes shrunken
 * input fields when for example the calibration table is shown after input is
 * selected.
 *
 * Maybe there's a better way to achieve the same but I didn't find it.
 */
void AddEditOperatorDialog::resizeEvent(QResizeEvent *)
{
    if (!m_resizeEventSeen)
    {
        m_resizeEventSeen = true;

        auto sz = size();

        if (sz.height() < WidgetMinHeight)
            sz.setHeight(WidgetMinHeight);

        if (sz.width() < WidgetMinWidth)
            sz.setWidth(WidgetMinWidth);

        resize(sz);
    }
}

//
// OperatorConfigurationWidget
//

static const s32 SlotIndexRole  = Qt::UserRole;
static const s32 ParamIndexRole = Qt::UserRole + 1;

using ArrayMappings = QVector<ArrayMap::IndexPair>;

static void debug_dump(const ArrayMappings &mappings)
{
    for (s32 i=0; i<mappings.size(); ++i)
    {
        qDebug("num=%3d, slot=%d, param=%d",
               i, mappings[i].slotIndex, mappings[i].paramIndex);
    }
}

static QDoubleSpinBox *make_parameter_spinbox()
{
    auto result = new QDoubleSpinBox;

    result->setDecimals(8);
    result->setMinimum(-1e20);
    result->setMaximum(+1e20);
    result->setValue(result->minimum());
    result->setSpecialValueText(QSL("not set"));

    return result;
}

static bool is_set_to_min(QDoubleSpinBox *spin)
{
    return (spin->value() == spin->minimum());
}

static void repopulate_arrayMap_tables(ArrayMap *arrayMap, const ArrayMappings &mappings,
                                       QTableWidget *tw_input, QTableWidget *tw_output)
{
    Q_ASSERT(arrayMap && tw_input && tw_output);

    //qDebug() << __PRETTY_FUNCTION__;
    //debug_dump(mappings);
    (void) debug_dump;

    tw_input->clearContents();
    tw_input->setRowCount(0);

    tw_output->clearContents();
    tw_output->setRowCount(0);

    const s32 slotCount = arrayMap->getNumberOfSlots();

    auto make_table_item = [](const AnalysisObjectPtr &source, s32 slotIndex, s32 paramIndex)
    {
        auto name = source->objectName();

        qDebug() << __PRETTY_FUNCTION__ << source.get() << source->getAnalysis().get();

        if (auto analysis = source->getAnalysis())
        {
            if (auto parentDir = analysis->getParentDirectory(source->shared_from_this()))
                name = parentDir->objectName() + '/' + name;
        }

        auto item = new QTableWidgetItem;

        item->setData(Qt::DisplayRole, QString("%1[%2]")
                      .arg(name)
                      .arg(paramIndex));

        item->setData(SlotIndexRole, slotIndex);
        item->setData(ParamIndexRole, paramIndex);

        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        return item;
    };

    // Fill the input table
    for (s32 slotIndex = 0;
         slotIndex < slotCount;
         ++slotIndex)
    {
        auto slot = arrayMap->getSlot(slotIndex);

        if (!slot->isConnected())
            continue;

        const s32 paramCount = slot->inputPipe->parameters.size();

        for (s32 paramIndex = 0;
             paramIndex < paramCount;
             ++paramIndex)
        {
            if (mappings.contains({slotIndex, paramIndex}))
                continue;

            auto item = make_table_item(slot->inputPipe->source->shared_from_this(),
                                        slotIndex, paramIndex);

            tw_input->setRowCount(tw_input->rowCount() + 1);
            tw_input->setItem(tw_input->rowCount() - 1, 0, item);
        }
    }

    // Fill the output table
    for (const auto &mapping: mappings)
    {
        auto slot = arrayMap->getSlot(mapping.slotIndex);

        if (!slot || !slot->isConnected())
            continue;

        auto item = make_table_item(slot->inputPipe->source->shared_from_this(),
                                    mapping.slotIndex, mapping.paramIndex);

        tw_output->setRowCount(tw_output->rowCount() + 1);
        tw_output->setItem(tw_output->rowCount() - 1, 0, item);
    }

    tw_input->resizeRowsToContents();
    tw_output->resizeRowsToContents();
}

AbstractOpConfigWidget::AbstractOpConfigWidget(OperatorInterface *op,
                                               s32 userLevel,
                                               AnalysisServiceProvider *asp,
                                               QWidget *parent)
    : QWidget(parent)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_wasNameEdited(false)
    , m_serviceProvider(asp)
{
}

OperatorConfigurationWidget::OperatorConfigurationWidget(OperatorInterface *op,
                                                         s32 userLevel,
                                                         AnalysisServiceProvider *serviceProvider,
                                                         QWidget *parent)
    : AbstractOpConfigWidget(op, userLevel, serviceProvider, parent)
{
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);

    widgetLayout->addLayout(formLayout);

    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textChanged, this, [this](const QString &newText) {
        // If the user clears the textedit reset NameEdited to false.
        this->setNameEdited(!newText.isEmpty());
    });
    formLayout->addRow(QSL("Name"), le_name);

    le_name->setText(op->objectName());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        le_xAxisTitle = new QLineEdit;
        le_xAxisTitle->setText(histoSink->m_xAxisTitle);
        formLayout->addRow(QSL("X Title"), le_xAxisTitle);

        combo_xBins = make_resolution_combo(Histo1DMinBits, Histo1DMaxBits, Histo1DDefBits);
        select_by_resolution(combo_xBins, histoSink->m_bins);
        formLayout->addRow(QSL("Resolution"), combo_xBins);

        limits_x = make_axis_limits_ui(QSL("X Limits"),
                                       std::numeric_limits<double>::lowest(),
                                       std::numeric_limits<double>::max(),
                                       histoSink->m_xLimitMin,
                                       histoSink->m_xLimitMax,
                                       histoSink->hasActiveLimits());

        limits_x.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

        formLayout->addRow(limits_x.outerFrame);
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        le_xAxisTitle = new QLineEdit;
        le_xAxisTitle->setText(histoSink->m_xAxisTitle);
        formLayout->addRow(QSL("X Title"), le_xAxisTitle);

        le_yAxisTitle = new QLineEdit;
        le_yAxisTitle->setText(histoSink->m_yAxisTitle);
        formLayout->addRow(QSL("Y Title"), le_yAxisTitle);

        combo_xBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);
        combo_yBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);

        select_by_resolution(combo_xBins, histoSink->m_xBins);
        select_by_resolution(combo_yBins, histoSink->m_yBins);

        formLayout->addRow(QSL("X Resolution"), combo_xBins);
        formLayout->addRow(QSL("Y Resolution"), combo_yBins);

        limits_x = make_axis_limits_ui(QSL("X Limits"),
                                       std::numeric_limits<double>::lowest(),
                                       std::numeric_limits<double>::max(),
                                       histoSink->m_xLimitMin,
                                       histoSink->m_xLimitMax,
                                       histoSink->hasActiveLimits(Qt::XAxis));

        // FIXME: input validation
        //connect(limits_x.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_y = make_axis_limits_ui(QSL("Y Limits"),
                                       std::numeric_limits<double>::lowest(),
                                       std::numeric_limits<double>::max(),
                                       histoSink->m_yLimitMin,
                                       histoSink->m_yLimitMax,
                                       histoSink->hasActiveLimits(Qt::YAxis));

        // FIXME: input validation
        //connect(limits_y.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_x.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        limits_y.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

        formLayout->addRow(limits_x.outerFrame);
        formLayout->addRow(limits_y.outerFrame);
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        spin_index = new QSpinBox;
        spin_index->setMinimum(0);
        spin_index->setMaximum(std::numeric_limits<s32>::max());
        formLayout->addRow(QSL("Selected Index"), spin_index);

        spin_index->setValue(selector->getIndex());
    }
    else if (auto previous = qobject_cast<PreviousValue *>(op))
    {
        cb_keepValid = new QCheckBox(QSL("Keep valid parameters"));
        cb_keepValid->setChecked(previous->m_keepValid);

        formLayout->addRow(cb_keepValid);

        auto label = new QLabel(QSL(
                "If <i>Keep valid parameters</i> is checked the previous valid "
                "input values will be copied to the output.<br/>"
                "<br>"
                "Otherwise the input from the previous event will be "
                "copied, including invalids."
                ));
        label->setWordWrap(true);

        formLayout->addRow(label);
    }
    else if (auto sum = qobject_cast<Sum *>(op))
    {
        cb_isMean = new QCheckBox(QSL("Calculate Mean"));
        cb_isMean->setChecked(sum->m_calculateMean);

        formLayout->addRow(cb_isMean);
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        tw_input = new QTableWidget;
        tw_input->setColumnCount(1);
        tw_input->setHorizontalHeaderLabels({"Input"});
        tw_input->horizontalHeader()->setStretchLastSection(true);
        tw_input->verticalHeader()->setVisible(false);

        tw_output = new QTableWidget;
        tw_output->setColumnCount(1);
        tw_output->setHorizontalHeaderLabels({"Output"});
        tw_output->horizontalHeader()->setStretchLastSection(true);
        tw_output->verticalHeader()->setVisible(false);

        auto pb_inputToOutput = new QPushButton(QIcon(":/arrow_right.png"), QString());
        pb_inputToOutput->setEnabled(false);

        auto pb_outputToInput = new QPushButton(QIcon(":/arrow_left.png"), QString());
        pb_outputToInput->setEnabled(false);

        connect(tw_input, &QTableWidget::itemSelectionChanged, this, [this, pb_inputToOutput] () {
            pb_inputToOutput->setEnabled(tw_input->selectedItems().size() > 0);
        });

        connect(tw_output, &QTableWidget::itemSelectionChanged, this, [this, pb_outputToInput] () {
            pb_outputToInput->setEnabled(tw_output->selectedItems().size() > 0);
        });

        connect(pb_inputToOutput, &QPushButton::clicked, this, [this, arrayMap] () {
            for (auto item: tw_input->selectedItems())
            {
                ArrayMap::IndexPair ip;
                ip.slotIndex = item->data(SlotIndexRole).toInt();
                ip.paramIndex = item->data(ParamIndexRole).toInt();
                m_arrayMappings.push_back(ip);
            }
            repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
        });

        connect(pb_outputToInput, &QPushButton::clicked, this, [this, arrayMap] () {
            for (auto item: tw_output->selectedItems())
            {
                ArrayMap::IndexPair ip;
                ip.slotIndex = item->data(SlotIndexRole).toInt();
                ip.paramIndex = item->data(ParamIndexRole).toInt();
                m_arrayMappings.removeOne(ip);
            }
            repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
        });

        auto buttonsLayout = new QVBoxLayout;
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->addStretch();
        buttonsLayout->addWidget(pb_inputToOutput);
        buttonsLayout->addWidget(pb_outputToInput);
        buttonsLayout->addStretch();

        auto listsLayout = new QHBoxLayout;
        listsLayout->addWidget(tw_input);
        listsLayout->addLayout(buttonsLayout);
        listsLayout->addWidget(tw_output);

        widgetLayout->addLayout(listsLayout);

        m_arrayMappings = arrayMap->m_mappings;
        repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
    {
        spin_minValue = new QDoubleSpinBox(this);
        spin_minValue->setDecimals(8);
        spin_minValue->setMinimum(-1e20);
        spin_minValue->setMaximum(+1e20);
        spin_minValue->setValue(spin_minValue->minimum());
        spin_minValue->setSpecialValueText(QSL("not set"));

        spin_maxValue = new QDoubleSpinBox(this);
        spin_maxValue->setDecimals(8);
        spin_maxValue->setMinimum(-1e20);
        spin_maxValue->setMaximum(+1e20);
        spin_maxValue->setValue(spin_maxValue->minimum());
        spin_maxValue->setSpecialValueText(QSL("not set"));

        rb_keepInside = new QRadioButton(QSL("Keep if inside range"), this);
        rb_keepOutside = new QRadioButton(QSL("Keep if outside range"), this);

        auto buttonGroup = new QButtonGroup(this);
        buttonGroup->addButton(rb_keepInside);
        buttonGroup->addButton(rb_keepOutside);

        auto radioLayout = new QHBoxLayout;
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->addWidget(rb_keepInside);
        radioLayout->addWidget(rb_keepOutside);

        formLayout->addRow(QSL("Min Value"), spin_minValue);
        formLayout->addRow(QSL("Max Value"), spin_maxValue);
        formLayout->addRow(radioLayout);

        if (!std::isnan(rangeFilter->m_minValue))
        {
            spin_minValue->setValue(rangeFilter->m_minValue);
        }

        if (!std::isnan(rangeFilter->m_maxValue))
        {
            spin_maxValue->setValue(rangeFilter->m_maxValue);
        }

        if (rangeFilter->m_keepOutside)
        {
            rb_keepOutside->setChecked(true);
        }
        else
        {
            rb_keepInside->setChecked(true);
        }
    }
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        spin_xMin = make_parameter_spinbox();
        spin_xMax = make_parameter_spinbox();
        spin_yMin = make_parameter_spinbox();
        spin_yMax = make_parameter_spinbox();

        rb_opAnd = new QRadioButton(QSL("AND"));
        rb_opOr  = new QRadioButton(QSL("OR"));

        auto buttonGroup = new QButtonGroup(this);
        buttonGroup->addButton(rb_opAnd);
        buttonGroup->addButton(rb_opOr);

        formLayout->addRow(QSL("X Min"), spin_xMin);
        formLayout->addRow(QSL("X Max"), spin_xMax);
        formLayout->addRow(QSL("Y Min"), spin_yMin);
        formLayout->addRow(QSL("Y Max"), spin_yMax);

        auto radioLayout = new QHBoxLayout;
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->addWidget(rb_opAnd);
        radioLayout->addWidget(rb_opOr);

        formLayout->addRow(QSL("Condition"), radioLayout);

        if (filter->getXInterval().isValid())
        {
            spin_xMin->setValue(filter->getXInterval().minValue());
            spin_xMax->setValue(filter->getXInterval().maxValue());
        }

        if (filter->getYInterval().isValid())
        {
            spin_yMin->setValue(filter->getYInterval().minValue());
            spin_yMax->setValue(filter->getYInterval().maxValue());
        }

        if (filter->getConditionOp() == RectFilter2D::OpAnd)
        {
            rb_opAnd->setChecked(true);
        }
        else
        {
            rb_opOr->setChecked(true);
        }
    }
    else if (auto binOp = qobject_cast<BinarySumDiff *>(op))
    {
        combo_equation = new QComboBox;
        combo_equation->setFont(make_monospace_font());

        for (s32 idx = 0; idx < binOp->getNumberOfEquations(); ++idx)
        {
            combo_equation->addItem(binOp->getEquationDisplayString(idx), idx);
        }

        combo_equation->setCurrentIndex(binOp->getEquation());

        le_unit = new QLineEdit;
        le_unit->setText(binOp->getOutputUnitLabel());

        spin_outputLowerLimit = new QDoubleSpinBox;
        spin_outputUpperLimit = new QDoubleSpinBox;

        for (auto spin: { spin_outputLowerLimit, spin_outputUpperLimit })
        {
            spin->setDecimals(8);
            spin->setMinimum(-1e20);
            spin->setMaximum(+1e20);
        }

        spin_outputLowerLimit->setValue(binOp->getOutputLowerLimit());
        spin_outputUpperLimit->setValue(binOp->getOutputUpperLimit());

        pb_autoLimits = new QPushButton(QSL("Auto Limits"));
        connect(pb_autoLimits, &QPushButton::clicked, this,
                [this, binOp]() { this->updateOutputLimits(binOp); });

        formLayout->addRow(QSL("Equation"), combo_equation);
        formLayout->addRow(QSL("Output Unit"), le_unit);
        formLayout->addRow(QSL("Output Lower Limit"), spin_outputLowerLimit);
        formLayout->addRow(QSL("Output Upper Limit"), spin_outputUpperLimit);
        formLayout->addRow(QSL(""), pb_autoLimits);
    }
    else if (auto aggOp = qobject_cast<AggregateOps *>(op))
    {
        // operation select combo
        combo_aggOp = new QComboBox();
        for (s32 i = 0; i < AggregateOps::NumOps; ++i)
        {
            combo_aggOp->addItem(AggregateOps::getOperationName(
                    static_cast<AggregateOps::Operation>(i)), i);
        }
        combo_aggOp->setCurrentIndex(static_cast<s32>(aggOp->getOperation()));

        // unit label
        le_unit = new QLineEdit;
        le_unit->setText(aggOp->getOutputUnitLabel());

        // thresholds
        cb_useMinThreshold = new QCheckBox;
        cb_useMaxThreshold = new QCheckBox;
        spin_minThreshold = new QDoubleSpinBox;
        spin_maxThreshold = new QDoubleSpinBox;

        connect(cb_useMinThreshold, &QCheckBox::stateChanged,
                this, [this] (int cbState) {
                    spin_minThreshold->setEnabled(cbState == Qt::Checked);
                });

        connect(cb_useMaxThreshold, &QCheckBox::stateChanged,
                this, [this] (int cbState) {
                    spin_maxThreshold->setEnabled(cbState == Qt::Checked);
                });

        for (auto spin: { spin_minThreshold, spin_maxThreshold })
        {
            spin->setDecimals(8);
            spin->setMinimum(-1e20);
            spin->setMaximum(+1e20);
            spin->setValue(0.0);
            spin->setEnabled(false);
        }

        double minT = aggOp->getMinThreshold();
        double maxT = aggOp->getMaxThreshold();

        if (!std::isnan(minT))
        {
            spin_minThreshold->setValue(minT);
            cb_useMinThreshold->setChecked(true);
        }
        else
        {
            cb_useMinThreshold->setChecked(false);
        }

        if (!std::isnan(maxT))
        {
            spin_maxThreshold->setValue(maxT);
            cb_useMaxThreshold->setChecked(true);
        }
        else
        {
            cb_useMaxThreshold->setChecked(false);
        }

        auto minTLayout = new QHBoxLayout;
        minTLayout->setContentsMargins(0, 0, 0, 0);
        minTLayout->setSpacing(2);

        minTLayout->addWidget(cb_useMinThreshold);
        minTLayout->addWidget(spin_minThreshold);

        auto maxTLayout = new QHBoxLayout;
        maxTLayout->setContentsMargins(0, 0, 0, 0);
        maxTLayout->setSpacing(2);

        maxTLayout->addWidget(cb_useMaxThreshold);
        maxTLayout->addWidget(spin_maxThreshold);

        formLayout->addRow(QSL("Operation"), combo_aggOp);
        formLayout->addRow(QSL("Threshold Min"), minTLayout);
        formLayout->addRow(QSL("Threshold Max"), maxTLayout);
        formLayout->addRow(QSL("Output Unit"), le_unit);
        formLayout->addRow(new QLabel(QSL("Leave output unit blank to copy from input.")));
    }
    else if (auto cf = qobject_cast<ConditionFilter *>(op))
    {
        cb_invertCondition = new QCheckBox;
        cb_invertCondition->setChecked(cf->m_invertedCondition);

        formLayout->addRow(QSL("Invert Condition"), cb_invertCondition);
    }
    else if (auto ex = qobject_cast<ExportSink *>(op))
    {
        // operator and struct/class name
        {
            auto label = make_framed_description_label(QSL(
                    "<i>Name</i> must be a valid C++/Python identifier as it is used"
                    " for generated struct and class names."));

            int nameRow = get_widget_row(formLayout, le_name);
            formLayout->insertRow(nameRow + 1, label);
        }

        QRegularExpression re("^[a-zA-Z_][a-zA-Z0-9_]*$");
        auto nameValidator = new QRegularExpressionValidator(re, le_name);
        le_name->setValidator(nameValidator);

        // export prefix path
        le_exportPrefixPath = new QLineEdit;
        m_prefixPathWasManuallyEdited = false;

        connect(le_name, &QLineEdit::textChanged, this, [this] (const QString &) {
            if (!m_prefixPathWasManuallyEdited && le_name->hasAcceptableInput())
            {
                QString newPrefix = QDir("exports/").filePath(le_name->text());
                le_exportPrefixPath->setText(newPrefix);
            }
            else if (!m_prefixPathWasManuallyEdited && le_name->text().isEmpty())
            {
                le_exportPrefixPath->setText(QSL("exports/"));
            }

            emit validityMayHaveChanged();
        });

        connect(le_exportPrefixPath, &QLineEdit::textEdited, this, [this] (const QString &) {
            m_prefixPathWasManuallyEdited = true;
            emit validityMayHaveChanged();
        });

        connect(le_exportPrefixPath, &QLineEdit::textChanged, this, [this] (const QString &) {
            emit validityMayHaveChanged();
        });

        pb_selectOutputDirectory = new QPushButton("Select");
        {
            auto l = new QHBoxLayout;
            l->addWidget(le_exportPrefixPath);
            l->addWidget(pb_selectOutputDirectory);
            l->setStretch(0, 1);
            formLayout->addRow("Output Directory", l);

            connect(pb_selectOutputDirectory, &QPushButton::clicked, this, [=]() {

                QString startDir = le_exportPrefixPath->text();

                if (startDir.isEmpty())
                    startDir = QSL("exports");

                auto dirName  = QFileDialog::getExistingDirectory(
                    this, QSL("Create or select an export output directory"),
                    startDir);

                if (!dirName.isEmpty())
                {
                    auto wsDir = m_serviceProvider->getWorkspaceDirectory();
                    if (!wsDir.endsWith("/")) wsDir += "/";

                    if (dirName.startsWith(wsDir))
                        dirName = dirName.mid(wsDir.size());

                    le_exportPrefixPath->setText(dirName);
                }
            });
        }

        // format (sparse, dense, csv)
        {
            combo_exportFormat = new QComboBox;
            combo_exportFormat->addItem("Indexed / Sparse", static_cast<int>(ExportSink::Format::Sparse));
            combo_exportFormat->addItem("Plain / Full",     static_cast<int>(ExportSink::Format::Full));
            combo_exportFormat->addItem("CSV",              static_cast<int>(ExportSink::Format::CSV));

            formLayout->addRow("Format", combo_exportFormat);

            auto stack = new QStackedWidget;

            auto label = make_framed_description_label(QSL(
                        "Sparse format writes out indexes and values, omitting"
                        " invalid parameters and NaNs.\n"
                        "For input arrays where for most events only a few"
                        " parameters are valid, this format produces much"
                        " smaller output files than the Full format."
                        ));
            stack->addWidget(label);

            label = make_framed_description_label(QSL(
                        "Full format writes out each input array as-is,"
                        " including invalid parameters (special NaN values).\n"
                        "Use this format if for most events all of the array"
                        " parameters are valid or read performance of the"
                        " exported data file is critical.\n"
                        "Warning: this format can produce large files quickly!"
                        ));
            stack->addWidget(label);

            label = make_framed_description_label(QSL(
                    "CSV text output format. No code generation."
                    ));
            stack->addWidget(label);

            connect(combo_exportFormat, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                    stack, &QStackedWidget::setCurrentIndex);

            formLayout->addRow(stack);
        }

        // compression
        {
            combo_exportCompression = new QComboBox;
            combo_exportCompression->addItem("Don't compress", 0);
            combo_exportCompression->addItem("zlib fast", 1);
            formLayout->addRow("Compression", combo_exportCompression);

            auto label = make_framed_description_label(QSL(
                    "zlib compression will produce a gzip compatible compressed file.\n"
                    "Generated C++ code will require the zlib development files"
                    " to be installed on the system.\n"
                    "Generated Python code will use the gzip module included with Python."
                    ));

            formLayout->addRow(label);
        }

        // codegen and open output directory

        {
            pb_generateCode  = new QPushButton(QSL("C++ && Python code"));

            pb_openOutputDir = new QPushButton(QIcon(":/folder_orange.png"),
                                               QSL("Open output directory"));

            gb_codeGen = new QGroupBox("Code generation");
            auto l     = new QGridLayout(gb_codeGen);
            l->setContentsMargins(2, 2, 2, 2);
            auto label = make_framed_description_label(QSL(
                    "Important: Code generation will overwrite existing files!\n"
                    "Set the output path and export options above, then use the"
                    " buttons below to generate the code files.\n"
                    "Errors during code generation will be shown in the Log Window."
                   ));
            label->setAlignment(Qt::AlignLeft | Qt::AlignTop);

            l->addWidget(label,                0, 0, 1, 3);
            l->addWidget(pb_generateCode,      1, 0);
            l->addWidget(pb_openOutputDir,     1, 1);
            l->addWidget(make_spacer_widget(), 1, 2);

            formLayout->addRow(gb_codeGen);

            auto logger = [serviceProvider, ex] (const QString &msg)
            {
                auto s = QSL("File Export %1: %2")
                    .arg(ex->objectName())
                    .arg(msg);
                serviceProvider->logMessage(s);
            };

            connect(pb_generateCode, &QPushButton::clicked, this, [this, ex, logger] () {
                try
                {
                    this->configureOperator();
                    ex->generateCode(logger);
                }
                catch (const QString &e)
                {
                    logger(e);
                }
                catch (const std::exception &e)
                {
                    logger(QString::fromStdString(e.what()));
                }
            });

            connect(this, &AbstractOpConfigWidget::validityMayHaveChanged, this, [this]() {
                pb_generateCode->setEnabled(isValid());
                pb_openOutputDir->setEnabled(isValid());
            });

            connect(pb_openOutputDir, &QPushButton::clicked, this, [this, ex, logger] () {
                try
                {
                    this->configureOperator();

                    QString path = m_serviceProvider->getWorkspaceDirectory() + "/" +
                        ex->getOutputPrefixPath();

                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                }
                catch (const QString &e)
                {
                    logger(e);
                }
                catch (const std::exception &e)
                {
                    logger(QString::fromStdString(e.what()));
                }
            });
        }

        connect(combo_exportFormat, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this] (int) {
                    auto fmt = static_cast<ExportSink::Format>(combo_exportFormat->currentData().toInt());
                    gb_codeGen->setEnabled(fmt != ExportSink::Format::CSV);
                });

        //
        // populate
        //

        auto prefixPath = ex->getOutputPrefixPath();

        if (prefixPath.isEmpty())
            prefixPath = "exports/";

        le_exportPrefixPath->setText(prefixPath);

        combo_exportFormat->setCurrentIndex(
            combo_exportFormat->findData(static_cast<int>(ex->getFormat())));

        combo_exportCompression->setCurrentIndex(
            combo_exportCompression->findData(ex->getCompressionLevel()));
    }
}

// NOTE: This will be called after construction for each slot by AddEditOperatorDialog::repopulateSlotGrid()!
void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;

    qDebug() << __PRETTY_FUNCTION__ << op
        << ": wasNamedEdited =" << wasNameEdited()
        << ", no_input_connected =" << no_input_connected(op)
        << ", isValid =" << this->isValid();

    emit validityMayHaveChanged();

    if (no_input_connected(op) && !wasNameEdited())
    {
        qDebug() << __PRETTY_FUNCTION__ << op << ": clearing name";
        le_name->clear();
        setNameEdited(false);
    }

    //
    // Operator name
    //
    if (!wasNameEdited())
    {
        // The name field is empty or was never modified by the user. Update its
        // contents to reflect the newly selected input(s).

        if (op->getNumberOfSlots() == 1 && op->getSlot(0)->isConnected())
        {
            le_name->setText(make_input_source_text(op->getSlot(0)));
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
        {
            if (histoSink->m_inputX.isConnected() && histoSink->m_inputY.isConnected())
            {
                QString nameX = make_input_source_text(&histoSink->m_inputX);
                QString nameY = make_input_source_text(&histoSink->m_inputY);
                le_name->setText(QString("%1_vs_%2").arg(nameX).arg(nameY));
            }
        }
        else if (auto difference = qobject_cast<Difference *>(op))
        {
            if (difference->m_inputA.isConnected() && difference->m_inputB.isConnected())
            {
                QString nameA = make_input_source_text(&difference->m_inputA);
                QString nameB = make_input_source_text(&difference->m_inputB);
                le_name->setText(QString("%1 - %2").arg(nameA).arg(nameB));
            }
        }
        else if (auto condFilter = qobject_cast<ConditionFilter *> (op))
        {
            if (condFilter->m_dataInput.isConnected() && condFilter->m_conditionInput.isConnected())
            {
                QString dataName = make_input_source_text(&condFilter->m_dataInput);
                QString condName = make_input_source_text(&condFilter->m_conditionInput);
                le_name->setText(QString("%1_if_%2").arg(dataName).arg(condName));
            }
        }
        else if (auto filter = qobject_cast<RectFilter2D *>(op))
        {
            if (filter->getSlot(0)->isConnected() && filter->getSlot(1)->isConnected())
            {
                QString nameX = make_input_source_text(filter->getSlot(0));
                QString nameY = make_input_source_text(filter->getSlot(1));
                le_name->setText(QString("%1_%2").arg(nameX).arg(nameY));
            }
        }
        else if (auto ex = qobject_cast<ExportSink *>(op))
        {
            // Slot 1 is the first data input.
            if (ex->getSlot(1)->isConnected())
            {
                QString name = variablify(make_input_source_text(op->getSlot(1)));
                le_name->setText(name);
            }
        }
        else if (auto binOp = qobject_cast<BinarySumDiff *>(op))
        {
            if (binOp->getSlot(0)->isConnected() && binOp->getSlot(1)->isConnected())
            {
                QString nameA = make_input_source_text(binOp->getSlot(0));
                QString nameB = make_input_source_text(binOp->getSlot(1));
                le_name->setText(QString("%1_%2").arg(nameA).arg(nameB));
            }
        }
    }

    if (!le_name->text().isEmpty()
        && op->getNumberOfOutputs() > 0                 // non-sinks only
        && required_inputs_connected_and_valid(op)
        && !wasNameEdited())
    {
        // TODO: use the currently selected operations name
        // as the suffix (currently it always says 'sum')
#if 0
        if (auto aggOp = qobject_cast<AggregateOps *>(op))
        {

        }
        else
#endif
        {
            // Append the lowercase short name for non sinks
            auto suffix = QSL(".") + op->getShortName().toLower();
            auto name = le_name->text();
            if (!name.endsWith(suffix))
            {
                name += suffix;
                le_name->setText(name);
            }
        }
    }

    //
    // Operator specific actions
    //

    Slot *slot = (slotIndex >= 0 ? op->getSlot(slotIndex) : nullptr);

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty())
        {
            le_xAxisTitle->setText(le_name->text());
        }

        if (histoSink->getSlot(0)->isParamIndexInRange()
            && slot == histoSink->getSlot(0)
            && std::isnan(histoSink->m_xLimitMin))
        {
            s32 paramIndex = slot->paramIndex;

            if (paramIndex == Slot::NoParamIndex && slot->inputPipe->getSize() > 0)
                paramIndex = 0;

            if (0 <= paramIndex && paramIndex < slot->inputPipe->getSize())
            {
                limits_x.spin_min->setValue(slot->inputPipe->parameters[paramIndex].lowerLimit);
                limits_x.spin_max->setValue(slot->inputPipe->parameters[paramIndex].upperLimit);
            }
        }
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty() && histoSink->m_inputX.isConnected())
            le_xAxisTitle->setText(make_input_source_text(&histoSink->m_inputX));

        if (le_yAxisTitle->text().isEmpty() && histoSink->m_inputY.isConnected())
            le_yAxisTitle->setText(make_input_source_text(&histoSink->m_inputY));

        // x input was selected
        if (histoSink->m_inputX.isParamIndexInRange()
            && slot == &histoSink->m_inputX
            && std::isnan(histoSink->m_xLimitMin))
        {
            limits_x.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_x.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }

        // y input was selected
        if (histoSink->m_inputY.isParamIndexInRange()
            && slot == &histoSink->m_inputY
            && std::isnan(histoSink->m_yLimitMin))
        {
            limits_y.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_y.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
    }
    else if (qobject_cast<RangeFilter1D *>(op))
    {
        Q_ASSERT(slot);

        if (slot->isConnected())
        {
            // use the first parameters min and max values to fill the spinboxes
            s32 paramIndex = (slot->paramIndex == Slot::NoParamIndex ? 0 : slot->paramIndex);
            Parameter *param = slot->inputPipe->getParameter(paramIndex);

            if (spin_minValue->value() == spin_minValue->minimum())
            {
                if (param)
                {
                    spin_minValue->setValue(param->lowerLimit);
                }
                else
                {
                    spin_minValue->setValue(0.0);
                }
            }

            if (spin_maxValue->value() == spin_maxValue->minimum())
            {
                if (param)
                {
                    spin_maxValue->setValue(param->upperLimit);
                }
                else
                {
                    spin_maxValue->setValue(0.0);
                }
            }
        }
    }
    else if (qobject_cast<RectFilter2D *>(op))
    {
        Q_ASSERT(slot);

        if (slot == op->getSlot(0)) // x
        {
            if (slot->isConnected())
            {
                Q_ASSERT(slot->isParamIndexInRange());

                if (spin_xMin->value() == spin_xMin->minimum())
                {
                    spin_xMin->setValue(slot->inputPipe->getParameter(slot->paramIndex)->lowerLimit);
                }

                if (spin_xMax->value() == spin_xMax->minimum())
                {
                    spin_xMax->setValue(slot->inputPipe->getParameter(slot->paramIndex)->upperLimit);
                }
            }
        }
        else if (slot == op->getSlot(1)) // y
        {
            if (slot->isConnected())
            {
                Q_ASSERT(slot->isParamIndexInRange());

                if (spin_yMin->value() == spin_yMin->minimum())
                {
                    spin_yMin->setValue(slot->inputPipe->getParameter(slot->paramIndex)->lowerLimit);
                }

                if (spin_yMax->value() == spin_yMax->minimum())
                {
                    spin_yMax->setValue(slot->inputPipe->getParameter(slot->paramIndex)->upperLimit);
                }
            }
        }
    }
}

bool OperatorConfigurationWidget::isValid() const
{
    if (auto ex = qobject_cast<ExportSink *>(m_op))
    {
        qDebug() << __PRETTY_FUNCTION__;
        return le_name->hasAcceptableInput()
            && !le_exportPrefixPath->text().isEmpty()
            && required_inputs_connected_and_valid(ex);
    }

    return true;
}

void OperatorConfigurationWidget::configureOperator()
{
    OperatorInterface *op = m_op;

    assert(required_inputs_connected_and_valid(op));

    op->setObjectName(le_name->text());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        histoSink->m_xAxisTitle = le_xAxisTitle->text();

        s32 bins = combo_xBins->currentData().toInt();
        histoSink->m_bins = bins;

        if (limits_x.rb_limited->isChecked())
        {
            histoSink->m_xLimitMin = limits_x.spin_min->value();
            histoSink->m_xLimitMax = limits_x.spin_max->value();
        }
        else
        {
            histoSink->m_xLimitMin = make_quiet_nan();
            histoSink->m_xLimitMax = make_quiet_nan();
        }

        // Actually updating the histograms is done in Histo1DSink::beginRun();
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        histoSink->m_xAxisTitle = le_xAxisTitle->text();
        histoSink->m_yAxisTitle = le_yAxisTitle->text();

        s32 xBins = combo_xBins->currentData().toInt();
        s32 yBins = combo_yBins->currentData().toInt();

        histoSink->m_xBins = xBins;
        histoSink->m_yBins = yBins;

        if (limits_x.rb_limited->isChecked())
        {
            histoSink->m_xLimitMin = limits_x.spin_min->value();
            histoSink->m_xLimitMax = limits_x.spin_max->value();
        }
        else
        {
            histoSink->m_xLimitMin = make_quiet_nan();
            histoSink->m_xLimitMax = make_quiet_nan();
        }

        if (limits_y.rb_limited->isChecked())
        {
            histoSink->m_yLimitMin = limits_y.spin_min->value();
            histoSink->m_yLimitMax = limits_y.spin_max->value();
        }
        else
        {
            histoSink->m_yLimitMin = make_quiet_nan();
            histoSink->m_yLimitMax = make_quiet_nan();
        }

        // Same as for Histo1DSink: the histogram is created or updated in Histo2DSink::beginRun()
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        s32 index = spin_index->value();
        selector->setIndex(index);
    }
    else if (auto previous = qobject_cast<PreviousValue *>(op))
    {
        previous->m_keepValid = cb_keepValid->isChecked();
    }
    else if (auto sum = qobject_cast<Sum *>(op))
    {
        sum->m_calculateMean = cb_isMean->isChecked();
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        /* Remove mappings that got invalidated due to removing slots from the
         * ArrayMap, then copy the mappings over into the ArrayMap operator. */
        s32 nSlots = arrayMap->getNumberOfSlots();

        m_arrayMappings.erase(
            std::remove_if(m_arrayMappings.begin(), m_arrayMappings.end(),
                           [nSlots](const ArrayMap::IndexPair &ip) {
                bool result = ip.slotIndex >= nSlots;
                return result;
            }),
            m_arrayMappings.end());

        arrayMap->m_mappings = m_arrayMappings;
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
    {
        rangeFilter->m_minValue = spin_minValue->value();
        rangeFilter->m_maxValue = spin_maxValue->value();
        rangeFilter->m_keepOutside = rb_keepOutside->isChecked();
    }
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        if (is_set_to_min(spin_xMin) || is_set_to_min(spin_xMax))
        {
            filter->setXInterval(QwtInterval()); // set an invalid interval
        }
        else
        {
            filter->setXInterval(spin_xMin->value(), spin_xMax->value());
        }

        if (is_set_to_min(spin_yMin) || is_set_to_min(spin_yMax))
        {
            filter->setYInterval(QwtInterval()); // set an invalid interval
        }
        else
        {
            filter->setYInterval(spin_yMin->value(), spin_yMax->value());
        }

        filter->setConditionOp(rb_opAnd->isChecked() ? RectFilter2D::OpAnd : RectFilter2D::OpOr);
    }
    else if (auto binOp = qobject_cast<BinarySumDiff *>(op))
    {
        double ll = spin_outputLowerLimit->value();
        double ul = spin_outputUpperLimit->value();

        if (ul - ll == 0.0)
        {
            updateOutputLimits(binOp);
            ll = spin_outputLowerLimit->value();
            ul = spin_outputUpperLimit->value();
        }

        binOp->setEquation(combo_equation->currentData().toInt());
        binOp->setOutputUnitLabel(le_unit->text());
        binOp->setOutputLowerLimit(ll);
        binOp->setOutputUpperLimit(ul);
    }
    else if (auto aggOp = qobject_cast<AggregateOps *>(op))
    {
        aggOp->setOperation(static_cast<AggregateOps::Operation>(combo_aggOp->currentData().toInt()));

        double minT = spin_minThreshold->value();
        double maxT = spin_maxThreshold->value();

        aggOp->setMinThreshold(cb_useMinThreshold->isChecked() ? minT : make_quiet_nan());
        aggOp->setMaxThreshold(cb_useMaxThreshold->isChecked() ? maxT : make_quiet_nan());

        aggOp->setOutputUnitLabel(le_unit->text());
    }
    else if (auto cf = qobject_cast<ConditionFilter *>(op))
    {
        cf->m_invertedCondition = cb_invertCondition->isChecked();
    }
    else if (auto ex = qobject_cast<ExportSink *>(op))
    {
        ex->setCompressionLevel(combo_exportCompression->currentData().toInt());
        ex->setFormat(static_cast<ExportSink::Format>(combo_exportFormat->currentData().toInt()));
        ex->setOutputPrefixPath(le_exportPrefixPath->text());
    }
}

void OperatorConfigurationWidget::updateOutputLimits(BinarySumDiff *op)
{
    if (!required_inputs_connected_and_valid(op))
        return;

    int equationIndex = combo_equation->currentData().toInt();
    double llA = op->getSlot(0)->inputPipe->parameters.value(0).lowerLimit;
    double ulA = op->getSlot(0)->inputPipe->parameters.value(0).upperLimit;

    double llB = op->getSlot(1)->inputPipe->parameters.value(0).lowerLimit;
    double ulB = op->getSlot(1)->inputPipe->parameters.value(0).upperLimit;

    double llO = 0.0;
    double ulO = 0.0;

    switch (equationIndex)
    {
        case 0: // C = A + B
            {
                llO = llA + llB;
                ulO = ulA + ulB;
            } break;

        case 1: // C = A - B
            {
                llO = llA - ulB;
                ulO = ulA - llB;
            } break;

        case 2: // C = (A + B) / (A - B)
            {
                llO = (ulA + llB) / (ulA - llB);
                ulO = (llA + ulB) / (llA - ulB);
            } break;

        case 3: // C = (A - B) / (A + B)
            {
                llO = (ulA - ulB) / (ulA + ulB);
                ulO = (llA - llB) / (llA + llB);
            } break;

        case 4: // C = A / (A - B)
            {
                llO = ulA / (ulA - llB);
                ulO = llA / (llA - ulB);
            } break;

        case 5: // C = (A - B) / A
            {
                llO = (ulA - llB) / ulA;
                ulO = (llA - ulB) / llA;
            } break;

        case 6: // C = (A * B)
            {
                llO = (llA * llB);
                ulO = (ulA * ulB);
            } break;

        case 7: // C = (A / B)
            {
                llO = (llA / ulB);
                ulO = (ulA / llB);
            } break;
    }

    spin_outputLowerLimit->setValue(llO);
    spin_outputUpperLimit->setValue(ulO);
}

struct RateTypeInfo
{
    RateMonitorSink::Type type;
    QString name;
    QString description;
};

static const std::array<RateTypeInfo, 3> RateTypeInfos =
{
    {
        {
            RateMonitorSink::Type::FlowRate,
            QSL("Flow Rate"),
            QSL("The rate of flow through the input array is calculated and recorded.\n"
                "The sampling period is based on analysis timeticks so that the"
                " time axis represents experiment time."
                )
        },

        {
            RateMonitorSink::Type::CounterDifference,
            QSL("Counter Difference"),
            QSL("Input values are interpreted as increasing counter values.\n"
                "The resulting rate is calculated from the difference of successive input values.\n"
                "The sampling rate is tied to the event rate. This option"
                " should mostly be used in periodic events with a fixed"
                " interval."
                )
        },

        {
            RateMonitorSink::Type::PrecalculatedRate,
            QSL("Precalculated Rate"),
            QSL("Input values are interpreted as rate values and are directly recorded.\n"
                "The sampling rate is tied to the event rate. This option"
                " should mostly be used in periodic events with a fixed"
                " interval."
                )
        },
    }
};

CalibrationMinMaxConfigWidget::CalibrationMinMaxConfigWidget(CalibrationMinMax *op,
                        s32 userLevel,
                        AnalysisServiceProvider *asp,
                        QWidget *parent)
    : AbstractOpConfigWidget(op, userLevel, asp, parent)
    , m_cal(op)
{
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);

    widgetLayout->addLayout(formLayout);

    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textChanged, this, [this](const QString &newText) {
        // If the user clears the textedit reset NameEdited to false.
        this->setNameEdited(!newText.isEmpty());
        setModified();
    });
    formLayout->addRow(QSL("Name"), le_name);

    le_name->setText(op->objectName());

    le_unit = new QLineEdit;
    le_unit->setText(m_cal->getUnitLabel());
    formLayout->addRow(QSL("Output Unit Label"), le_unit);

    spin_unityInput = make_calibration_spinbox();
    spin_unityInput->setDecimals(1);
    spin_unityInput->setValue(1.0);
    spin_unityInput->setReadOnly(true);
    spin_unityOutput = make_calibration_spinbox();
    spin_unityOutput->setValue(1.0);
    pb_applyUnity = new QPushButton("&Set");
    pb_applyUnity->setToolTip("Apply to Output Min/Max values");
    pb_useInputLimits = new QPushButton("&Use Input Range");
    spin_inputMin = make_calibration_spinbox();
    spin_inputMax = make_calibration_spinbox();
    spin_inputMin->setReadOnly(true);
    spin_inputMax->setReadOnly(true);
    spin_outputMin = make_calibration_spinbox();
    spin_outputMax = make_calibration_spinbox();

    auto calibGridLayout = new QGridLayout;
    calibGridLayout->setContentsMargins(0, 0, 0, 0);
    calibGridLayout->setSpacing(2);

    calibGridLayout->addWidget(new QLabel("Unity"), 1, 0);
    calibGridLayout->addWidget(new QLabel("Min"), 2, 0);
    calibGridLayout->addWidget(new QLabel("Max"), 3, 0);

    calibGridLayout->addWidget(new QLabel("Input"), 0, 1);
    calibGridLayout->addWidget(new QLabel("Output"), 0, 2);

    calibGridLayout->addWidget(spin_unityInput, 1, 1);
    calibGridLayout->addWidget(spin_unityOutput, 1, 2);
    calibGridLayout->addWidget(pb_applyUnity, 1, 3);
    calibGridLayout->addWidget(pb_useInputLimits, 2, 3, 2, 1);

    calibGridLayout->addWidget(spin_inputMin, 2, 1);
    calibGridLayout->addWidget(spin_inputMax, 3, 1);
    calibGridLayout->addWidget(spin_outputMin, 2, 2);
    calibGridLayout->addWidget(spin_outputMax, 3, 2);


    calibGridLayout->setColumnStretch(0, 0);

    formLayout->addRow(calibGridLayout);

    //formLayout->addRow(QSL("Unit Min"), spin_unitMin);
    //formLayout->addRow(QSL("Unit Max"), spin_unitMax);

    // Find first valid min/max pair and use it to fill the spinboxes.
    for (const auto &params: m_cal->getCalibrations())
    {
        if (params.isValid())
        {
            qDebug() << __PRETTY_FUNCTION__ << m_cal->objectName()
                << "setting spinbox values:" << params.unitMin << params.unitMax;
            spin_outputMin->setValue(params.unitMin);
            spin_outputMax->setValue(params.unitMax);
            break;
        }
    }

    m_pb_applyGlobalCalib = new QPushButton(QIcon(":/arrow_down.png"), QSL("&Apply to all"));
    m_pb_applyGlobalCalib->setToolTip(QSL("Apply above calibration to all output addresses."));

    m_applyGlobalCalibFrame = new QFrame;
    auto applyCalibLayout = new QHBoxLayout(m_applyGlobalCalibFrame);
    applyCalibLayout->setContentsMargins(0, 0, 0, 0);
    applyCalibLayout->addStretch(1);
    applyCalibLayout->addWidget(m_pb_applyGlobalCalib);
    applyCalibLayout->addStretch(1);

    formLayout->addRow(make_separator_frame());
    formLayout->addRow(m_applyGlobalCalibFrame);
    m_applyGlobalCalibFrame->setVisible(false);

    static const QStringList HeaderLabels{"Address", "Min", "Max", "Apply Calib"};
    static const auto ColumnCount = HeaderLabels.size();

    m_calibrationTable = new QTableWidget;
    m_calibrationTable->setMinimumSize(325, 175);
    m_calibrationTable->setVisible(false);
    m_calibrationTable->setColumnCount(ColumnCount);
    m_calibrationTable->setItemDelegateForColumn(1, new CalibrationItemDelegate(m_calibrationTable));
    m_calibrationTable->setItemDelegateForColumn(2, new CalibrationItemDelegate(m_calibrationTable));
    m_calibrationTable->setHorizontalHeaderLabels(HeaderLabels);
    m_calibrationTable->verticalHeader()->setVisible(false);

    widgetLayout->addWidget(m_calibrationTable);

    connect(m_pb_applyGlobalCalib, &QPushButton::clicked, this, [this] () {
        double unitMin = spin_outputMin->value();
        double unitMax = spin_outputMax->value();

        // Write spinbox values into the table
        for (s32 row = 0;
                row < m_calibrationTable->rowCount();
                ++row)
        {
            auto minItem = m_calibrationTable->item(row, 1);
            minItem->setData(Qt::EditRole, unitMin);

            auto maxItem = m_calibrationTable->item(row, 2);
            maxItem->setData(Qt::EditRole, unitMax);
        }
    });

    auto on_unit_text_changed =  [this] (const QString &str)
    {
        auto suffix = " " + str;
        spin_outputMin->setSuffix(suffix);
        spin_outputMax->setSuffix(suffix);
        spin_unityOutput->setSuffix(suffix);
    };

    // Set output min/max values based on the value of the unity output value.
    auto apply_unity_value = [this]
    {
        auto unityOutputValue = spin_unityOutput->value();
        auto newOutputMin = spin_inputMin->value() * unityOutputValue;
        auto newOutputMax = spin_inputMax->value() * unityOutputValue;
        spin_outputMin->setValue(newOutputMin);
        spin_outputMax->setValue(newOutputMax);
    };

    // Adjust unity output value based on output min/max values.
    auto update_unity_value = [this]
    {
        auto inputRange = spin_inputMax->value() - spin_inputMin->value();
        auto outputRange = spin_outputMax->value() - spin_outputMin->value();
        auto factor = outputRange / inputRange;
        spin_unityOutput->setValue(factor);
    };

    auto use_input_limits = [this]
    {
        spin_outputMin->setValue(spin_inputMin->value());
        spin_outputMax->setValue(spin_inputMax->value());
    };

    auto on_calib_data_modified = [this]
    {
         qDebug() << "on_calib_data_modified";
         calibModifiedButNotApplied_ = true;
    };

    connect(le_unit, &QLineEdit::textChanged, this, on_unit_text_changed);

    connect(spin_outputMin, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, update_unity_value);

    connect(spin_outputMax, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, update_unity_value);

    connect(pb_applyUnity, &QPushButton::clicked, this,  apply_unity_value);

    connect(pb_useInputLimits, &QPushButton::clicked, this, use_input_limits);

    // Handle modifications to calibration data.
    connect(le_unit, &QLineEdit::textChanged, this, on_calib_data_modified);

    connect(spin_unityOutput, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, on_calib_data_modified);

    connect(spin_outputMin, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, on_calib_data_modified);

    connect(spin_outputMax, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, on_calib_data_modified);

    // Populates the calibration table
    inputSelected(0);
    on_unit_text_changed(le_unit->text());
    update_unity_value();
    calibModifiedButNotApplied_ = false; // just populated with data loaded from the calibration
}

void CalibrationMinMaxConfigWidget::inputSelected(s32 slotIndex)
{
    setModified();
    emit validityMayHaveChanged();

    auto op = m_cal;

    if (no_input_connected(op) && !wasNameEdited())
    {
        le_name->clear();
        setNameEdited(false);
    }

    if (!wasNameEdited() && op->getSlot(0)->isConnected())
    {
        le_name->setText(make_input_source_text(op->getSlot(0)));
    }

    Slot *slot = (slotIndex >= 0 ? op->getSlot(slotIndex) : nullptr);

    assert(slot);

    if (slot->isConnected())
    {
        const s32 paramIndex = (slot->paramIndex == Slot::NoParamIndex ? 0 : slot->paramIndex);

        if (auto inParam = slot->inputPipe->getParameter(paramIndex))
        {
            spin_inputMin->setValue(inParam->lowerLimit);
            spin_inputMax->setValue(inParam->upperLimit);
            spin_inputMin->setSuffix(" " + slot->inputPipe->parameters.unit);
            spin_inputMax->setSuffix(" " + slot->inputPipe->parameters.unit);
            spin_unityInput->setSuffix(" " + slot->inputPipe->parameters.unit);
            spin_unityInput->setValue(1.0);
        }

        // If connected to array:
        //   use first calibration values for spinbox
        //   if those are not valid use first input param values for spinbox
        // if connected to param:
        //   use calibration for param for spinbox
        //   if that is not valid use input param values for spinbox
        // if nothing is valid keep spinbox values

        double proposedMin = spin_outputMin->value();
        double proposedMax = spin_outputMax->value();

        auto params = op->getCalibration(paramIndex);

        if (params.isValid())
        {
            proposedMin = params.unitMin;
            proposedMax = params.unitMax;
        }
        else if (auto inParam = slot->inputPipe->getParameter(paramIndex))
        {
            proposedMin = inParam->lowerLimit;
            proposedMax = inParam->upperLimit;
        }

        spin_outputMin->setValue(proposedMin);
        spin_outputMax->setValue(proposedMax);

        if (slot->paramIndex == Slot::NoParamIndex)
        {
            fillCalibrationTable(op, proposedMin, proposedMax);
            m_calibrationTable->setVisible(true);
            m_applyGlobalCalibFrame->setVisible(true);
        }
        else
        {
            m_calibrationTable->setVisible(false);
            m_applyGlobalCalibFrame->setVisible(false);
        }
    }
    else
    {
        // Not connected -> Make spinboxes show "Not Set" and hide table and button
        spin_inputMin->setValue(spin_inputMin->minimum());
        spin_inputMax->setValue(spin_inputMax->minimum());
        spin_inputMin->setSuffix({});
        spin_inputMax->setSuffix({});
        spin_outputMin->setValue(spin_outputMin->minimum());
        spin_outputMax->setValue(spin_outputMax->minimum());
        spin_unityInput->setSuffix({});
        spin_unityInput->setValue(spin_unityInput->minimum());
        spin_unityOutput->setSuffix({});
        spin_unityOutput->setValue(spin_unityOutput->minimum());

        m_calibrationTable->setVisible(false);
        m_applyGlobalCalibFrame->setVisible(false);
    }

    pb_applyUnity->setVisible(slot->isConnected());
    pb_useInputLimits->setVisible(slot->isConnected());
}


void CalibrationMinMaxConfigWidget::configureOperator()
{
    assert(required_inputs_connected_and_valid(m_cal));

    m_cal->setObjectName(le_name->text());
    m_cal->setUnitLabel(le_unit->text());

    double unitMin = spin_outputMin->value();
    double unitMax = spin_outputMax->value();

    if (unitMin == spin_outputMin->minimum())
        unitMin = make_quiet_nan();

    if (unitMax == spin_outputMax->minimum())
        unitMax = make_quiet_nan();

    for (s32 addr = 0; addr < m_cal->getSlot(0)->inputPipe->parameters.size(); ++addr)
    {
        if (m_op->getSlot(0)->paramIndex != Slot::NoParamIndex)
        {
            // Connected to specific address -> use spinbox values for that address
            if (m_op->getSlot(0)->paramIndex == addr)
            {
                m_cal->setCalibration(addr, unitMin, unitMax);
            }
            else
            {
                // Set invalid params for other addresses
                m_cal->setCalibration(addr, CalibrationMinMaxParameters());
            }
        }
        else
        {
            // Connected to an array -> use table values
            unitMin = unitMax = make_quiet_nan();

            if (auto item = m_calibrationTable->item(addr, 1))
                unitMin = item->data(Qt::EditRole).toDouble();

            if (auto item = m_calibrationTable->item(addr, 2))
                unitMax = item->data(Qt::EditRole).toDouble();

            m_cal->setCalibration(addr, unitMin, unitMax);
        }
    }
    clearPendingModifications();
}

bool CalibrationMinMaxConfigWidget::isValid() const
{
   return required_inputs_connected_and_valid(m_cal);
}

void CalibrationMinMaxConfigWidget::fillCalibrationTable(CalibrationMinMax *calib, double proposedMin, double proposedMax)
{
    Q_ASSERT(calib->getSlot(0)->isConnected());

    s32 paramCount = calib->getSlot(0)->inputPipe->parameters.size();

    m_calibrationTable->setRowCount(paramCount);

    for (s32 addr = 0; addr < paramCount; ++addr)
    {
        // address
        auto item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::DisplayRole, addr);
        m_calibrationTable->setItem(addr, 0, item);

        auto calibParams = calib->getCalibration(addr);
        double unitMin = calibParams.isValid() ? calibParams.unitMin : proposedMin;
        double unitMax = calibParams.isValid() ? calibParams.unitMax : proposedMax;

        // min
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, unitMin);
        m_calibrationTable->setItem(addr, 1, item);

        // max
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, unitMax);
        m_calibrationTable->setItem(addr, 2, item);

        // apply to this row
        auto pb_apply = new QPushButton("Apply calibration");
        pb_apply->setToolTip("Apply calibration values to this address.");
        pb_apply->setStatusTip(pb_apply->toolTip());
        connect(pb_apply, &QPushButton::clicked, this, [this, addr] () {
            double unitMin = spin_outputMin->value();
            double unitMax = spin_outputMax->value();

            auto minItem = m_calibrationTable->item(addr, 1);
            minItem->setData(Qt::EditRole, unitMin);

            auto maxItem = m_calibrationTable->item(addr, 2);
            maxItem->setData(Qt::EditRole, unitMax);

            calibModifiedButNotApplied_ = false;
        });

        m_calibrationTable->setCellWidget(addr, 3, pb_apply);
    }

    m_calibrationTable->resizeColumnsToContents();
    m_calibrationTable->resizeRowsToContents();
    calibModifiedButNotApplied_ = false;
}

//
// RateMonitorConfigWidget
//
RateMonitorConfigWidget::RateMonitorConfigWidget(RateMonitorSink *rms,
                                                 s32 userLevel,
                                                 AnalysisServiceProvider *asp,
                                                 QWidget *parent)
    : AbstractOpConfigWidget(rms, userLevel, asp, parent)
    , m_rms(rms)
{
    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textEdited, this, [this](const QString &newText) {
        // If the user clears the textedit reset NameEdited to false.
        this->setNameEdited(!newText.isEmpty());
    });

    le_name->setText(rms->objectName());

    // rate type selection and explanation of what each type does
    combo_type = new QComboBox;
    auto stack_descriptions = new QStackedWidget;

    for (const auto &info: RateTypeInfos)
    {
        combo_type->addItem(info.name, static_cast<s32>(info.type));

        auto label_description = new QLabel(info.description);
        label_description->setWordWrap(true);
        label_description->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        stack_descriptions->addWidget(label_description);
    }

    connect(combo_type, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            stack_descriptions, &QStackedWidget::setCurrentIndex);

    combo_type->setCurrentIndex(combo_type->findData(static_cast<s32>(m_rms->getType())));

    // rate history capacity
    spin_capacity = new QSpinBox;
    spin_capacity->setMinimum(1);
    spin_capacity->setMaximum(1u << 20);
    spin_capacity->setValue(m_rms->getRateHistoryCapacity());

    // unit label and calibration
    le_unit = new QLineEdit;
    le_unit->setText(m_rms->getUnitLabel());

    spin_factor = make_calibration_spinbox(QString());
    spin_factor->setValue(m_rms->getCalibrationFactor());
    spin_offset = make_calibration_spinbox(QString());
    spin_offset->setValue(m_rms->getCalibrationOffset());

    spin_dtSample = new QDoubleSpinBox;
    spin_dtSample->setDecimals(4);
    spin_dtSample->setMinimum(1e-20);
    spin_dtSample->setMaximum(1e+20);
    spin_dtSample->setSuffix(QSL(" s"));
    spin_dtSample->setValue(m_rms->getSamplingInterval());

    auto gb_calibration = new QGroupBox(QSL("Y-Axis (Rate Value Scaling)"));
    {
        auto l = new QFormLayout(gb_calibration);
        l->setContentsMargins(2, 2, 2, 2);
        l->addRow(QSL("Calibration Factor"), spin_factor);
        l->addRow(QSL("Calibration Offset"), spin_offset);

        auto label = new QLabel(QSL("resulting_rate = input_rate * factor + offset"));
        label->setFont(make_monospace_font());
        l->addRow(label);
    }

    // x-axis setup
    combo_xScaleType = new QComboBox;
    combo_xScaleType->addItem("Time", static_cast<int>(RateMonitorXScaleType::Time));
    combo_xScaleType->addItem("Samples", static_cast<int>(RateMonitorXScaleType::Samples));

    auto gb_xAxis = new QGroupBox(QSL("X-Axis")); // Sampling Interval (x-axis scaling)"));
    {
        auto label_dtSample = new QLabel(QSL(
                "Note: The interval does not affect the systems sampling frequency, only"
                " the x-axis time scale.\n"
                "For CounterDifference and PrecalculatedRate type samplers"
                " the sampling frequency is determined by the trigger rate"
                " of the corresponding VME event.\n"
                "FlowRate sampling is based on timeticks contained in the readout data stream."
                ));
        label_dtSample->setWordWrap(true);

        auto l = new QFormLayout(gb_xAxis);
        l->setContentsMargins(2, 2, 2, 2);
        l->addRow(QSL("X-Axis Type"), combo_xScaleType);
        l->addRow(QSL("Interval"), spin_dtSample);
        l->addRow(label_dtSample);

        connect(combo_xScaleType, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, label_dtSample] ()
                {
                    auto scaleType = static_cast<RateMonitorXScaleType>(combo_xScaleType->currentData().toInt());
                    spin_dtSample->setEnabled(scaleType == RateMonitorXScaleType::Time);
                    label_dtSample->setEnabled(scaleType == RateMonitorXScaleType::Time);
                    // Sample based x-axis needs a "sampling interval" of 1.0
                    if (scaleType == RateMonitorXScaleType::Samples)
                        spin_dtSample->setValue(1.0);
                });
    }

    combo_xScaleType->setCurrentIndex(combo_xScaleType->findData(static_cast<s32>(m_rms->getXScaleType())));

    // populate the layouts

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);
    formLayout->addRow(QSL("Name"), le_name);
    formLayout->addRow(QSL("Type"), combo_type);
    formLayout->addRow(QSL("Description"), stack_descriptions);
    formLayout->addRow(QSL("Max samples"), spin_capacity);
    formLayout->addRow(QSL("Y-Axis Label"), le_unit);
    formLayout->addRow(gb_calibration);
    formLayout->addRow(gb_xAxis);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    widgetLayout->addLayout(formLayout);
}

void RateMonitorConfigWidget::configureOperator()
{
    assert(required_inputs_connected_and_valid(m_op));

    m_rms->setObjectName(le_name->text());
    m_rms->setType(static_cast<RateMonitorSink::Type>(combo_type->currentData().toInt()));
    m_rms->setRateHistoryCapacity(spin_capacity->value());
    m_rms->setUnitLabel(le_unit->text());
    m_rms->setCalibrationFactor(spin_factor->value());
    m_rms->setCalibrationOffset(spin_offset->value());
    m_rms->setSamplingInterval(spin_dtSample->value());
    m_rms->setXScaleType(static_cast<RateMonitorXScaleType>(combo_xScaleType->currentData().toInt()));
}

void RateMonitorConfigWidget::inputSelected(s32 slotIndex)
{
    (void) slotIndex;

    OperatorInterface *op = m_op;

    if (no_input_connected(op) && !wasNameEdited())
    {
        le_name->clear();
        setNameEdited(false);
    }

    if (!wasNameEdited() && m_rms->getSlot(0)->isConnected())
    {
        auto name = make_input_source_text(m_rms->getSlot(0));

        switch (static_cast<RateMonitorSink::Type>(combo_type->currentData().toInt()))
        {
            case RateMonitorSink::Type::CounterDifference:
            case RateMonitorSink::Type::PrecalculatedRate:
                name += QSL(".rate");
                break;

            case RateMonitorSink::Type::FlowRate:
                name += QSL(".flowRate");
                break;
        }

        QSignalBlocker sb(le_name);
        le_name->setText(name);
    }

    if (required_inputs_connected_and_valid(op))
    {
        // Use the inputs unit label
        le_unit->setText(op->getSlot(0)->inputPipe->parameters.unit);
    }
}

bool RateMonitorConfigWidget::isValid() const
{
    return true;
}

//
// SelectConditionsDialog
//
SelectConditionsDialog::SelectConditionsDialog(const OperatorPtr &op, EventWidget *eventWidget)
    : ObjectEditorDialog(eventWidget)
    , m_eventWidget(eventWidget)
    , m_op(op)
{
    setWindowTitle(QSL("Select conditions for '%1'").arg(op->objectName()));

    auto conditionsFrame = new QFrame;
    m_buttonsGrid = new QGridLayout(conditionsFrame);
    m_buttonsGrid->setColumnStretch(0, 1);
    m_buttonsGrid->setColumnStretch(1, 0);

    auto conditionsGroupBox = new QGroupBox("Conditions");
    auto conditionsGroupBoxLayout = new QGridLayout(conditionsGroupBox);
    conditionsGroupBoxLayout->setContentsMargins(2, 2, 2, 2);
    conditionsGroupBoxLayout->addWidget(conditionsFrame, 0, 0, 1, 2);

    auto addConditionButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
    addConditionButton->setToolTip(QSL("Add condition"));

    connect(addConditionButton, &QPushButton::clicked,
            this, [this] () { addSelectButtons(); });

    auto gb_info = new QGroupBox("Info");
    auto l_info = make_hbox(gb_info);
    auto label_info = new QLabel;
    l_info->addWidget(label_info);
    label_info->setWordWrap(true);
    label_info->setText(QSL("Shows the active conditions for '%1 %2'. Use the plus button to add another selection row,"
        " then select the condition to use. The operator will only be run if all of its assigned conditions are true"
        " (AND over all conditions).")
        .arg(op->getDisplayName())
        .arg(op->objectName()));

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch();
    buttonLayout->addWidget(addConditionButton);
    conditionsGroupBoxLayout->addLayout(buttonLayout, 1, 0, 1, 2);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SelectConditionsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SelectConditionsDialog::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(buttonBox);

    auto layout = new QGridLayout(this);
    s32 row = 0;
    layout->addWidget(conditionsGroupBox, row++, 0);
    layout->addWidget(gb_info, row++, 0);
    layout->addLayout(buttonBoxLayout, row++, 0);

    if (auto analysis = op->getAnalysis())
    {
        auto conds = analysis->getActiveConditions(op).toList();
        std::sort(std::begin(conds), std::end(conds),
                  [] (auto &a, auto &b) { return a->objectName() < b->objectName(); });
        for (auto cond: conds)
            addSelectButtons(cond);
    }

    if (m_selectButtons.isEmpty())
        addSelectButtons();

    resize(450, 400);
}

void SelectConditionsDialog::addSelectButtons(const ConditionPtr &cond)
{
    auto selectButton = new QPushButton(cond ? cond->objectName() : QSL("<select>"));
    selectButton->setCheckable(true);
    selectButton->setMouseTracking(true);
    selectButton->installEventFilter(this);
    m_selectButtons.push_back(selectButton);
    m_selectedConditions.push_back(cond);
    int buttonIndex = m_selectButtons.size() - 1;

    auto clearButton  = new QPushButton(QIcon(":/dialog-close.png"), QString());

    int row = m_buttonsGrid->rowCount();
    m_buttonsGrid->addWidget(selectButton, row, 0);
    m_buttonsGrid->addWidget(clearButton, row, 1);

    connect(selectButton, &QPushButton::toggled,
            this, [=] (bool checked)
            {
                m_eventWidget->endSelectInput();

                if (checked)
                {
                    // uncheck all other buttons
                    for (auto button: m_selectButtons)
                        if (button != selectButton)
                            button->setChecked(false);

                    auto on_condition_selected = [=] (const ConditionPtr &cond)
                    {
                        m_selectedConditions[buttonIndex] = cond;
                        selectButton->setText(cond->objectName());
                        QSignalBlocker b(selectButton);
                        selectButton->setChecked(false);
                        m_inputSelectActive = false;
                    };

                    m_eventWidget->selectConditionFor(m_op, on_condition_selected);
                    m_inputSelectActive = true;
                }
            });

    connect(clearButton, &QPushButton::clicked,
            this, [=] ()
            {
                m_selectedConditions[buttonIndex] = {};
                m_selectButtons[buttonIndex]->setText("<select>");
            });
}

bool SelectConditionsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (auto button = qobject_cast<QPushButton *>(watched))
    {
        auto buttonIndex = m_selectButtons.indexOf(button);
        auto cond = m_selectedConditions.value(buttonIndex);

        if ((event->type() == QEvent::Enter || event->type() == QEvent::Leave)
            && !m_inputSelectActive && cond)
        {
            if (auto node = m_eventWidget->findNode(cond))
            {
                bool doColor = event->type() == QEvent::Enter;
                auto color = doColor ? InputNodeOfColor : QColor(0, 0, 0, 0);
                node->setBackground(0, color);

                for (node = node->parent();
                     node && node->type() == NodeType_Directory;
                     node = node->parent())
                {
                    auto color = doColor ? ChildIsInputNodeOfColor : QColor(0, 0, 0, 0);
                    node->setBackground(0, color);
                }
            }
        }
    }

    // Do not filter the event out.
    return false;
}

void SelectConditionsDialog::accept()
{
    AnalysisPauser pauser(m_eventWidget->getServiceProvider());
    auto analysis = m_eventWidget->getServiceProvider()->getAnalysis();
    analysis->clearConditionsUsedBy(m_op);

    for (auto cond: m_selectedConditions)
    {
        if (cond)
            analysis->addConditionLink(m_op, cond);
    }

    // clears histogram contents and other state
    m_op->clearState();

    QDialog::accept();
}

void SelectConditionsDialog::reject()
{
    QDialog::reject();
}


//
// PipeDisplay
//

PipeDisplay::PipeDisplay(Analysis *analysis, Pipe *pipe, bool showDecimals, QWidget *parent)
    : QWidget(parent)
    , m_analysis(analysis)
    , m_pipe(pipe)
    , m_showDecimals(showDecimals)
    , m_parameterTable(new QTableWidget)
{
    auto pb_toggleNumberDisplay = new QPushButton(QSL("&Toggle Number Format"));
    pb_toggleNumberDisplay->setToolTip("Toggle between automatic scientific notation and full digit number display");
    connect(pb_toggleNumberDisplay, &QPushButton::clicked,
        this, [this] {
            setShowDecimals(!doesShowDecimals());
            refresh();
        });

    auto closeButton = new QPushButton(QSL("&Close"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto buttonLayout = make_hbox();
    buttonLayout->addWidget(pb_toggleNumberDisplay);
    buttonLayout->addWidget(closeButton);

    auto layout = new QGridLayout(this);
    s32 row = 0;

    layout->addWidget(m_parameterTable, row++, 0);
    layout->addLayout(buttonLayout, row++, 0);
    layout->setRowStretch(1, 1);

    m_parameterTable->setColumnCount(4);
    m_parameterTable->setHorizontalHeaderLabels({"Valid", "Value", "Lower Limit", "Upper Limit"});
    m_parameterTable->setContextMenuPolicy(Qt::CustomContextMenu);


    auto copy_to_clipboard = [this]
    {
        auto sm = m_parameterTable->selectionModel();
        auto idx = sm->currentIndex();

        if (!idx.isValid())
            return;

        auto item = m_parameterTable->item(idx.row(), idx.column());

        if (!item)
            return;

        auto text = item->data(Qt::EditRole).toString();
        QGuiApplication::clipboard()->setText(text);
    };

    auto handle_table_context_menu = [this, copy_to_clipboard] (const QPoint &pos)
    {
        auto item = m_parameterTable->itemAt(pos);

        if (!item)
            return;

        QMenu menu;
        menu.addAction(QIcon::fromTheme("edit-copy"), QSL("Copy"), this, copy_to_clipboard, QKeySequence::Copy);
        menu.exec(m_parameterTable->mapToGlobal(pos));
    };

    connect(m_parameterTable, &QTableWidget::customContextMenuRequested, this, handle_table_context_menu);

    refresh();
}

void PipeDisplay::refresh()
{
    setWindowTitle(m_pipe->parameters.name);

    const auto unitLabel = m_pipe->parameters.unit;

    if (auto a2State = m_analysis->getA2AdapterState())
    {
        a2::PipeVectors pipe = find_output_pipe(a2State, m_pipe).first;

        m_parameterTable->setRowCount(pipe.data.size);

        QVector<QString> colStrings;
        colStrings.resize(m_parameterTable->columnCount());

        for (s32 pi = 0; pi < pipe.data.size; pi++)
        {
            double param = pipe.data[pi];
            double lowerLimit = pipe.lowerLimits[pi];
            double upperLimit = pipe.upperLimits[pi];

            int col = 0;
            colStrings[col++] = a2::is_param_valid(param) ? QSL("Y") : QSL("N");
            colStrings[col++] = formatParameter(param, unitLabel);
            colStrings[col++] = formatParameter(lowerLimit, unitLabel);
            colStrings[col++] = formatParameter(upperLimit, unitLabel);

            for (s32 ci = 0; ci < colStrings.size(); ci++)
            {
                auto item = m_parameterTable->item(pi, ci);
                if (!item)
                {
                    item = new QTableWidgetItem;
                    m_parameterTable->setItem(pi, ci, item);
                }

                item->setText(colStrings[ci]);
                item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            }

            if (!m_parameterTable->verticalHeaderItem(pi))
            {
                m_parameterTable->setVerticalHeaderItem(pi, new QTableWidgetItem);
            }

            m_parameterTable->verticalHeaderItem(pi)->setText(QString::number(pi));
        }

        m_parameterTable->resizeColumnsToContents();
        m_parameterTable->resizeRowsToContents();
    }
    else
    {
        m_parameterTable->setRowCount(0);
    }
}

QString PipeDisplay::formatParameter(double param, const QString &unitLabel)
{
    QString paramString;

    if (a2::is_param_valid(param))
    {
        if (doesShowDecimals())
            paramString = QString::number(param); // automatically uses scientific notation
        else
            paramString = QString::number(param, 'f', 0);

        if (!unitLabel.isEmpty())
            paramString += " " + unitLabel;
    }

    return paramString;
}

QWidget* CalibrationItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    auto result = QStyledItemDelegate::createEditor(parent, option, index);

    if (auto spin = qobject_cast<QDoubleSpinBox *>(result))
    {
        spin->setDecimals(8);
        spin->setMinimum(-1e20);
        spin->setMaximum(+1e20);
        spin->setSpecialValueText(QSL("not set"));
        spin->setValue(spin->minimum());
    }

    return result;
}

SessionErrorDialog::SessionErrorDialog(const QString &message, const QString &title, QWidget *parent)
    : QDialog(parent)
{
    if (!title.isEmpty())
    {
        setWindowTitle(title);
    }
    auto tb = new QTextBrowser(this);
    tb->setText(message);
    tb->setReadOnly(true);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(bb);
    bbLayout->addStretch(1);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tb);
    mainLayout->addLayout(bbLayout);

    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(600, 300);
    bb->button(QDialogButtonBox::Close)->setFocus();
}

ExportSinkStatusMonitor::ExportSinkStatusMonitor(const std::shared_ptr<ExportSink> &sink,
                                                 AnalysisServiceProvider *serviceProvider,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_sink(sink)
    , m_serviceProvider(serviceProvider)
    , label_outputDirectory(new QLabel)
    , label_fileName(new QLabel)
    , label_fileSize(new QLabel)
    , label_eventsWritten(new QLabel)
    , label_bytesWritten(new QLabel)
    , label_status(new QLabel)
    , pb_openDirectory(new QPushButton(QIcon(":/folder_orange.png"), QSL("Open")))
{
    label_status->setWordWrap(true);

    auto widgetLayout = new QFormLayout(this);

    {
        auto &l = widgetLayout;

        auto dirLayout = new QHBoxLayout;
        dirLayout->setContentsMargins(0, 0, 0, 0);
        dirLayout->setSpacing(2);
        dirLayout->addWidget(label_outputDirectory);
        dirLayout->addWidget(pb_openDirectory);

        l->addRow(QSL("Output Directory"),  dirLayout);
        l->addRow(QSL("Output File"),       label_fileName);
        l->addRow(QSL("Output File Size"),  label_fileSize);
        l->addRow(QSL("Bytes Written"),     label_bytesWritten);
        l->addRow(QSL("Events Written"),    label_eventsWritten);
        l->addRow(QSL("Status"),            label_status);
    }

    connect(pb_openDirectory, &QPushButton::clicked, this, [this]() {
        QString path = m_serviceProvider->getWorkspaceDirectory() + "/"
            + m_sink->getOutputPrefixPath();

        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    static const int UpdateInterval_ms = 1000;
    auto updateTimer = new QTimer(this);
    updateTimer->setInterval(UpdateInterval_ms);
    connect(updateTimer, &QTimer::timeout, this, &ExportSinkStatusMonitor::update);
    updateTimer->start();
    update();
}

void ExportSinkStatusMonitor::update()
{
    setWindowTitle(QSL("File Export %1").arg(m_sink->objectName()));

    auto a2_state = m_serviceProvider->getAnalysis()->getA2AdapterState();

    if (auto a2_sink = a2_state->operatorMap.value(m_sink.get(), nullptr))
    {
        auto d = reinterpret_cast<a2::ExportSinkData *>(a2_sink->d);
        auto runInfo = m_serviceProvider->getAnalysis()->getRunInfo();

        auto fileName = !runInfo.runId.isEmpty() ? QString::fromStdString(d->filename) : QSL("-");
        auto fileSize = QFileInfo(fileName).size();

        label_outputDirectory->setText(m_sink->getOutputPrefixPath());
        label_fileName->setText(fileName);
        label_fileSize->setText(format_number(fileSize, QSL("B"), UnitScaling::Binary));
        label_eventsWritten->setText(QString::number(d->eventsWritten));
        label_bytesWritten->setText(format_number(d->bytesWritten, QSL("B"), UnitScaling::Binary));

        auto lastError = QString::fromStdString(d->getLastError());

        if (!lastError.isEmpty())
        {
            label_status->setText(QSL("Error: ") + lastError);
        }
        else
        {
            label_status->setText(!runInfo.runId.isEmpty() ? QSL("Ok") : QSL("-"));
        }
    }
}

//
// EventSettingsDialog
//

struct EventSettingsDialog::Private
{
    const VMEConfig *vmeConfig_;
    Analysis::VMEObjectSettings settings_;
    QVector<QCheckBox *> check_multiEvent_;
};

EventSettingsDialog::EventSettingsDialog(
    const VMEConfig *vmeConfig,
    const Analysis::VMEObjectSettings &settings,
    QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    d->vmeConfig_ = vmeConfig;
    d->settings_ = settings;

    setWindowTitle(QSL("Analysis Event Settings"));

    bool hasEventBuilder = is_mvlc_controller(d->vmeConfig_->getControllerType());
    auto eventConfigs = d->vmeConfig_->getEventConfigs();

    const QStringList headers =
    {
        QSL("Multi Event Processing"),
        QSL("Event Builder Settings"),
    };

    auto table = new QTableWidget(eventConfigs.size(), headers.size());
    table->setHorizontalHeaderLabels(headers);

    for (int ei=0; ei<eventConfigs.size(); ei++)
    {
        auto eventConfig = eventConfigs[ei];
        auto eventSettings = d->settings_.value(eventConfig->getId());
        auto cb_multiEvent = new QCheckBox;
        auto pb_eventBuilderSettings = new QPushButton;
        pb_eventBuilderSettings->setIcon(QIcon(QSL(":/gear.png")));

        cb_multiEvent->setChecked(eventSettings.value("MultiEventProcessing", false).toBool());

        table->setVerticalHeaderItem(ei, new QTableWidgetItem(eventConfig->objectName()));
        table->setCellWidget(ei, 0, make_centered(cb_multiEvent));
        table->setCellWidget(ei, 1, make_centered(pb_eventBuilderSettings));

        if (!hasEventBuilder)
        {
            //table->cellWidget(ei, 0)->setEnabled(false);
            table->cellWidget(ei, 1)->setEnabled(false);
        }

        d->check_multiEvent_.push_back(cb_multiEvent);

        auto run_event_builder_settings_dialog = [this, eventConfig] ()
        {
            // GUI to set the main module and the timestamp window for each
            // module in the current event.

            auto moduleConfigs = eventConfig->getModuleConfigs();
            auto combo_mainModule = new QComboBox();

            for (auto moduleConfig: moduleConfigs)
                combo_mainModule->addItem(moduleConfig->objectName(), moduleConfig->getId());

            auto ebSettings = (this->d->settings_.value(eventConfig->getId())
                               .value("EventBuilderSettings").toMap());

            auto mainModuleId = ebSettings.value("MainModule").toUuid();

            if (!mainModuleId.isNull())
                combo_mainModule->setCurrentIndex(combo_mainModule->findData(mainModuleId));
            else
                combo_mainModule->setCurrentIndex(combo_mainModule->count() - 1);

            auto eventSettings = d->settings_.value(eventConfig->getId());

            auto cb_enableEventBuilder = new QCheckBox;
            cb_enableEventBuilder->setChecked(eventSettings.value("EventBuilderEnabled", false).toBool());

            auto matchWindows = ebSettings.value("MatchWindows").toMap();

            QVector<QSpinBox *> spins_lowerLimits;
            QVector<QSpinBox *> spins_upperLimits;
            QVector<QCheckBox *> checks_ignoredModules;
            // TODO: add a column for the module type name, e.g. mdpp16_scp
            auto tableMatchWindows = new QTableWidget(moduleConfigs.size(), 3);
            tableMatchWindows->setHorizontalHeaderLabels({"Lower", "Upper", "Ignore Module"});

            for (int mi=0; mi<moduleConfigs.size(); ++mi)
            {
                auto spin_lower = new QSpinBox();
                auto spin_upper = new QSpinBox();
                for (auto spin: { spin_lower, spin_upper })
                {
                    spin->setMinimum(std::numeric_limits<s32>::lowest());
                    spin->setMaximum(std::numeric_limits<s32>::max());
                }
                auto moduleConfig = moduleConfigs.at(mi);
                auto matchWindow = matchWindows.value(moduleConfig->getId().toString()).toMap();
                spin_lower->setValue(matchWindow.value("lower", mesytec::mvlc::event_builder::DefaultMatchWindow.first).toInt());
                spin_upper->setValue(matchWindow.value("upper", mesytec::mvlc::event_builder::DefaultMatchWindow.second).toInt());

                spins_lowerLimits.push_back(spin_lower);
                spins_upperLimits.push_back(spin_upper);

                auto cb_ignoreModule = new QCheckBox;
                cb_ignoreModule->setChecked(matchWindow.value("ignoreModule", false).toBool());

                checks_ignoredModules.push_back(cb_ignoreModule);

                tableMatchWindows->setVerticalHeaderItem(mi, new QTableWidgetItem(moduleConfig->objectName()));
                tableMatchWindows->setCellWidget(mi, 0, spin_lower);
                tableMatchWindows->setCellWidget(mi, 1, spin_upper);
                tableMatchWindows->setCellWidget(mi, 2, make_centered(cb_ignoreModule));
            }

            auto gbMatchWindows = new QGroupBox("Module timestamp match settings");
            auto gbl = make_hbox(gbMatchWindows);
            gbl->addWidget(tableMatchWindows);

            // FIXME: memory limit is global to the event builder, not specific to an event...
            //auto spin_memoryLimit = new QDoubleSpinBox;
            //spin_memoryLimit->setMinimum(0.0);
            //spin_memoryLimit->setMaximum(1000.0);
            //spin_memoryLimit->setDecimals(1);
            //spin_memoryLimit->setSingleStep(1.0);
            //spin_memoryLimit->setSuffix(" GB");
            //spin_memoryLimit->setValue(ebSettings.value(
            //        "MemoryLimit", static_cast<qulonglong>(mesytec::mvlc::DefaultMemoryLimit)).toDouble() / Gigabytes(1));

            auto fl = new QFormLayout;
            fl->addRow("Enable Event Builder", cb_enableEventBuilder);
            fl->addRow("Main/Reference Module", combo_mainModule);
            fl->addRow(gbMatchWindows);
            //fl->addRow("Memory Limit", spin_memoryLimit);

            auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
            auto bbl = make_hbox();
            bbl->addStretch(1);
            bbl->addWidget(bb);

            auto dl = make_vbox<4, 4>();
            dl->addLayout(fl);
            dl->addLayout(bbl);
            dl->setStretch(0, 1);

            QDialog dia;
            dia.setLayout(dl);
            dia.resize(500, 300);
            dia.setWindowTitle(QSL("Event Builder Settings for %1").arg(eventConfig->objectName()));

            QObject::connect(bb, &QDialogButtonBox::accepted, &dia, &QDialog::accept);
            QObject::connect(bb, &QDialogButtonBox::rejected, &dia, &QDialog::reject);
            QObject::connect(bb, &QDialogButtonBox::helpRequested,
                             &dia, mesytec::mvme::make_help_keyword_handler("EventBuilder"));

            if (dia.exec() == QDialog::Accepted)
            {
                // On dialog accept apply the modifications to the local copy
                // of the VMEObjectSettings.

                QVariantMap matchWindows;
                for (int mi=0; mi<moduleConfigs.size(); ++mi)
                {
                    auto id = moduleConfigs[mi]->getId();
                    QVariantMap matchWindow;
                    matchWindow["lower"] = spins_lowerLimits[mi]->value();
                    matchWindow["upper"] = spins_upperLimits[mi]->value();
                    matchWindow["ignoreModule"] = checks_ignoredModules[mi]->isChecked();
                    matchWindows[id.toString()] = matchWindow;
                }

                QVariantMap ebSettings;

                // Stores the uuid of the main module
                ebSettings["MainModule"] = combo_mainModule->currentData();
                ebSettings["MatchWindows"] = matchWindows;
                //ebSettings["MemoryLimit"] = spin_memoryLimit->value() * Gigabytes(1);

                this->d->settings_[eventConfig->getId()]["EventBuilderSettings"] = ebSettings;
                this->d->settings_[eventConfig->getId()]["EventBuilderEnabled"] = cb_enableEventBuilder->isChecked();
            }
        };

        connect(pb_eventBuilderSettings, &QPushButton::clicked,
                this, run_event_builder_settings_dialog);
    }

    auto gbSettings = new QGroupBox(QSL("Event settings"));
    auto settingsLayout = new QVBoxLayout(gbSettings);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->addWidget(table);

    auto label = new QLabel(QSL("Changes become active on the next DAQ/Replay start."));
    set_widget_font_pointsize_relative(label, -1);
    settingsLayout->addWidget(label);

    auto dialogLayout = new QVBoxLayout(this);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(bb);

    dialogLayout->addWidget(gbSettings);
    dialogLayout->addLayout(bbLayout);
    dialogLayout->setStretch(0, 1);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(bb, &QDialogButtonBox::helpRequested,
                     this, mesytec::mvme::make_help_keyword_handler("Analysis Processing Chain"));
    resize(600, 300);
}

EventSettingsDialog::~EventSettingsDialog()
{
}

Analysis::VMEObjectSettings EventSettingsDialog::getSettings() const
{
    auto eventConfigs = d->vmeConfig_->getEventConfigs();
    auto settings = d->settings_;

    for (int ei=0; ei<eventConfigs.size(); ei++)
    {
        if (auto checkbox = d->check_multiEvent_.value(ei))
            settings[eventConfigs[ei]->getId()][QSL("MultiEventProcessing")] = checkbox->isChecked();

        // TODO: maybe do display the 'eb enabled' checkbox again here in the outer overview table
        //if (auto checkbox = d->checks_eventBuilder.value(ei))
        //    settings[eventConfigs[ei]->getId()][QSL("EventBuilderEnabled")] = checkbox->isChecked();
    }

    return settings;
}

//
// ModuleSettingsDialog
//

ModuleSettingsDialog::ModuleSettingsDialog(const ModuleConfig *moduleConfig,
                                           const QVariantMap &settings,
                                           QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_filterEdit(makeFilterEdit())
{
    setWindowTitle(QSL("Analysis Module Settings"));

    auto gbSettings = new QGroupBox(QSL("Module settings"));
    auto settingsLayout = new QFormLayout(gbSettings);
    settingsLayout->addRow(QSL("Multi Event Header Filter"), m_filterEdit);
    auto label = new QLabel(QSL(
            "Used to split the module data section into individual events.<br/>"
            "Only has an effect if Multi Event Processing is enabled for the"
            " current event.<br/>"
            "Changes become active on the next DAQ/Replay start."
            ));
    label->setWordWrap(true);

    set_widget_font_pointsize_relative(label, -1);
    settingsLayout->addRow(label);

    auto dialogLayout = new QVBoxLayout(this);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(bb);

    dialogLayout->addWidget(gbSettings);
    dialogLayout->addLayout(bbLayout);
    dialogLayout->setStretch(0, 1);

    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // populate
    QString filterString = settings.value(QSL("MultiEventHeaderFilter")).toString();

    if (filterString.isEmpty())
    {
        filterString = moduleConfig->getModuleMeta().eventHeaderFilter;
    }

    m_filterEdit->setFilterString(filterString);
}

void ModuleSettingsDialog::accept()
{
    m_settings.insert(QSL("MultiEventHeaderFilter"), m_filterEdit->text().trimmed());

    QDialog::accept();
}

//
// MVLCParserDebugHandler
//
using namespace mesytec::mvme_mvlc;
using namespace mesytec::mvlc;

MVLCParserDebugHandler::MVLCParserDebugHandler(QObject *parent)
    : QObject(parent)
    , m_geometrySaver(new WidgetGeometrySaver(this))
{
}

inline const char *to_string(const readout_parser::ReadoutParserState::GroupParseState &mps)
{
    switch (mps)
    {
        case readout_parser::ReadoutParserState::Prefix:
            return "Prefix";
        case readout_parser::ReadoutParserState::Dynamic:
            return "Dynamic";
        case readout_parser::ReadoutParserState::Suffix:
            return "Suffix";
    }

    return "unknown ModuleParseState";
}

namespace
{

struct InterestingOffsets
{
    std::set<u32> ethHeaders;
    std::set<u32> systemFrames;
    std::set<u32> stackFrames;
};

void find_headers_inside_eth_packet(
    const u32 *bufferBegin,
    basic_string_view<u32> packetInput,
    InterestingOffsets &dest)
{
    eth::PayloadHeaderInfo ethHdrs{ packetInput[0], packetInput[1] };
    auto packetTotalWords = eth::HeaderWords + ethHdrs.dataWordCount();

    if (ethHdrs.isNextHeaderPointerPresent()
        && ethHdrs.nextHeaderPointer() < ethHdrs.dataWordCount()
        && packetInput.size() >= packetTotalWords)
    {
        packetInput.remove_prefix(eth::HeaderWords + ethHdrs.nextHeaderPointer());

        while (!packetInput.empty())
        {
            const u32 frameHeader = packetInput[0];
            const auto frameInfo = extract_frame_info(frameHeader);

            if (frameInfo.type == frame_headers::StackFrame
                || frameInfo.type == frame_headers::StackContinuation)
            {
                dest.stackFrames.insert(packetInput.data() - bufferBegin);
                packetInput.remove_prefix(
                    std::min(static_cast<size_t>(frameInfo.len + 1u), packetInput.size()));
            }
            else
                break; // don't get stuck
        }
    }
}

InterestingOffsets find_interesting_headers_eth(const DataBuffer &buffer)
{
    // To find StackFrames and StackContinuations we do not need to keep track
    // of the frame state. Either nextHeaderPointer is set and from that first
    // header in the packet we can follow the framing until the end of the
    // packet or there just is no header in the packets payload.

    assert(buffer.tag == static_cast<int>(ListfileBufferFormat::MVLC_ETH));

    InterestingOffsets result;
    const auto bufferBegin = buffer.asU32(0);
    basic_string_view<u32> input(bufferBegin, buffer.usedU32());

    while (!input.empty())
    {
        auto offset = input.data() - bufferBegin;

        if (get_frame_type(input[0]) == frame_headers::SystemEvent)
        {
            auto frameHeader = input[0];
            result.systemFrames.insert(offset);
            input.remove_prefix(extract_frame_info(frameHeader).len + 1);
        }
        else if (input.size() >= eth::HeaderWords)
        {
            // insert offsets to both headers 0 and 1
            result.ethHeaders.insert(offset);
            result.ethHeaders.insert(offset+1);

            // handle the packet
            eth::PayloadHeaderInfo ethHdrs{ input[0], input[1] };
            auto packetTotalWords = eth::HeaderWords + ethHdrs.dataWordCount();

            find_headers_inside_eth_packet(
                bufferBegin,
                basic_string_view<u32>(input.data(), packetTotalWords),
                result);

            input.remove_prefix(packetTotalWords);
        }
        else
            break; // do not get stuck
    }

    return result;
}

InterestingOffsets find_interesting_headers_usb(const DataBuffer &buffer)
{
    assert(buffer.tag == static_cast<int>(ListfileBufferFormat::MVLC_USB));

    InterestingOffsets result;
    const auto bufferBegin = buffer.asU32(0);
    basic_string_view<u32> input(bufferBegin, buffer.usedU32());

    while (!input.empty())
    {
        auto offset = input.data() - bufferBegin;

        const u32 frameHeader = input[0];
        const auto frameInfo = extract_frame_info(frameHeader);

        if (frameInfo.type == frame_headers::SystemEvent)
        {
            result.systemFrames.insert(offset);
        }
        else if (frameInfo.type == frame_headers::StackFrame
                 || frameInfo.type == frame_headers::StackContinuation)
        {
            result.stackFrames.insert(offset);
        }
        else
            break; // do not get stuck

        input.remove_prefix(
            std::min(static_cast<size_t>(frameInfo.len + 1u), input.size()));
    }

    return result;
}

void mvlc_parser_debug_log_buffer(const DataBuffer &buffer, QTextStream &out)
{
    // Preparations: collect interesting offsets for highlighting later.
    InterestingOffsets offsets;

    const bool isEth = (buffer.tag == static_cast<int>(ListfileBufferFormat::MVLC_ETH));

    if (isEth)
        offsets = find_interesting_headers_eth(buffer);
    else
        offsets = find_interesting_headers_usb(buffer);

    // output
    static const u32 wordsPerRow = 8;

    QString strbuf;

    BufferIterator iter(buffer.data, buffer.used);

    while (iter.longwordsLeft())
    {
        u32 nWords = std::min(wordsPerRow, iter.longwordsLeft());

        out << QString("%1: ").arg(iter.current32BitOffset(), 6);

        for (u32 i=0; i<nWords; ++i)
        {
            u32 currentOffset = iter.current32BitOffset();
            u32 currentWord = iter.extractU32();

            QString cssClass;

            if (isEth && offsets.ethHeaders.count(currentOffset))
                cssClass = QSL("ethHeader");
            else if (offsets.systemFrames.count(currentOffset))
                cssClass = QSL("systemFrame");
            else if (offsets.stackFrames.count(currentOffset))
                cssClass = QSL("stackFrame");

            if (!cssClass.isEmpty())
            {
                out << QString("<span class='%1'>0x%2</span> ")
                    .arg(cssClass)
                    .arg(currentWord, 8, 16, QLatin1Char('0'));
            }
            else
            {
                out << QString("0x%1 ").arg(currentWord, 8, 16, QLatin1Char('0'));
            }
        }

        out << endl;
    }
}

}

void MVLCParserDebugHandler::handleDebugInfo(
    const DataBuffer &buffer,
    mesytec::mvlc::readout_parser::ReadoutParserState parserState,
    const mesytec::mvlc::readout_parser::ReadoutParserCounters &/*parserCounters_*/, // FIXME: currently not used
    const VMEConfig *vmeConfig,
    const analysis::Analysis *analysis)
{

    qDebug() << __PRETTY_FUNCTION__ << buffer.used << parserState.workBuffer.used;

    const auto styles = QSL(
        ".systemFrame { background-color: Salmon; }\n"
        ".ethHeader   { background-color: LightBlue; }\n"
        ".stackFrame  { background-color: GoldenRod; }\n"
        );

    QString rawBufferText;

    // Log the initial parser state and the raw buffer contents.
    {
        QTextStream out(&rawBufferText);

        // Log buffer information and buffer contents

        out << "<html><body><pre>";
        out << "buffer number = " << buffer.id
            << ", last buffer number = " << parserState.lastBufferNumber <<  endl;
        out << "buffer size = " << buffer.used / sizeof(u32) << endl;
        out << "buffer format is " << to_string(static_cast<ListfileBufferFormat>(buffer.tag)) << endl;
        out << "last ETH packet number = " << parserState.lastPacketNumber << endl;

        out << "ParserState: eventIndex=" << parserState.eventIndex
            << ", moduleIndex=" << parserState.moduleIndex
            << ", moduleParseState=" << to_string(parserState.groupParseState)
            << endl;

        out << "Parser readoutInfo:" << endl;

        for (const auto &eventIter: parserState.readoutStructure | indexed(0))
        {
            int eventIndex = eventIter.index();
            for (const auto &moduleIter: eventIter.value() | indexed(0))
            {
#if 0
                int moduleIndex = moduleIter.index();
                const auto &moduleParts = moduleIter.value();

                bool isDynamic = moduleParts.len < 0;

                out << "  ei=" << eventIndex
                    << ", mi=" << moduleIndex;

                if (isDynamic)
                    out << ", dynamic size";
                else
                    out << ", size=" << moduleParts.len;

                if (auto moduleConfig = vmeConfig->getModuleConfig(eventIndex, moduleIndex))
                    out << ", name=" << moduleConfig->objectName();

                out << endl;
#else
                int moduleIndex = moduleIter.index();
                const auto &moduleParts = moduleIter.value();

                out << "  eventIndex=" << eventIndex
                    << ", moduleIndex=" << moduleIndex
                    << ": prefixLen=" << static_cast<unsigned>(moduleParts.prefixLen)
                    << ", hasDynamic=" << moduleParts.hasDynamic
                    << ", suffixLen=" << static_cast<unsigned>(moduleParts.suffixLen)
                    << ", name=" << moduleParts.name.c_str()
                    << endl;
#endif
            }
        }

        out << QSL("curStackFrame: 0x%1, wordsLeft=%2\n")
            .arg(parserState.curStackFrame.header, 8, 16, QLatin1Char('0'))
            .arg(parserState.curStackFrame.wordsLeft)
            ;

        out << QSL("curBlockFrame: 0x%1, wordsLeft=%2\n")
            .arg(parserState.curBlockFrame.header, 8, 16, QLatin1Char('0'))
            .arg(parserState.curBlockFrame.wordsLeft)
            ;

        out << "Legend: ";
        out << "<span class='ethHeader'>ethPacketHeader</span>, ";
        out << "<span class='systemFrame'>systemFrame</span>, ";
        out << "<span class='stackFrame'>stackFrame</span>";
        out << endl;
        out << "----------" << endl;

        mvlc_parser_debug_log_buffer(buffer, out);
        out << "</pre></body></html>";
    }

    using ModuleData = mesytec::mvme::multi_event_splitter::ModuleData;
    bool usesMultiEventSplitting = uses_multi_event_splitting(*vmeConfig, *analysis);
    std::error_code multiEventSplitterError;
    mesytec::mvme::multi_event_splitter::State multiEventSplitter;
    QString parserText;
    QString splitterText;

    // Messy way to setup parser and splitter callbacks and run them both. They log their outputs
    // into parserText and splitterText respectively.
    {
        QTextStream parserOut(&parserText);
        QTextStream splitterOut(&splitterText);

        mesytec::mvme::multi_event_splitter::Callbacks splitterCallbacks;
        readout_parser::ReadoutParserCallbacks parserCallbacks;

        // Setup parser callbacks and run the parser on the input buffer. This will
        // be exactly the same state, data and code as was run on the analysis
        // thread except for different callbacks.

        if (usesMultiEventSplitting)
        {
            auto filterStrings = collect_multi_event_splitter_filter_strings(
                *vmeConfig, *analysis);

            std::tie(multiEventSplitter, multiEventSplitterError) = mesytec::mvme::multi_event_splitter::make_splitter(filterStrings);

            if (multiEventSplitterError)
            {
                splitterOut << QString("Error setting up multi event splitting: %1").arg(
                    multiEventSplitterError.message().c_str()) << endl;
            }
            else
            {
                // Factory function for module callbacks which log their input data.
                auto make_module_callback = [&splitterOut] (const QString &typeString)
                {
                    return [&splitterOut, typeString] (int ei, int mi, const u32 *data, u32 size)
                    {
                        splitterOut << QString("  module%1, ei=%2, mi=%3, size=%4:")
                            .arg(typeString).arg(ei).arg(mi).arg(size)
                            << endl;

                        ::logBuffer(BufferIterator(const_cast<u32 *>(data), size),
                                    [&splitterOut] (const QString &str)
                                    {
                                        splitterOut << "    " << str << endl;
                                    });
                    };
                };

                splitterCallbacks.eventData = [&splitterOut, make_module_callback] (
                    void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
                {
                    splitterOut << "beginEvent(ei=" << ei << ")" << endl;

                    for (unsigned mi=0; mi<moduleCount; ++mi)
                    {
                        auto &moduleData = moduleDataList[mi];

                        make_module_callback("Data")(
                            ei, mi, moduleData.data.data, moduleData.data.size);
                    }

                    splitterOut << "endEvent(ei=" << ei << ")" << endl;
                };

                splitterCallbacks.logger = [&splitterOut] (void *, const std::string &msg)
                {
                    splitterOut << QSL("Error: ") <<  msg.c_str();
                };
            }
        }

        //
        // End of multi event splitter setup. Now setup the readout parser
        // callback. These will receive and log the input data and hand it
        // down to the splitter code if splitting is enabled.

        auto log_module_part = [&parserOut] (
            const QString &partName, int ei, int mi, const u32 *data, u32 size)
        {
            parserOut << QString("  module%4, ei=%1, mi=%2, size=%3:")
                .arg(ei).arg(mi).arg(size).arg(partName)
                << endl;

            ::logBuffer(
                BufferIterator(const_cast<u32 *>(data), size), [&parserOut] (const QString &str)
                {
                    parserOut << "    " << str << endl;
                });
        };

        parserCallbacks.eventData = [&] (
            void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            parserOut << "beginEvent(ei=" << ei << ")" << endl;

            for (unsigned mi=0; mi<moduleCount; ++mi)
            {
                auto &moduleData = moduleDataList[mi];

                log_module_part("Data", ei, mi, moduleData.data.data, moduleData.data.size);
            }

            parserOut << "endEvent(ei=" << ei << ")" << endl;

            if (usesMultiEventSplitting && !multiEventSplitterError)
            {
                auto countersPrecall = multiEventSplitter.counters;
                multiEventSplitter.processingFlags = {};
                mesytec::mvme::multi_event_splitter::event_data(
                    multiEventSplitter, splitterCallbacks,
                    nullptr, ei, moduleDataList, moduleCount);
                splitterOut << "========================================" << endl;
            }
        };

        parserOut << "Running readout parser..." << endl;

        readout_parser::ParseResult pr = {};
        readout_parser::ReadoutParserCounters parserCounters({});

        try
        {
            if (buffer.tag == static_cast<int>(ListfileBufferFormat::MVLC_ETH))
            {
                // Input buffers are MVLC_ETH formatted buffers as generated by
                // MVLCReadoutWorker::readout_eth().
                pr = parse_readout_buffer_eth(
                    parserState, parserCallbacks, parserCounters,
                    buffer.id, buffer.asU32(0), buffer.used/sizeof(u32));
            }
            else if (buffer.tag == static_cast<int>(ListfileBufferFormat::MVLC_USB))
            {
                // Input buffers are MVLC_USB formatted buffers as generated by
                // MVLCReadoutWorker::readout_usb()
                pr = parse_readout_buffer_usb(
                    parserState, parserCallbacks, parserCounters,
                    buffer.id, buffer.asU32(0), buffer.used/sizeof(u32));
            }
            else
                throw std::runtime_error("unexpected buffer format (expected MVLC_ETH or MVLC_USB)");

            parserOut << "parser returned " << get_parse_result_name(pr) << endl;
        }
        catch (const end_of_buffer &e)
        {
            parserOut << "end_of_buffer from parser: " << e.what() << endl;
        }
        catch (const std::exception &e)
        {
            parserOut << "exception from parser: " << e.what() << endl;
        }
        catch (...)
        {
            parserOut << "unknown exception from parser!" << endl;
        }
    }

    auto make_searchable_text_widget = [](QTextEdit *te)
        -> std::pair<QWidget *, TextEditSearchWidget *>
    {
        auto parserResultWidget = new QWidget;
        auto resultLayout = make_layout<QVBoxLayout, 0, 0>(parserResultWidget);
        auto searchWidget = new TextEditSearchWidget(te);
        resultLayout->addWidget(searchWidget);
        resultLayout->addWidget(te);
        resultLayout->setStretch(1, 1);

        return std::make_pair(parserResultWidget, searchWidget);
    };

    // Display the buffer contents and the parser results side-by-side in two
    // (or three if multievent splitting is enabled) QTextBrowsers.
    {
        auto widget = new QWidget;
        widget->setAttribute(Qt::WA_DeleteOnClose);
        widget->setWindowTitle("MVLC Readout Parser Debug");
        widget->setFont(make_monospace_font());
        m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/MVLCReadoutParserDebug"));
        add_widget_close_action(widget);

        // parser state and buffer contents on the left side
        auto tb_buffer = new QTextBrowser;
        auto doc_buffer = new QTextDocument(tb_buffer);
        doc_buffer->setDefaultStyleSheet(styles);
        doc_buffer->setHtml(rawBufferText);
        tb_buffer->setDocument(doc_buffer);
        auto bufferWidget = make_searchable_text_widget(tb_buffer).first;

        // parser result on the right side
        auto tb_parserResult = new QTextBrowser;
        tb_parserResult->setText(parserText);
        auto parserResultWidget = make_searchable_text_widget(tb_parserResult).first;

        auto splitter = new QSplitter;
        splitter->addWidget(bufferWidget);
        splitter->addWidget(parserResultWidget);

        if (usesMultiEventSplitting)
        {
            std::ostringstream ss;
            mesytec::mvme::multi_event_splitter::format_counters_tabular(ss, multiEventSplitter.counters);
            QString countersText = "Multi Event Splitter Counters:\n" + QString::fromStdString(ss.str());
            countersText += "\n========================================\n";
            splitterText.prepend(countersText);
            auto tb_splitterResult = new QTextBrowser;
            tb_splitterResult->setText(splitterText);
            auto splitterResultWidget = make_searchable_text_widget(tb_splitterResult).first;
            splitter->addWidget(splitterResultWidget);
        }

        auto wl = make_layout<QHBoxLayout, 0, 0>(widget);
        wl->addWidget(splitter);
        widget->show();
    }
}

MVLCSingleStepHandler::MVLCSingleStepHandler(
    Logger logger, QObject *parent)
    : QObject(parent)
    , m_logger(logger)
{
}

void MVLCSingleStepHandler::handleSingleStepResult(
    const EventRecord &record)
{
    qDebug() << __PRETTY_FUNCTION__
        << "record.eventIndex=" << record.eventIndex
        << "record.modulesData.size() =" << record.modulesData.size();

    for (const auto &kv: record.modulesData | indexed(0))
    {
        int moduleIndex = kv.index();
        const auto &moduleData = kv.value();

        if (is_empty(moduleData))
        {
            m_logger(QString("SingleStep: eventIndex=%1, moduleIndex=%2: empty module data")
                     .arg(record.eventIndex)
                     .arg(moduleIndex));
            continue;
        }

        m_logger(QString("SingleStep: eventIndex=%1, moduleIndex=%2:")
                 .arg(record.eventIndex)
                 .arg(moduleIndex));

        auto bufferLogger = [this] (const QString &msg)
        {
            m_logger("    " + msg);
        };

        auto handle_part = [&](const QVector<u32> &data, const QString &typeString)
        {
            m_logger(QString("  %1 (words=%2, bytes=%3):")
                     .arg(typeString)
                     .arg(data.size())
                     .arg(data.size() * sizeof(u32)));

            ::logBuffer(data, bufferLogger);
        };

        if (moduleData.data.size() > 0)
            handle_part(moduleData.data, "moduleData");
    }

    m_logger("---");
}

} // end namespace ui
} // end namespace analysis
