#include "analysis_ui_p.h"
#include "data_extraction_widget.h"
#include "../globals.h"
#include "../mvme_config.h"

#include <limits>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>

namespace analysis
{


/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * closing it will call eventWidget->addOperator(). */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorPtr opPtr, s32 userLevel, EventWidget *eventWidget)
    : AddEditOperatorWidget(opPtr.get(), userLevel, eventWidget)
{
    m_opPtr = opPtr;
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * closing it will call FIXME */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorInterface *op, s32 userLevel, EventWidget *eventWidget)
    : QWidget(eventWidget, Qt::Tool)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_eventWidget(eventWidget)
    , m_opConfigWidget(new OperatorConfigurationWidget(op, userLevel, this))
{
    // TODO: actually do implement edit support for AddEditOperatorWidget

    //m_opConfigWidget->setEnabled(false);

    auto slotGrid = new QGridLayout;
    int row = 0;
    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
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

            // Clear the slot
            slot->disconnectPipe();
            // Update the current select button to reflect the change
            m_selectButtons[slotIndex]->setText(QSL("<select>"));
            // Disable ok button as there's now at least one unset input
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            //m_opConfigWidget->setEnabled(false);
            m_opConfigWidget->inputSelected(slotIndex);
        });

        slotGrid->addWidget(new QLabel(slot->name), row, 0);
        slotGrid->addWidget(selectButton, row, 1);
        slotGrid->addWidget(clearButton, row, 2);
        ++row;
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditOperatorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    int col = 0, maxCol = 1;
    row = 0;
    // row, col, rowSpan, colSpan
    layout->addLayout(slotGrid, row++, 0);
    layout->addWidget(m_opConfigWidget, row++, 0, 1, 2);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(0, 1);
    layout->setRowStretch(1, 1);
}

void AddEditOperatorWidget::inputSelected(s32 slotIndex)
{
    Slot *slot = m_op->getSlot(slotIndex);
    qDebug() << __PRETTY_FUNCTION__ << slot;

    auto selectButton = m_selectButtons[slotIndex];
    QSignalBlocker b(selectButton);
    selectButton->setChecked(false);

    QString buttonText = slot->inputPipe->source->objectName();
    if (slot->paramIndex != Slot::NoParamIndex)
    {
        buttonText = QString("%1[%2]").arg(buttonText).arg(slot->paramIndex);
    }
    else
    {
        buttonText = QString("%1 (size=%2)").arg(buttonText).arg(slot->inputPipe->getParameters().size());
    }
    selectButton->setText(buttonText);

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
    //m_opConfigWidget->setEnabled(enableOkButton);
    m_inputSelectActive = false;
}

