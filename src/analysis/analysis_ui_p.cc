#include "analysis_ui_p.h"
#include "data_extraction_widget.h"
#include "../globals.h"
#include "../histo_util.h"
#include "../vme_config.h"
#include "../mvme_context.h"
#include "../qt_util.h"

#include <limits>
#include <QButtonGroup>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QRadioButton>

namespace analysis
{
//
// AddEditSourceWidget
//
QByteArray getDefaultFilter(u8 moduleType)
{
    // defined in globals.h
    //return defaultDataFilters.value(moduleType).value(0).filter;
    // FIXME: implement this
    return QByteArray();
}

const char *getDefaultFilterName(u8 moduleType)
{
    // defined in globals.h
    //return defaultDataFilters.value(moduleType).value(0).name;
    // FIXME: implement this
    return "";
}

QVector<std::shared_ptr<Extractor>> get_default_data_extractors(const QString &moduleTypeName)
{
    QVector<std::shared_ptr<Extractor>> result;

    QDir moduleDir(vats::get_module_path(moduleTypeName));
    QFile filtersFile(moduleDir.filePath("analysis/default_filters.analysis"));

    if (filtersFile.open(QIODevice::ReadOnly))
    {
        auto doc = QJsonDocument::fromJson(filtersFile.readAll());
        Analysis filterAnalysis;
        auto readResult = filterAnalysis.read(doc.object()[QSL("AnalysisNG")].toObject());

        if (readResult.code == Analysis::ReadResult::NoError)
        {
            for (auto entry: filterAnalysis.getSources())
            {
                auto extractor = std::dynamic_pointer_cast<Extractor>(entry.source);
                if (extractor)
                {
                    result.push_back(extractor);
                }
            }
        }
    }

    qSort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a->objectName() < b->objectName();
    });

    return result;
}

