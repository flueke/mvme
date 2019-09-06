#include <cassert>
#include <QtWidgets>

#include "dev_mvlc_trigger_gui.h"
#include "mvlc/mvlc_trigger_io.h"

using namespace mesytec::mvlc;

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

LUTOutputEditor::LUTOutputEditor(QWidget *parent)
    : QWidget(parent)
{
    // LUT input bit selection
    auto table_inputs = new QTableWidget(2, trigger_io::LUT::InputBits);
    table_inputs->setVerticalHeaderLabels({"Use", "Name" });

    // FIXME: either reverse column order (right to left) or transpose the
    // table again so that it goes from top to bottom. In the 2nd case maybe
    // try to sort in reverse order.
    for (int col = 0; col < table_inputs->columnCount(); col++)
    {
        table_inputs->setHorizontalHeaderItem(col, new QTableWidgetItem(
                QString("%1").arg(col)));

        auto cb = new QCheckBox;
        m_inputCheckboxes.push_back(cb);

        table_inputs->setCellWidget(0, col, make_centered(cb));
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
    layout_inputSelect->addWidget(make_centered(new QLabel("Input Selection")));
    layout_inputSelect->addWidget(table_inputs, 1);

    auto widget_outputActivation = new QWidget;
    auto layout_outputActivation = new QVBoxLayout(widget_outputActivation);
    layout_outputActivation->addWidget(make_centered(new QLabel("Output Activation")));
    layout_outputActivation->addWidget(m_outputTable);

    auto splitter = new QSplitter;
    splitter->addWidget(widget_inputSelect);
    splitter->addWidget(widget_outputActivation);

    auto layout = new QHBoxLayout(this);
    layout->addWidget(splitter);
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
                        if (outputMapping.test(i))
                            qDebug() << __PRETTY_FUNCTION__ << i;
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

// FIXME: this does not yield enough values.  When e.g. bits 4 and 5 are used
// as input bits and the combination 00 is set to activate the output then all
// input values where bits 4 and 5 are 00 must be set in the result
// (00yyy with yyy being all binary permutations).
// With bits 3 and 5 it would become (0y0yy)
LUTOutputEditor::OutputMapping LUTOutputEditor::getOutputMapping() const
{
    OutputMapping result;

    auto bitMap = getInputBitMapping();

    for (int row = 0; row < m_outputStateWidgets.size(); ++row)
    {
        if (!m_outputStateWidgets[row]->isChecked())
            continue;

        unsigned inputValue = 0u;

        for (int mappedBit = 0; mappedBit < bitMap.size(); ++mappedBit)
        {
            if (row & (1u << mappedBit))
            {
                inputValue |= 1u << bitMap[mappedBit];
            }
        }

        assert(inputValue < result.size());

        result.set(inputValue);
    }

    return result;
}

LUTEditor::LUTEditor(QWidget *parent)
    : QDialog(parent)
{
    auto tabWidget = new QTabWidget;

    for (int output = 0; output < trigger_io::LUT::OutputBits; output++)
    {
        auto page = new QWidget;
        auto l = new QHBoxLayout(page);
        l->addWidget(new LUTOutputEditor);
        tabWidget->addTab(page, QString("Out%1").arg(output));
    }

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->addWidget(tabWidget, 1);
    widgetLayout->addWidget(bb);
}