void AddEditOperatorWidget::accept()
{
    m_opConfigWidget->configureOperator();
    if (m_opPtr)
    {
        m_eventWidget->addOperator(m_opPtr, m_userLevel);
    }
    else
    {
        m_eventWidget->operatorEdited(m_op);
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
// AddEditSourceWidget
//
QByteArray getDefaultFilter(VMEModuleType moduleType)
{
    // defined in globals.h
    return defaultDataFilters.value(moduleType).value(0).filter;
}

/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * accepting the widget inputs it will call eventWidget->addSource(). */
AddEditSourceWidget::AddEditSourceWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget)
    : AddEditSourceWidget(srcPtr.get(), mod, eventWidget)
{
    m_srcPtr = srcPtr;
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepting the widget inputs it will call eventWidget->sourceEdited(). */
AddEditSourceWidget::AddEditSourceWidget(SourceInterface *src, ModuleConfig *module, EventWidget *eventWidget)
    : QWidget(eventWidget, Qt::Tool)
    , m_src(src)
    , m_module(module)
    , m_eventWidget(eventWidget)
{
    auto extractor = qobject_cast<Extractor *>(src);
    Q_ASSERT(extractor); // TODO: implement support for other sources once they exist
    Q_ASSERT(module);

    le_name = new QLineEdit;
    m_filterEditor = new DataExtractionEditor;
    m_filterEditor->setMinimumHeight(125);

    if (extractor)
    {
        le_name->setText(extractor->objectName());

        m_filterEditor->m_defaultFilter = getDefaultFilter(module->type);
        m_filterEditor->m_subFilters = extractor->getFilter().getSubFilters();
        if (m_filterEditor->m_subFilters.isEmpty())
        {
            m_filterEditor->m_subFilters.push_back(DataFilter(m_filterEditor->m_defaultFilter));
        }
        m_filterEditor->m_requiredCompletionCount = extractor->getRequiredCompletionCount();

        m_filterEditor->updateDisplay();
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    //m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditSourceWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    s32 row = 0, col = 0, maxCol = 2;
    // row, col, rowSpan, colSpan
    layout->addWidget(new QLabel(QSL("Name")), row, 0);
    layout->addWidget(le_name, row, 1);
    ++row;

    layout->addWidget(m_filterEditor, row++, 0, 1, maxCol);
    layout->addLayout(buttonBoxLayout, row++, 0, 1, maxCol);

    layout->setRowStretch(1, 1);
}

void AddEditSourceWidget::accept()
{
    auto extractor = qobject_cast<Extractor *>(m_src);

    extractor->setObjectName(le_name->text());
    m_filterEditor->apply();
    extractor->getFilter().setSubFilters(m_filterEditor->m_subFilters);
    extractor->setRequiredCompletionCount(m_filterEditor->m_requiredCompletionCount);

    if (m_srcPtr)
    {
        m_eventWidget->addSource(m_srcPtr, m_module);
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
// OperatorConfigurationWidget
//

static const s32 bin1DMin = 1;
static const s32 bin1DMax = 1 << 20;
static const s32 bin1DDef = 1 << 16;

static const s32 bin2DMin = 1;
static const s32 bin2DMax = 1 << 12;
static const s32 bin2DDef = 1 << 10;

OperatorConfigurationWidget::OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, AddEditOperatorWidget *parent)
    : QWidget(parent)
    , m_parent(parent)
    , m_op(op)
    , m_userLevel(userLevel)
{
    auto *formLayout = new QFormLayout(this);

    le_name = new QLineEdit;
    formLayout->addRow(QSL("Name"), le_name);

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        spin_xBins = new QSpinBox;
        spin_xBins->setMinimum(bin1DMin);
        spin_xBins->setMaximum(bin1DMax);
        spin_xBins->setValue(bin1DDef);

        spin_xMin = new QDoubleSpinBox;

        formLayout->addRow(QSL("Bins"), spin_xBins);
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        spin_xBins = new QSpinBox;
        spin_yBins = new QSpinBox;

        for (auto spin: {spin_xBins, spin_yBins})
        {
            spin->setMinimum(bin2DMin);
            spin->setMaximum(bin2DMax);
            spin->setValue(bin2DDef);
        }

        formLayout->addRow(QSL("X Bins"), spin_xBins);
        formLayout->addRow(QSL("Y Bins"), spin_yBins);
    }
    else if (auto calibration = qobject_cast<Calibration *>(op))
    {
        le_unit = new QLineEdit;
        spin_factor = new QDoubleSpinBox;
        spin_factor->setDecimals(8);
        spin_factor->setMinimum(1e-20);
        spin_factor->setMaximum(1e+20);
        spin_factor->setValue(1.0);

        spin_offset = new QDoubleSpinBox;
        spin_offset->setDecimals(8);
        spin_offset->setMinimum(1e-20);
        spin_offset->setMaximum(1e+20);
        spin_offset->setValue(0.0);

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Factor"), spin_factor);
        formLayout->addRow(QSL("Offset"), spin_offset);
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        spin_index = new QSpinBox;
        spin_index->setMinimum(0);
        spin_index->setMaximum(std::numeric_limits<s32>::max());
        formLayout->addRow(QSL("Selected Index"), spin_index);
    }
}

bool OperatorConfigurationWidget::validateInputs()
{
    OperatorInterface *op = m_op;

    if (le_name->text().isEmpty())
        return false;

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        return spin_xBins->value() > 0;
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        return spin_xBins->value() > 0 && spin_yBins->value() > 0;
    }
    else if (auto calibration = qobject_cast<Calibration *>(op))
    {
        return spin_factor->value() != 0.0;
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        return true;
    }
}

void OperatorConfigurationWidget::configureOperator()
{
    OperatorInterface *op = m_op;

    op->setObjectName(le_name->text());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        s32 bins = spin_xBins->value();

        Slot *slot = histoSink->getSlot(0);
        s32 minIdx = 0;
        s32 maxIdx = slot->inputPipe->parameters.size();

        if (slot->paramIndex != Slot::NoParamIndex)
        {
            minIdx = slot->paramIndex;
            maxIdx = minIdx + 1;
        }

        for (s32 idx = minIdx; idx < maxIdx; ++idx)
        {
            double xMin = slot->inputPipe->parameters[idx].lowerLimit;
            double xMax = slot->inputPipe->parameters[idx].upperLimit;

            auto histo = std::make_shared<Histo1D>(bins, xMin, xMax);
            histoSink->histos.push_back(histo);
        }
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        s32 xBins = spin_xBins->value();
        s32 yBins = spin_yBins->value();

        auto xSlot = op->getSlot(0);
        auto ySlot = op->getSlot(1);

        double xMin = xSlot->inputPipe->parameters[xSlot->paramIndex].lowerLimit;
        double xMax = xSlot->inputPipe->parameters[xSlot->paramIndex].upperLimit;

        double yMin = ySlot->inputPipe->parameters[ySlot->paramIndex].lowerLimit;
        double yMax = ySlot->inputPipe->parameters[ySlot->paramIndex].upperLimit;

        histoSink->m_histo = std::make_shared<Histo2D>(xBins, xMin, xMax,
                                                       yBins, yMin, yMax);
    }
    else if (auto calibration = qobject_cast<Calibration *>(op))
    {
        double factor = spin_factor->value();
        double offset = spin_offset->value();

        calibration->setGlobalCalibration(factor, offset);
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        s32 index = spin_index->value();
        selector->setIndex(index);
    }
}

void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
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
