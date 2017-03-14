#include "analysis_ui_p.h"
#include "data_extraction_widget.h"
#include "../globals.h"
#include "../mvme_config.h"
#include "../mvme_context.h"

#include <limits>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
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

static const s32 Histo1DMinBits = 1;
static const s32 Histo1DMaxBits = 20;
static const s32 Histo1DDefBits = 16;

static const s32 Histo2DMinBits = 1;
static const s32 Histo2DMaxBits = 13;
static const s32 Histo2DDefBits = 10;

// Assumes that selectedRes is a power of 2!
void select_by_resolution(QComboBox *combo, s32 selectedRes)
{
    s32 minBits = 0;
    s32 selectedBits = std::log2(selectedRes);

    if (selectedBits > 0)
    {
        s32 index = selectedBits - minBits - 1;
        index = std::min(index, combo->count() - 1);
        combo->setCurrentIndex(index);
    }
}

QComboBox *make_resolution_combo(s32 minBits, s32 maxBits, s32 selectedBits)
{
    QComboBox *result = new QComboBox;

    for (s32 bits = minBits;
         bits <= maxBits;
         ++bits)
    {
        s32 value = 1 << bits;

        QString text = QString("%1, %2 bit").arg(value, 4).arg(bits, 2);

        result->addItem(text, value);
    }

    select_by_resolution(result, 1 << selectedBits);

    return result;
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

        if (histoSink->m_histo)
        {
            select_by_resolution(combo_xBins, histoSink->m_histo->getAxisBinning(Qt::XAxis).getBins());
            select_by_resolution(combo_yBins, histoSink->m_histo->getAxisBinning(Qt::YAxis).getBins());
        }

        formLayout->addRow(QSL("X Resolution"), combo_xBins);
        formLayout->addRow(QSL("Y Resolution"), combo_yBins);
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
        spin_unitMax->setValue(0.0);

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Unit Min"), spin_unitMin);
        formLayout->addRow(QSL("Unit Max"), spin_unitMax);

        le_unit->setText(calibration->getUnitLabel());
        auto globalParams = calibration->getGlobalCalibration();
        if (globalParams.isValid())
        {
            spin_unitMin->setValue(globalParams.unitMin);
            spin_unitMax->setValue(globalParams.unitMax);
        }

        m_calibrationTable = new QTableWidget;
        m_calibrationTable->setVisible(false);

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
}

bool OperatorConfigurationWidget::validateInputs()
{
    OperatorInterface *op = m_op;

    if (le_name->text().isEmpty())
        return false;

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        return true;
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        return true;
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

        if (histoSink->m_histo)
        {
            qDebug() << "histo2d resize" << xBins << yBins;
            histoSink->m_histo->resize(xBins, yBins);
        }
        else
        {
            // Note: these are "fake" values. Histo2DSink::beginRun() will look
            // at the input parameters and update the axis limits accordingly
            double xMin = 0;
            double xMax = 1 << xBins;

            double yMin = 0;
            double yMax = 1 << yBins;

            qDebug() << "new histo2d" << xBins << yBins;

            histoSink->m_histo = std::make_shared<Histo2D>(xBins, xMin, xMax,
                                                           yBins, yMin, yMax);
            histoSink->m_histo->setObjectName(op->objectName());
        }
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

        calibration->setGlobalCalibration(unitMin, unitMax);
        calibration->setUnitLabel(le_unit->text());

        for (s32 addr = 0; addr < op->getSlot(0)->inputPipe->parameters.size(); ++addr)
        {
            unitMin = unitMin = make_quiet_nan();

            if (auto item = m_calibrationTable->item(addr, 0))
                unitMin = item->data(Qt::EditRole).toDouble();

            if (auto item = m_calibrationTable->item(addr, 1))
                unitMax = item->data(Qt::EditRole).toDouble();

            calibration->setCalibration(addr, unitMin, unitMax);
        }
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        s32 index = spin_index->value();
        selector->setIndex(index);
    }
}

void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;
    Slot *slot = op->getSlot(slotIndex);

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

    if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        if (calibration->getSlot(0)->isConnected())
        {
            if (!calibration->getGlobalCalibration().isValid())
            {
                Parameter *firstParam = calibration->getSlot(0)->inputPipe->first();
                if (firstParam)
                {
                    calibration->setGlobalCalibration(firstParam->lowerLimit, firstParam->upperLimit);
                    spin_unitMin->setValue(firstParam->lowerLimit);
                    spin_unitMax->setValue(firstParam->upperLimit);
                }
            }

            fillCalibrationTable(calibration);
        }
        else
        {
            m_calibrationTable->setVisible(false);
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
    }
}

void OperatorConfigurationWidget::fillCalibrationTable(CalibrationMinMax *calib)
{
    Q_ASSERT(calib->getSlot(0)->isConnected());


    s32 paramCount = calib->getSlot(0)->inputPipe->parameters.size();

    m_calibrationTable->clear();
    m_calibrationTable->setColumnCount(2);
    m_calibrationTable->setHorizontalHeaderLabels({"Min", "Max"});
    m_calibrationTable->setCornerButtonEnabled(true);
    m_calibrationTable->setRowCount(paramCount);

    for (s32 addr = 0; addr < paramCount; ++addr)
    {
        auto calibParams = calib->getCalibration(addr);
        auto item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, calibParams.unitMin);
        //item->setText(QString::number(calibParams.unitMin));
        m_calibrationTable->setItem(addr, 0, item);

        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, calibParams.unitMax);
        item->setText(QString::number(calibParams.unitMax));
        m_calibrationTable->setItem(addr, 1, item);

        m_calibrationTable->setVerticalHeaderItem(addr, new QTableWidgetItem(QString::number(addr)));
    }

    m_calibrationTable->setVisible(true);
    //m_calibrationTable->resizeColumnsToContents();
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

}