/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * accepting the widget inputs it will call eventWidget->addSource(). */
AddEditSourceWidget::AddEditSourceWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget)
    : AddEditSourceWidget(srcPtr.get(), mod, eventWidget)
{
    m_srcPtr = srcPtr;
    setWindowTitle(QString("New  %1").arg(srcPtr->getDisplayName()));

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
    spin_unitMax->setValue(1 << 16); // FIXME: find a better default value. Maybe input dependent (upperLimit)

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

    m_optionsLayout->addRow(m_gbGenHistograms);

    // Load data from the first template into the gui
    applyTemplate(0);
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepting the widget inputs it will call eventWidget->sourceEdited(). */
AddEditSourceWidget::AddEditSourceWidget(SourceInterface *src, ModuleConfig *module, EventWidget *eventWidget)
    : QDialog(eventWidget)
    , m_src(src)
    , m_module(module)
    , m_eventWidget(eventWidget)
    , m_gbGenHistograms(nullptr)
{
    setWindowTitle(QString("Edit %1").arg(m_src->getDisplayName()));
    add_widget_close_action(this);

    m_defaultExtractors = get_default_data_extractors(module->getModuleMeta().typeName);

    // Read filter templates for the given module
    m_templateCombo = new QComboBox;
    for (auto &ex: m_defaultExtractors)
    {
        m_templateCombo->addItem(ex->objectName());
    }

    auto applyTemplateButton = new QPushButton(QSL("Load Template"));
    applyTemplateButton->setAutoDefault(false);
    applyTemplateButton->setDefault(false);
    auto templateSelectLayout = new QHBoxLayout;
    templateSelectLayout->setContentsMargins(0, 0, 0, 0);
    templateSelectLayout->addWidget(m_templateCombo);
    templateSelectLayout->addWidget(applyTemplateButton);
    templateSelectLayout->setStretch(0, 1);

    connect(applyTemplateButton, &QPushButton::clicked, this, [this]() { applyTemplate(m_templateCombo->currentIndex()); });

    auto extractor = qobject_cast<Extractor *>(src);
    Q_ASSERT(extractor);
    Q_ASSERT(module);

    le_name = new QLineEdit;
    m_filterEditor = new DataExtractionEditor(extractor->getFilter().getSubFilters());
    m_filterEditor->setMinimumHeight(125);
    m_filterEditor->setMinimumWidth(550);

    m_spinCompletionCount = new QSpinBox;
    m_spinCompletionCount->setMinimum(1);
    m_spinCompletionCount->setMaximum(std::numeric_limits<int>::max());

    if (!extractor->objectName().isEmpty())
    {
        le_name->setText(QString("%1").arg(extractor->objectName()));
    }
    else
    {
        le_name->setText(QString("%1.%2").arg(module->objectName()).arg(getDefaultFilterName(module->getModuleMeta().typeId)));
    }

    m_spinCompletionCount->setValue(extractor->getRequiredCompletionCount());

    m_optionsLayout = new QFormLayout;
    m_optionsLayout->addRow(QSL("Name"), le_name);
    m_optionsLayout->addRow(QSL("Required Completion Count"), m_spinCompletionCount);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditSourceWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditSourceWidget::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QFormLayout(this);

    if (m_defaultExtractors.size())
    {
        layout->addRow(QSL("Filter template"), templateSelectLayout);
    }
    layout->addRow(m_filterEditor);
    layout->addRow(m_optionsLayout);
    layout->addRow(buttonBoxLayout);
}

void AddEditSourceWidget::applyTemplate(int index)
{
    if (0 <= index && index < m_defaultExtractors.size())
    {
        auto tmpl = m_defaultExtractors[index];
        m_filterEditor->setSubFilters(tmpl->getFilter().getSubFilters());
        QString name = m_module->getModuleMeta().typeName + QSL(".") + tmpl->objectName().section('.', 0, -1);
        le_name->setText(name);
        m_spinCompletionCount->setValue(tmpl->getRequiredCompletionCount());
    }
}

void AddEditSourceWidget::accept()
{
    qDebug() << __PRETTY_FUNCTION__;
    auto extractor = qobject_cast<Extractor *>(m_src);

    extractor->setObjectName(le_name->text());
    m_filterEditor->apply();
    extractor->getFilter().setSubFilters(m_filterEditor->m_subFilters);
    extractor->setRequiredCompletionCount(m_spinCompletionCount->value());

    if (m_srcPtr)
    {
        m_eventWidget->addSource(m_srcPtr, m_module, m_gbGenHistograms->isChecked(),
                                 le_unit->text(), spin_unitMin->value(), spin_unitMax->value());
    }
    else
    {
        m_eventWidget->sourceEdited(m_src);
    }

    m_eventWidget->uniqueWidgetCloses();
    QDialog::accept();
}

void AddEditSourceWidget::reject()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_eventWidget->uniqueWidgetCloses();
    QDialog::reject();
}

//
// AddEditOperatorWidget
//

