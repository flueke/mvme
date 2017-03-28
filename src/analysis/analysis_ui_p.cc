#include "analysis_ui_p.h"
#include "data_extraction_widget.h"
#include "../globals.h"
#include "../histo_util.h"
#include "../mvme_config.h"
#include "../mvme_context.h"

#include <limits>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>

namespace analysis
{
//
// AddEditSourceWidget
//
QByteArray getDefaultFilter(VMEModuleType moduleType)
{
    // defined in globals.h
    return defaultDataFilters.value(moduleType).value(0).filter;
}

const char *getDefaultFilterName(VMEModuleType moduleType)
{
    // defined in globals.h
    return defaultDataFilters.value(moduleType).value(0).name;
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
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepting the widget inputs it will call eventWidget->sourceEdited(). */
AddEditSourceWidget::AddEditSourceWidget(SourceInterface *src, ModuleConfig *module, EventWidget *eventWidget)
    : QWidget(eventWidget, Qt::Tool)
    , m_src(src)
    , m_module(module)
    , m_eventWidget(eventWidget)
    , m_gbGenHistograms(nullptr)
{
    setWindowTitle(QString("Edit %1").arg(m_src->getDisplayName()));

    auto extractor = qobject_cast<Extractor *>(src);
    Q_ASSERT(extractor);
    Q_ASSERT(module);

    le_name = new QLineEdit;
    m_filterEditor = new DataExtractionEditor;
    m_filterEditor->setMinimumHeight(125);
    m_filterEditor->setMinimumWidth(550);

    m_spinCompletionCount = new QSpinBox;
    m_spinCompletionCount->setMinimum(1);
    m_spinCompletionCount->setMaximum(std::numeric_limits<int>::max());

    if (extractor)
    {
        if (!extractor->objectName().isEmpty())
        {
            le_name->setText(QString("%1").arg(extractor->objectName()));
        }
        else
        {
            le_name->setText(QString("%1.%2").arg(module->objectName()).arg(getDefaultFilterName(module->type)));
        }

        m_filterEditor->m_defaultFilter = getDefaultFilter(module->type);
        m_filterEditor->m_subFilters = extractor->getFilter().getSubFilters();
        if (m_filterEditor->m_subFilters.isEmpty())
        {
            m_filterEditor->m_subFilters.push_back(DataFilter(m_filterEditor->m_defaultFilter));
        }
        m_filterEditor->updateDisplay();
        m_spinCompletionCount->setValue(extractor->getRequiredCompletionCount());
    }

    m_optionsLayout = new QFormLayout;
    m_optionsLayout->addRow(QSL("Name"), le_name);
    m_optionsLayout->addRow(QSL("Required Completion Count"), m_spinCompletionCount);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditSourceWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QVBoxLayout(this);

    layout->addWidget(m_filterEditor);
    layout->addLayout(m_optionsLayout);
    layout->addLayout(buttonBoxLayout);
    layout->setStretch(1, 1);
}

void AddEditSourceWidget::accept()
{
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
    close();
}

void AddEditSourceWidget::closeEvent(QCloseEvent *event)
{
    m_eventWidget->uniqueWidgetCloses();
    event->accept();
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
    : QWidget(eventWidget, Qt::Tool)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_eventWidget(eventWidget)
    , m_opConfigWidget(new OperatorConfigurationWidget(op, userLevel, this))
{
    setWindowTitle(QString("Edit %1").arg(m_op->getDisplayName()));

    // We're editing an operator so we assume the name has been specified by the user.
    m_opConfigWidget->wasNameEdited = true;

    s32 slotCount = m_op->getNumberOfSlots();
    auto slotGroupBox = new QGroupBox(slotCount > 1 ? QSL("Inputs") : QSL("Input"));
    auto slotGrid = new QGridLayout(slotGroupBox);
    slotGrid->setContentsMargins(2, 2, 2, 2);
    s32 row = 0;
    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        Slot *slot = m_op->getSlot(slotIndex);

        // Record the current slot input
        m_slotBackups.push_back({slot->inputPipe, slot->paramIndex});

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
                m_eventWidget->selectInputFor(slot, userLevel, [this, slotIndex] () {
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
        if (slotCount > 1)
        {
            slotGrid->addWidget(new QLabel(slot->name), row, col++);
        }

        slotGrid->addWidget(selectButton, row, col++);
        slotGrid->addWidget(clearButton, row, col++);
        ++row;
    }

    if (slotCount == 1)
    {
        slotGrid->setColumnStretch(0, 1);
        slotGrid->setColumnStretch(1, 0);
    }

    if (slotCount > 1)
    {
        slotGrid->setColumnStretch(0, 0);
        slotGrid->setColumnStretch(1, 1);
        slotGrid->setColumnStretch(2, 0);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditOperatorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditOperatorWidget::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    //layout->setContentsMargins(2, 2, 2, 2);

    int col = 0, maxCol = 1;
    row = 0;
    // row, col, rowSpan, colSpan
    layout->addWidget(slotGroupBox, row++, 0);
    layout->addWidget(m_opConfigWidget, row++, 0, 1, 2);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);


    // Updates the slot select buttons in case we're editing a connected operator
    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
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
    Slot *slot = m_op->getSlot(slotIndex);
    Q_ASSERT(slot);
    qDebug() << __PRETTY_FUNCTION__ << slot;

    auto selectButton = m_selectButtons[slotIndex];
    QSignalBlocker b(selectButton);
    selectButton->setChecked(false);
    selectButton->setText(makeSlotSourceString(slot));

    bool enableOkButton = true;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        if (!m_op->getSlot(slotIndex)->inputPipe)
        {
            enableOkButton = false;
            break;
        }
    }

    m_opConfigWidget->inputSelected(slotIndex);

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableOkButton);
    m_inputSelectActive = false;
}

void AddEditOperatorWidget::accept()
{
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
    close();
}

void AddEditOperatorWidget::reject()
{
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
        for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = m_op->getSlot(slotIndex);
            auto oldConnection = m_slotBackups[slotIndex];
            slot->connectPipe(oldConnection.inputPipe, oldConnection.paramIndex);
        }
    }
    close();
}

