#include <cassert>
#include <QtWidgets>

#include "dev_mvlc_trigger_gui.h"
#include "mvlc/mvlc_trigger_io.h"


namespace
{
    QWidget *make_centered(QWidget *widget)
    {
        auto w = new QWidget;
        auto l = new QHBoxLayout(w);
        l->setSpacing(0);
        l->setContentsMargins(0, 0, 0, 0);
        l->addStretch(1);
        l->addWidget(widget);
        l->addStretch(1);
        return w;
    }
}

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
{

const std::array<QString, trigger_io::Level0::OutputCount> Level0::DefaultOutputNames =
{
    "timer0",
    "timer1",
    "timer2",
    "timer3",
    "IRQ0",
    "IRQ1",
    "soft_trigger0",
    "soft_trigger1",
    "slave_trigger0",
    "slave_trigger1",
    "slave_trigger2",
    "slave_trigger3",
    "stack_busy0",
    "stack_busy1",
    "N/A",
    "N/A",
    "NIM0",
    "NIM1",
    "NIM2",
    "NIM3",
    "NIM4",
    "NIM5",
    "NIM6",
    "NIM7",
    "NIM8",
    "NIM9",
    "NIM10",
    "NIM11",
    "NIM12",
    "NIM13",
    "ECL0",
    "ECL1",
    "ECL2",
};

// Level0 output pin -> address of the unit producing the output
const std::array<UnitAddress, trigger_io::NIM_IO_Count> Level0::OutputPinMapping
{
    {
        {0, 16}, {0, 17}, {0, 18}, {0, 19}, {0, 20}, {0, 21}, {0, 22},
        {0, 23}, {0, 24}, {0, 25}, {0, 26}, {0, 27}, {0, 28}, {0, 29},
    }
};

Level0::Level0()
{
    outputNames.reserve(DefaultOutputNames.size());

    std::copy(DefaultOutputNames.begin(), DefaultOutputNames.end(),
              std::back_inserter(outputNames));
}

// Level 1 connections including internal ones between the LUTs.
const std::array<LUT_Connections, trigger_io::Level1::LUTCount> Level1::StaticConnections =
{
    {
        // L1.LUT0
        { { {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5} } },
        // L1.LUT1
        { { {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 8}, {0, 9} } },
        // L1.LUT2
        { { {0,  8}, {0,  9}, {0, 10}, {0, 11}, {0, 12}, {0, 13} }, },

        // L1.LUT3
        { { {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2} }, },
        // L1.LUT4
        { { {1, 1, 0}, {1, 1, 1}, {1, 1, 2}, {1, 2, 0}, {1, 2, 1}, {1, 2, 2} }, },
    },
};

// Level1 output -> address of the unit producing the output
const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> Level1::OutputPinMapping
{
    { {1, 3, 0 }, { 1, 3, 1 }, { 1, 3, 2 }, { 1, 4, 0 }, { 1, 4, 1 }, { 1, 4, 2 } }
};



TriggerIOGraphicsScene::TriggerIOGraphicsScene(QObject *parent)
    : QGraphicsScene(parent)
{
    auto make_level1_items = [] () -> Level1Items
    {
        Level1Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        // background box containing the 5 LUTs
        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            3 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = new QGraphicsRectItem(lutRect, result.parent);
            lutItem->setBrush(QBrush("#fffbcc"));
            result.luts[lutIdx] = lutItem;

            auto label = new QGraphicsSimpleTextItem(QString("LUT%1").arg(lutIdx), lutItem);
            label->moveBy((lutItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            for (int input = 5; input >= 0; input--)
            {
                double r = 4;
                auto circle = new QGraphicsEllipseItem(0, 0, 2*r, 2*r, lutItem);
                circle->setPen(Qt::NoPen);
                circle->setBrush(Qt::blue);
                circle->moveBy(-circle->boundingRect().width() / 2.0,
                               -circle->boundingRect().height() / 2.0);

                const int Inputs = 6;
                double margin = 20;
                double height = lutRect.height() - 2 * margin;
                double stepHeight = height / Inputs;
                int topDownIndex = Inputs - 1 - input;

                circle->moveBy(0, margin + topDownIndex * (stepHeight + r));
            }

            for (int output = 2; output >= 0; output--)
            {
                double r = 4;
                auto circle = new QGraphicsEllipseItem(0, 0, 2*r, 2*r, lutItem);
                circle->setPen(Qt::NoPen);
                circle->setBrush(Qt::blue);
                circle->moveBy(lutRect.width() - circle->boundingRect().width() / 2.0,
                               -circle->boundingRect().height() / 2.0);

                const int Outputs = 3;
                double margin = 40;
                double height = lutRect.height() - 2 * margin;
                double stepHeight = height / Outputs;
                int topDownIndex = Outputs - 1 - output;

                circle->moveBy(0, margin + topDownIndex * (stepHeight + r));
            }
        }

        lutRect.translate(25, 25);
        result.luts[2]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[1]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[0]->setPos(lutRect.topLeft());

        lutRect.moveTo(lutRect.width() + 50, 0);
        lutRect.translate(25, 25);
        lutRect.translate(0, (lutRect.height() + 25) / 2.0);
        result.luts[4]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[3]->setPos(lutRect.topLeft());

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L1", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    m_level1Items = make_level1_items();
    this->addItem(m_level1Items.parent);
};

void TriggerIOGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev)
{
    auto items = this->items(ev->scenePos());

    for (size_t unit = 0; unit < m_level1Items.luts.size(); unit++)
    {
        if (items.indexOf(m_level1Items.luts[unit]) >= 0)
        {
            ev->accept();
            emit editLUT(1, unit);
            return;
        }
    }
}

IOSettingsWidget::IOSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    // NIM IO
    auto page_NIM = new QWidget;
    {
        auto table = new QTableWidget(trigger_io::NIM_IO_Count, 7);

        table->setHorizontalHeaderLabels(
            {"Activate", "Direction", "Delay", "Width", "Holdoff", "Invert", "Name"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("NIM%1").arg(row)));

            auto combo_dir = new QComboBox;
            combo_dir->addItem("IN");
            combo_dir->addItem("OUT");

            table->setCellWidget(row, 0, make_centered(new QCheckBox()));
            table->setCellWidget(row, 1, combo_dir);
            table->setCellWidget(row, 5, make_centered(new QCheckBox()));
            table->setItem(row, 6, new QTableWidgetItem(
                    QString("NIM%1").arg(row)));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto l = new QHBoxLayout(page_NIM);
        l->addWidget(table);
    }

    // ECL Out
    auto page_ECL = new QWidget;
    {
        auto table = new QTableWidget(trigger_io::ECL_OUT_Count, 6);
        table->setHorizontalHeaderLabels(
            {"Activate", "Delay", "Width", "Holdoff", "Invert", "Name"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("ECL%1").arg(row)));

            table->setCellWidget(row, 0, make_centered(new QCheckBox()));
            table->setCellWidget(row, 4, make_centered(new QCheckBox()));
            table->setItem(row, 5, new QTableWidgetItem(
                    QString("ECL%1").arg(row)));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto l = new QHBoxLayout(page_ECL);
        l->addWidget(table);
    }

    // Timers
    auto page_Timers = new QWidget;
    {
        auto table = new QTableWidget(trigger_io::TimerCount, 4);
        table->setHorizontalHeaderLabels({"Range", "Period", "Delay", "Name"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

            auto combo_range = new QComboBox;
            combo_range->addItem("ns", 0);
            combo_range->addItem("µs", 1);
            combo_range->addItem("ms", 2);
            combo_range->addItem("s",  3);

            table->setCellWidget(row, 0, combo_range);
            table->setItem(row, 1, new QTableWidgetItem(QString::number(16)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(0)));
            table->setItem(row, 3, new QTableWidgetItem(
                    QString("Timer%1").arg(row)));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto l = new QHBoxLayout(page_Timers);
        l->addWidget(table);
    }

    auto tabWidget = new QTabWidget;

    tabWidget->addTab(page_NIM, "NIM IO");
    tabWidget->addTab(page_ECL, "ECL Out");
    tabWidget->addTab(page_Timers, "Timers");

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(tabWidget);
}

// TODO: add AND, OR, invert and [min, max] bits setup helpers
LUTOutputEditor::LUTOutputEditor(
    int outputNumber,
    const QStringList &inputNames,
    QWidget *parent)
    : QWidget(parent)
{
    // LUT input bit selection
    auto table_inputs = new QTableWidget(2, trigger_io::LUT::InputBits);
    table_inputs->setVerticalHeaderLabels({"Use", "Name" });

    for (int col = 0; col < table_inputs->columnCount(); col++)
    {
        table_inputs->setHorizontalHeaderItem(col, new QTableWidgetItem(
                QString("%1").arg(col)));

        auto cb = new QCheckBox;
        m_inputCheckboxes.push_back(cb);

        table_inputs->setCellWidget(0, col, make_centered(cb));

        qDebug() << __PRETTY_FUNCTION__ << inputNames;
        auto nameItem = new QTableWidgetItem(inputNames.value(col));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        table_inputs->setItem(1, col, nameItem);
    }

    // Reverse the column order by swapping the horizontal header view sections.
    // This way the bits are ordered the same way as in the rows of the output
    // state table: bit 0 is the rightmost bit.
    for (int col = 0; col < table_inputs->columnCount() / 2; col++)
    {
        auto hView = table_inputs->horizontalHeader();
        hView->swapSections(col, table_inputs->columnCount() - 1 - col);
    }

    table_inputs->setSelectionMode(QAbstractItemView::NoSelection);
    table_inputs->resizeColumnsToContents();
    table_inputs->resizeRowsToContents();

    for (auto cb: m_inputCheckboxes)
    {
        connect(cb, &QCheckBox::stateChanged,
                this, &LUTOutputEditor::onInputSelectionChanged);
    }

    // Initially empty output value table. Populated in onInputSelectionChanged().
    m_outputTable = new QTableWidget(0, 1);
    m_outputTable->setHorizontalHeaderLabels({"State"});

    auto widget_inputSelect = new QWidget;
    auto layout_inputSelect = new QVBoxLayout(widget_inputSelect);
    layout_inputSelect->addWidget(new QLabel("Input Selection"));
    layout_inputSelect->addWidget(table_inputs, 1);

    auto widget_outputActivation = new QWidget;
    auto layout_outputActivation = new QVBoxLayout(widget_outputActivation);
    layout_outputActivation->addWidget(new QLabel("Output Activation"));
    layout_outputActivation->addWidget(m_outputTable);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(widget_inputSelect, 1);
    layout->addWidget(widget_outputActivation, 3);
}

void LUTOutputEditor::onInputSelectionChanged()
{
    auto make_bit_string = [](unsigned totalBits, unsigned value)
    {
        QString str(totalBits, '0');

        for (unsigned bit = 0; bit < totalBits; ++bit)
        {
            if (static_cast<unsigned>(value) & (1u << bit))
                str[totalBits - bit - 1] = '1';
        }

        return str;
    };

    auto bitMap = getInputBitMapping();
    unsigned totalBits = static_cast<unsigned>(bitMap.size());
    unsigned rows = totalBits > 0 ? 1u << totalBits : 0u;

    assert(rows <= 64);

    m_outputTable->setRowCount(0);
    m_outputTable->setRowCount(rows);
    m_outputStateWidgets.clear();

    for (int row = 0; row < m_outputTable->rowCount(); ++row)
    {
        auto rowHeader = make_bit_string(totalBits, row);
        m_outputTable->setVerticalHeaderItem(row, new QTableWidgetItem(rowHeader));

        auto button = new QPushButton("0");
        button->setCheckable(true);
        m_outputStateWidgets.push_back(button);

        connect(button, &QPushButton::toggled,
                this, [button] (bool checked) {
                    button->setText(checked ? "1" : "0");
                });

        m_outputTable->setCellWidget(row, 0, make_centered(button));
    }

    // FIXME: debug output only
    for (auto cb: m_outputStateWidgets)
    {
        connect(cb, &QPushButton::toggled,
                this, [this] ()
                {
                    qDebug() << __PRETTY_FUNCTION__ << ">>>";
                    auto outputMapping = getOutputMapping();
                    for (size_t i = 0; i < outputMapping.size(); i++)
                    {
                        qDebug() << __PRETTY_FUNCTION__
                            << QString("%1").arg(i, 2)
                            << QString("%1")
                            .arg(QString::number(i, 2), 6, QLatin1Char('0'))
                            << "->" << outputMapping.test(i)
                            ;
                    }
                    qDebug() << __PRETTY_FUNCTION__ << "<<<";
                });
    }
}


QVector<unsigned> LUTOutputEditor::getInputBitMapping() const
{
    QVector<unsigned> bitMap;

    for (int bit = 0; bit < m_inputCheckboxes.size(); ++bit)
    {
        if (m_inputCheckboxes[bit]->isChecked())
            bitMap.push_back(bit);
    }

    return bitMap;
}

// Returns the full 2^6 entry LUT bitset corresponding to the current state of
// the GUI.
OutputMapping LUTOutputEditor::getOutputMapping() const
{
    OutputMapping result;

    const auto bitMap = getInputBitMapping();

    // Create a full 6 bit mask from the input mapping.
    unsigned inputMask  = 0u;

    for (int bitIndex = 0; bitIndex < bitMap.size(); ++bitIndex)
        inputMask |= 1u << bitMap[bitIndex];

    // Note: this is not efficient. If all input bits are used the output state
    // table has 64 entries. The inner loop iterates 64 times. In total this
    // will result in 64 * 64 iterations.

    for (unsigned row = 0; row < static_cast<unsigned>(m_outputStateWidgets.size()); ++row)
    {
        // Calculate the full input value corresponding to this row.
        unsigned inputValue = 0u;

        for (int bitIndex = 0; bitIndex < bitMap.size(); ++bitIndex)
        {
            if (row & (1u << bitIndex))
                inputValue |= 1u << bitMap[bitIndex];
        }

        bool outputBit = m_outputStateWidgets[row]->isChecked();

        for (size_t inputCombination = 0;
             inputCombination < result.size();
             ++inputCombination)
        {
            if (outputBit && ((inputCombination & inputMask) == inputValue))
                result.set(inputCombination);
        }
    }

    return result;
}

void LUTOutputEditor::setOutputMapping(const OutputMapping &mapping)
{
    for (auto cb: m_inputCheckboxes)
        cb->setChecked(true);

    int inputMax = std::min(m_outputStateWidgets.size(), static_cast<int>(mapping.size()));

    for (int input = 0; input < inputMax; ++input)
        m_outputStateWidgets[input]->setChecked(mapping.test(input));
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const QStringList &inputNames,
    const QStringList &outputNames,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lutName);

    auto editorLayout = new QHBoxLayout;

    for (int output = 0; output < trigger_io::LUT::OutputBits; output++)
    {
        auto lutOutputEditor = new LUTOutputEditor(output, inputNames);

        auto nameEdit = new QLineEdit;
        nameEdit->setText(outputNames.value(output));

        auto nameEditLayout = new QHBoxLayout;
        nameEditLayout->addWidget(new QLabel("Name:"));
        nameEditLayout->addWidget(nameEdit, 1);

        auto gb = new QGroupBox(QString("Out%1").arg(output));
        auto gbl = new QVBoxLayout(gb);
        gbl->addLayout(nameEditLayout);
        gbl->addWidget(lutOutputEditor);

        editorLayout->addWidget(gb);

        m_outputEditors.push_back(lutOutputEditor);
        m_outputNameEdits.push_back(nameEdit);
    }

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->addLayout(editorLayout, 1);
    widgetLayout->addWidget(bb);
}

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config