/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * accepted it will call eventWidget->addOperator()! */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorPtr opPtr, s32 userLevel, EventWidget *eventWidget)
    : AddEditOperatorWidget(opPtr.get(), userLevel, eventWidget)
{
    m_opPtr = opPtr;
    setWindowTitle(QString("New  %1").arg(opPtr->getDisplayName()));

    // Creating a new operator. Override the setting of wasNameEdited by the
    // constructor below.
    m_opConfigWidget->wasNameEdited = false;
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepted it will call eventWidget->operatorEdited()! */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorInterface *op, s32 userLevel, EventWidget *eventWidget)
    : QDialog(eventWidget)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_eventWidget(eventWidget)
    , m_opConfigWidget(new OperatorConfigurationWidget(op, userLevel, this))
{
    setWindowTitle(QString("Edit %1").arg(m_op->getDisplayName()));
    add_widget_close_action(this);

    // We're editing an operator so we assume the name has been specified by the user.
    m_opConfigWidget->wasNameEdited = true;

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
            inputSelected(-1);
            m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);
        });

        connect(m_removeSlotButton, &QPushButton::clicked, this, [this] () {
            if (m_op->getNumberOfSlots() > 1)
            {
                m_op->removeLastSlot();
                repopulateSlotGrid();
                inputSelected(-1);
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
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditOperatorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditOperatorWidget::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    //layout->setContentsMargins(2, 2, 2, 2);

    s32 row = 0;
    // row, col, rowSpan, colSpan
    layout->addWidget(slotGroupBox, row++, 0);
    layout->addWidget(m_opConfigWidget, row++, 0, 1, 2);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);

    // The widget is complete, now populate the slot grid.
    repopulateSlotGrid();
}

void AddEditOperatorWidget::repopulateSlotGrid()
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
        m_selectButtons.push_back(selectButton);

        connect(selectButton, &QPushButton::toggled, this, [this, slot, slotIndex, userLevel](bool checked) {
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
                m_eventWidget->selectInputFor(slot, userLevel, [this, slot, slotIndex] () {
                    // The assumption is that the analysis has been paused by
                    // the EventWidget.
                    Q_ASSERT(!m_eventWidget->getContext()->isAnalysisRunning());

                    // Update the slots source operator and all dependents
                    do_beginRun_forward(slot->parentOperator);
                    this->inputSelected(slotIndex);
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

            AnalysisPauser pauser(m_eventWidget->getContext());
            // Clear the slot
            slot->disconnectPipe();
            // Update the current select button to reflect the change
            m_selectButtons[slotIndex]->setText(QSL("<select>"));
            // Disable ok button as there's now at least one unset input
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
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

    // Updates the slot select buttons in case we're editing a connected operator
    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        if (m_op->getSlot(slotIndex)->inputPipe)
            inputSelected(slotIndex);
    }
}

QString makeSlotSourceString(Slot *slot)
{
    Q_ASSERT(slot->inputPipe);
    Q_ASSERT(slot->inputPipe->source);

    QString result = slot->inputPipe->source->objectName();

    if (slot->paramIndex != Slot::NoParamIndex)
    {
        result = QString("%1[%2]").arg(result).arg(slot->paramIndex);
    }

    return result;
}

void AddEditOperatorWidget::inputSelected(s32 slotIndex)
{
    if (slotIndex >= 0)
    {
        Slot *slot = m_op->getSlot(slotIndex);
        Q_ASSERT(slot);
        qDebug() << __PRETTY_FUNCTION__ << slot;

        auto selectButton = m_selectButtons[slotIndex];
        QSignalBlocker b(selectButton);
        selectButton->setChecked(false);
        selectButton->setText(makeSlotSourceString(slot));
    }

    m_opConfigWidget->inputSelected(slotIndex);

    bool enableOkButton = true;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        if (!m_op->getSlot(slotIndex)->inputPipe)
        {
            enableOkButton = false;
            break;
        }
    }

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableOkButton);
    m_inputSelectActive = false;
}

void AddEditOperatorWidget::accept()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getContext());
    m_opConfigWidget->configureOperator();
    if (m_opPtr)
    {
        // add mode
        m_eventWidget->addOperator(m_opPtr, m_userLevel);
    }
    else
    {
        // edit mode
        m_eventWidget->operatorEdited(m_op);
    }
    //close();
    m_eventWidget->endSelectInput();
    m_eventWidget->uniqueWidgetCloses();
    QDialog::accept();
}