void AddEditOperatorWidget::closeEvent(QCloseEvent *event)
{
    m_eventWidget->endSelectInput();
    m_eventWidget->uniqueWidgetCloses();
    event->accept();
}

//
// OperatorConfigurationWidget
//

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

        limits_x = make_histo2d_axis_limits_ui(QSL("X Limits"),
                                               std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                               histoSink->m_xLimitMin, histoSink->m_xLimitMax);

        connect(limits_x.groupBox, &QGroupBox::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_y = make_histo2d_axis_limits_ui(QSL("Y Limits"),
                                               std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                               histoSink->m_yLimitMin, histoSink->m_yLimitMax);

        connect(limits_y.groupBox, &QGroupBox::toggled, this, [this] (bool) { this->validateInputs(); });

        formLayout->addRow(limits_x.groupBox);
        formLayout->addRow(limits_y.groupBox);
    }
#if 0
    else if (auto calibration = qobject_cast<CalibrationFactorOffset *>(op))
    {
        // Note: CalibrationFactorOffset is not used anymore. This code hasn't been updated for a while.
        le_unit = new QLineEdit;
        spin_factor = new QDoubleSpinBox;
        spin_factor->setDecimals(8);
        spin_factor->setMinimum(-1e20);
        spin_factor->setMaximum(+1e20);
        spin_factor->setValue(1.0);

        spin_offset = new QDoubleSpinBox;
        spin_offset->setDecimals(8);
        spin_offset->setMinimum(-1e20);
        spin_offset->setMaximum(+1e20);
        spin_offset->setValue(0.0);

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Factor"), spin_factor);
        formLayout->addRow(QSL("Offset"), spin_offset);

        le_unit->setText(calibration->getUnitLabel());
        auto globalParams = calibration->getGlobalCalibration();
        if (globalParams.isValid())
        {
            spin_factor->setValue(globalParams.factor);
            spin_offset->setValue(globalParams.offset);
        }
    }