void AddEditOperatorWidget::reject()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getContext());

    if (m_opPtr)
    {
        // add mode
        // The operator will not be added to the analysis. This means any slots
        // connected by the user must be disconnected again to avoid having
        // stale connections in the source operators.
        for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = m_op->getSlot(slotIndex);
            slot->disconnectPipe();
        }
    }
    else
    {
        // edit mode
        // Restore previous slot connections.

        if (m_op->hasVariableNumberOfSlots())
        {
            // Restore the original number of inputs.

            while (m_op->removeLastSlot());

            while (m_op->getNumberOfSlots() < m_slotBackups.size())
            {
                m_op->addSlot();
            }
        }

        for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = m_op->getSlot(slotIndex);
            auto oldConnection = m_slotBackups[slotIndex];
            slot->connectPipe(oldConnection.inputPipe, oldConnection.paramIndex);
        }

        do_beginRun_forward(m_op);
    }
    //close();
    m_eventWidget->endSelectInput();
    m_eventWidget->uniqueWidgetCloses();
    QDialog::reject();
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
void AddEditOperatorWidget::resizeEvent(QResizeEvent *event)
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

void repopulate_arrayMap_tables(ArrayMap *arrayMap, const ArrayMappings &mappings, QTableWidget *tw_input, QTableWidget *tw_output)
{
    Q_ASSERT(arrayMap && tw_input && tw_output);

    //qDebug() << __PRETTY_FUNCTION__;
    //debug_dump(mappings);

    tw_input->clearContents();
    tw_input->setRowCount(0);

    tw_output->clearContents();
    tw_output->setRowCount(0);

    const s32 slotCount = arrayMap->getNumberOfSlots();

    auto make_table_item = [](const QString &name, s32 slotIndex, s32 paramIndex)
    {
        auto item = new QTableWidgetItem;

        item->setData(Qt::DisplayRole, QString("%1[%2]")
                      .arg(name)
                      .arg(paramIndex));

        item->setData(SlotIndexRole, slotIndex);
        item->setData(ParamIndexRole, paramIndex);

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

            auto item = make_table_item(slot->inputPipe->source->objectName(), slotIndex, paramIndex);

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

        auto item = make_table_item(slot->inputPipe->source->objectName(), mapping.slotIndex, mapping.paramIndex);

        tw_output->setRowCount(tw_output->rowCount() + 1);
        tw_output->setItem(tw_output->rowCount() - 1, 0, item);
    }

    tw_input->resizeRowsToContents();
    tw_output->resizeRowsToContents();
}

OperatorConfigurationWidget::OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, AddEditOperatorWidget *parent)
    : QWidget(parent)
    , m_parent(parent)
    , m_op(op)
    , m_userLevel(userLevel)
{
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);

    widgetLayout->addLayout(formLayout);

    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textEdited, this, [this](const QString &newText) {
        // If the user clears the textedit reset wasNameEdited to false.
        this->wasNameEdited = !newText.isEmpty();
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
                                       std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                       histoSink->m_xLimitMin, histoSink->m_xLimitMax, histoSink->hasActiveLimits(Qt::XAxis));

        // FIXME: input validation
        //connect(limits_x.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_y = make_axis_limits_ui(QSL("Y Limits"),
                                       std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                       histoSink->m_yLimitMin, histoSink->m_yLimitMax, histoSink->hasActiveLimits(Qt::YAxis));

        // FIXME: input validation
        //connect(limits_y.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_x.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        limits_y.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

        formLayout->addRow(limits_x.outerFrame);
        formLayout->addRow(limits_y.outerFrame);
    }
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        le_unit = new QLineEdit;
        le_unit->setText(calibration->getUnitLabel());

        spin_unitMin = new QDoubleSpinBox;
        spin_unitMin->setDecimals(8);
        spin_unitMin->setMinimum(-1e20);
        spin_unitMin->setMaximum(+1e20);
        spin_unitMin->setValue(spin_unitMin->minimum());
        spin_unitMin->setSpecialValueText(QSL("not set"));

        spin_unitMax = new QDoubleSpinBox;
        spin_unitMax->setDecimals(8);
        spin_unitMax->setMinimum(-1e20);
        spin_unitMax->setMaximum(+1e20);
        spin_unitMax->setValue(spin_unitMax->minimum());
        spin_unitMax->setSpecialValueText(QSL("not set"));

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Unit Min"), spin_unitMin);
        formLayout->addRow(QSL("Unit Max"), spin_unitMax);

        // Find first valid min/max pair and use it to fill the spinboxes.
        for (const auto &params: calibration->getCalibrations())
        {
            if (params.isValid())
            {
                qDebug() << __PRETTY_FUNCTION__ << calibration->objectName()
                    << "setting spinbox values:" << params.unitMin << params.unitMax;
                spin_unitMin->setValue(params.unitMin);
                spin_unitMax->setValue(params.unitMax);
                break;
            }
        }

        m_calibrationTable = new QTableWidget;
        m_calibrationTable->setMinimumSize(325, 175);

        m_pb_applyGlobalCalib = new QPushButton(QIcon(":/arrow_down.png"), QSL("Apply"));
        m_pb_applyGlobalCalib->setToolTip(QSL("Apply to all addresses"));

        connect(m_pb_applyGlobalCalib, &QPushButton::clicked, this, [this] () {
            double unitMin = spin_unitMin->value();
            double unitMax = spin_unitMax->value();

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

        m_applyGlobalCalibFrame = new QFrame;
        auto applyCalibLayout = new QHBoxLayout(m_applyGlobalCalibFrame);
        applyCalibLayout->setContentsMargins(0, 0, 0, 0);
        applyCalibLayout->addStretch(1);
        applyCalibLayout->addWidget(m_pb_applyGlobalCalib);
        applyCalibLayout->addStretch(1);

        formLayout->addRow(m_applyGlobalCalibFrame);
        m_applyGlobalCalibFrame->setVisible(false);

        m_calibrationTable->setVisible(false);
        m_calibrationTable->setColumnCount(3);
        m_calibrationTable->setItemDelegateForColumn(1, new CalibrationItemDelegate(m_calibrationTable));
        m_calibrationTable->setItemDelegateForColumn(2, new CalibrationItemDelegate(m_calibrationTable));
        m_calibrationTable->setHorizontalHeaderLabels({"Address", "Min", "Max"});
        m_calibrationTable->verticalHeader()->setVisible(false);

        // Populates the calibration table
        inputSelected(0);

        widgetLayout->addWidget(m_calibrationTable);
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
}

#if 0
// FIXME: right now this is not actually called by AddEditOperatorWidget...
bool OperatorConfigurationWidget::validateInputs()
{
    qDebug() << __PRETTY_FUNCTION__;
    OperatorInterface *op = m_op;

    if (le_name->text().isEmpty())
        return false;

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        return true;
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        bool result = true;
        if (limits_x.rb_limited->isChecked())
        {
            result = result && (limits_x.spin_min->value() != limits_x.spin_max->value());
        }

        if (limits_y.rb_limited->isChecked())
        {
            result = result && (limits_y.spin_min->value() != limits_y.spin_max->value());
        }

        return result;
    }
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        double unitMin = spin_unitMin->value();
        double unitMax = spin_unitMax->value();
        return (unitMin != unitMax);
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        return true;
    }

    return false;
}
#endif