#endif
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        parent->resize(350, 450);
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
        spin_unitMax->setValue(spin_unitMin->minimum());
        spin_unitMax->setSpecialValueText(QSL("not set"));

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Unit Min"), spin_unitMin);
        formLayout->addRow(QSL("Unit Max"), spin_unitMax);


        auto globalParams = calibration->getGlobalCalibration();
        if (globalParams.isValid())
        {
            spin_unitMin->setValue(globalParams.unitMin);
            spin_unitMax->setValue(globalParams.unitMax);
        }

        m_calibrationTable = new QTableWidget;

        m_pb_applyGlobalCalib = new QPushButton(QIcon(":/arrow_down.png"), QSL("Apply"));
        m_pb_applyGlobalCalib->setToolTip(QSL("Apply to all addresses"));

        connect(m_pb_applyGlobalCalib, &QPushButton::clicked, this, [this] () {
            double unitMin = spin_unitMin->value();
            double unitMax = spin_unitMax->value();

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
}

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
        if (limits_x.groupBox->isChecked())
        {
            result = result && (limits_x.spin_min->value() != limits_x.spin_max->value());
        }

        if (limits_y.groupBox->isChecked())
        {
            result = result && (limits_y.spin_min->value() != limits_y.spin_max->value());
        }

        return result;
    }
#if 0
    else if (auto calibration = qobject_cast<CalibrationFactorOffset *>(op))
    {
        return spin_factor->value() != 0.0;
    }
#endif
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

        if (limits_x.groupBox->isChecked())
        {
            histoSink->m_xLimitMin = limits_x.spin_min->value();
            histoSink->m_xLimitMax = limits_x.spin_max->value();
        }
        else
        {
            histoSink->m_xLimitMin = make_quiet_nan();
            histoSink->m_xLimitMax = make_quiet_nan();
        }

        if (limits_y.groupBox->isChecked())
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
#if 0
    else if (auto calibration = qobject_cast<CalibrationFactorOffset *>(op))
    {
        double factor = spin_factor->value();
        double offset = spin_offset->value();

        calibration->setGlobalCalibration(factor, offset);
    }
#endif
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        double unitMin = spin_unitMin->value();
        double unitMax = spin_unitMax->value();

        if (unitMin == spin_unitMin->minimum())
            unitMin = make_quiet_nan();

        if (unitMax == spin_unitMax->minimum())
            unitMax = make_quiet_nan();

        calibration->setGlobalCalibration(unitMin, unitMax);
        calibration->setUnitLabel(le_unit->text());

        for (s32 addr = 0; addr < op->getSlot(0)->inputPipe->parameters.size(); ++addr)
        {
            unitMin = unitMax = make_quiet_nan();

            if (auto item = m_calibrationTable->item(addr, 1))
                unitMin = item->data(Qt::EditRole).toDouble();

            if (auto item = m_calibrationTable->item(addr, 2))
                unitMax = item->data(Qt::EditRole).toDouble();

            calibration->setCalibration(addr, unitMin, unitMax);
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
}

void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;
    Slot *slot = op->getSlot(slotIndex);

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
            le_name->setText(makeSlotSourceString(slot));
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
    }
    else
    {
        le_name->setText(op->objectName());
    }

    if (!le_name->text().isEmpty() && op->getNumberOfOutputs() > 0 && all_inputs_connected(op) && !wasNameEdited)
    {
        // Append the lowercase short name for non sinks
        auto name = le_name->text();
        name = name + "." + op->getShortName().toLower();
        le_name->setText(name);
    }

    //
    // Operator specific actions
    //

    if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        if (slot->isConnected())
        {
            if (!calibration->getGlobalCalibration().isValid())
            {
                Parameter *firstParam = slot->inputPipe->first();
                if (slot->paramIndex != Slot::NoParamIndex)
                {
                    firstParam = slot->inputPipe->getParameter(slot->paramIndex);
                }

                if (firstParam)
                {
                    spin_unitMin->setValue(firstParam->lowerLimit);
                    spin_unitMax->setValue(firstParam->upperLimit);
                }
            }

            double proposedMin = spin_unitMin->value();
            double proposedMax = spin_unitMax->value();

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
        qDebug() << __PRETTY_FUNCTION__ << "calib params for" << addr << ": valid =" << calibParams.isValid();
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