// NOTE: This will be called after construction for each slot by AddEditOperatorWidget::repopulateSlotGrid()!
void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;

    if (no_input_connected(op) && !wasNameEdited)
    {
        le_name->clear();
        wasNameEdited = false;
    }

    //
    // Operator name
    //
    if (!wasNameEdited)
    {
        // The name field is empty or was never modified by the user. Update its
        // contents to reflect the newly selected input(s).

        if (op->getNumberOfSlots() == 1 && op->getSlot(0)->isConnected())
        {
            le_name->setText(makeSlotSourceString(op->getSlot(0)));
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
        {
            if (histoSink->m_inputX.isConnected() && histoSink->m_inputY.isConnected())
            {
                QString nameX = makeSlotSourceString(&histoSink->m_inputX);
                QString nameY = makeSlotSourceString(&histoSink->m_inputY);
                le_name->setText(QString("%1_vs_%2").arg(nameX).arg(nameY));
            }
        }
        else if (auto difference = qobject_cast<Difference *>(op))
        {
            if (difference->m_inputA.isConnected() && difference->m_inputB.isConnected())
            {
                QString nameA = makeSlotSourceString(&difference->m_inputA);
                QString nameB = makeSlotSourceString(&difference->m_inputB);
                le_name->setText(QString("%1 - %2").arg(nameA).arg(nameB));
            }
        }
        else if (auto condFilter = qobject_cast<ConditionFilter *> (op))
        {
            if (condFilter->m_dataInput.isConnected() && condFilter->m_conditionInput.isConnected())
            {
                QString dataName = makeSlotSourceString(&condFilter->m_dataInput);
                QString condName = makeSlotSourceString(&condFilter->m_conditionInput);
                le_name->setText(QString("%1_if_%2").arg(dataName).arg(condName));
            }
        }
        else if (auto filter = qobject_cast<RectFilter2D *>(op))
        {
            if (filter->getSlot(0)->isConnected() && filter->getSlot(1)->isConnected())
            {
                QString nameX = makeSlotSourceString(filter->getSlot(0));
                QString nameY = makeSlotSourceString(filter->getSlot(1));
                le_name->setText(QString("%1_%2").arg(nameX).arg(nameY)); // FIXME: better name here
            }
        }
    }
    else
    {
        le_name->setText(op->objectName());
    }

    if (!le_name->text().isEmpty() && op->getNumberOfOutputs() > 0 && all_inputs_connected(op) && !wasNameEdited)
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

    //
    // Operator specific actions
    //

    Slot *slot = (slotIndex >= 0 ? op->getSlot(slotIndex) : nullptr);

    if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        Q_ASSERT(slot);

        if (slot->isConnected())
        {
            // If connected to array:
            //   use first calibration values for spinbox
            //   if those are not valid use first input param values for spinbox
            // if connected to param:
            //   use calibration for param for spinbox
            //   if that is not valid use input param values for spinbox
            // if nothing is valid keep spinbox values

            double proposedMin = spin_unitMin->value();
            double proposedMax = spin_unitMax->value();

            s32 paramIndex = (slot->paramIndex == Slot::NoParamIndex ? 0 : slot->paramIndex);
            auto params = calibration->getCalibration(paramIndex);

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

            spin_unitMin->setValue(proposedMin);
            spin_unitMax->setValue(proposedMax);

            if (slot->paramIndex == Slot::NoParamIndex)
            {
                fillCalibrationTable(calibration, proposedMin, proposedMax);
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
            spin_unitMin->setValue(spin_unitMin->minimum());
            spin_unitMax->setValue(spin_unitMax->minimum());
            m_calibrationTable->setVisible(false);
            m_applyGlobalCalibFrame->setVisible(false);
        }
    }
    else if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty())
        {
            le_xAxisTitle->setText(le_name->text());
        }
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty() && histoSink->m_inputX.isConnected())
            le_xAxisTitle->setText(makeSlotSourceString(&histoSink->m_inputX));

        if (le_yAxisTitle->text().isEmpty() && histoSink->m_inputY.isConnected())
            le_yAxisTitle->setText(makeSlotSourceString(&histoSink->m_inputY));

        // x input was selected
        if (histoSink->m_inputX.isConnected() && slot == &histoSink->m_inputX && std::isnan(histoSink->m_xLimitMin))
        {
            limits_x.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_x.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }

        // y input was selected
        if (histoSink->m_inputY.isConnected() && slot == &histoSink->m_inputY && std::isnan(histoSink->m_yLimitMin))
        {
            limits_y.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_y.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
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
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        Q_ASSERT(slot);

        if (slot == op->getSlot(0)) // x
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
        else if (slot == op->getSlot(1)) // y
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

void OperatorConfigurationWidget::configureOperator()
{
    OperatorInterface *op = m_op;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        Q_ASSERT(op->getSlot(slotIndex)->isConnected());
    }

    op->setObjectName(le_name->text());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        histoSink->m_xAxisTitle = le_xAxisTitle->text();

        s32 bins = combo_xBins->currentData().toInt();
        histoSink->m_bins = bins;
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
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        calibration->setUnitLabel(le_unit->text());

        double unitMin = spin_unitMin->value();
        double unitMax = spin_unitMax->value();

        if (unitMin == spin_unitMin->minimum())
            unitMin = make_quiet_nan();

        if (unitMax == spin_unitMax->minimum())
            unitMax = make_quiet_nan();


        for (s32 addr = 0; addr < op->getSlot(0)->inputPipe->parameters.size(); ++addr)
        {
            if (op->getSlot(0)->paramIndex != Slot::NoParamIndex)
            {
                // Connected to specific address -> use spinbox values for that address
                if (op->getSlot(0)->paramIndex == addr)
                {
                    calibration->setCalibration(addr, unitMin, unitMax);
                }
                else
                {
                    // Set invalid params for other addresses
                    calibration->setCalibration(addr, CalibrationMinMaxParameters());
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

                calibration->setCalibration(addr, unitMin, unitMax);
            }
        }
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
}

void OperatorConfigurationWidget::fillCalibrationTable(CalibrationMinMax *calib, double proposedMin, double proposedMax)
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
    }

    m_calibrationTable->resizeRowsToContents();
}

//
// PipeDisplay
//

PipeDisplay::PipeDisplay(Pipe *pipe, QWidget *parent)
    : QWidget(parent, Qt::Tool)
    , m_pipe(pipe)
    , m_parameterTable(new QTableWidget)
{
    auto layout = new QGridLayout(this);
    s32 row = 0;
    s32 nCols = 1;

    auto refreshButton = new QPushButton(QSL("Refresh"));
    connect(refreshButton, &QPushButton::clicked, this, &PipeDisplay::refresh);

    auto closeButton = new QPushButton(QSL("Close"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    layout->addWidget(refreshButton, row++, 0);
    layout->addWidget(m_parameterTable, row++, 0);
    layout->addWidget(closeButton, row++, 0, 1, 1);

    layout->setRowStretch(1, 1);

    refresh();
}

void PipeDisplay::refresh()
{
    m_parameterTable->clear();
    m_parameterTable->setColumnCount(4);
    m_parameterTable->setRowCount(m_pipe->parameters.size());

    // columns:
    // Valid, Value, lower Limit, upper Limit
    m_parameterTable->setHorizontalHeaderLabels({"Valid", "Value", "Lower Limit", "Upper Limit"});

    for (s32 paramIndex = 0; paramIndex < m_pipe->parameters.size(); ++paramIndex)
    {
        const auto &param(m_pipe->parameters[paramIndex]);
        auto item = new QTableWidgetItem(param.valid ? "Y" : "N");
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_parameterTable->setItem(paramIndex, 0, item);

        item = new QTableWidgetItem(param.valid ? QString::number(param.value) : "");
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_parameterTable->setItem(paramIndex, 1, item);

        item = new QTableWidgetItem(QString::number(param.lowerLimit));
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_parameterTable->setItem(paramIndex, 2, item);

        item = new QTableWidgetItem(QString::number(param.upperLimit));
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_parameterTable->setItem(paramIndex, 3, item);

        m_parameterTable->setVerticalHeaderItem(paramIndex, new QTableWidgetItem(QString::number(paramIndex)));
    }

    m_parameterTable->resizeColumnsToContents();
    m_parameterTable->resizeRowsToContents();

    setWindowTitle(m_pipe->parameters.name);
}


QWidget* CalibrationItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    auto result = QStyledItemDelegate::createEditor(parent, option, index);

    if (auto spin = qobject_cast<QDoubleSpinBox *>(result))
    {
        spin->setDecimals(8);
        spin->setMinimum(-1e20);
        spin->setMaximum(+1e20);
        spin->setSpecialValueText(QSL("no set"));
        spin->setValue(spin->minimum());
    }

    return result;
}

}
