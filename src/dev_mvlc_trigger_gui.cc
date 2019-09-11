#include <cassert>
#include <QtWidgets>

#include "dev_mvlc_trigger_gui.h"
#include "mvlc/mvlc_trigger_io.h"
#include "qt_util.h"


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

const std::array<QString, trigger_io::Level0::OutputCount> Level0::DefaultUnitNames =
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

Level0::Level0()
{
    outputNames.reserve(DefaultUnitNames.size());

    std::copy(DefaultUnitNames.begin(), DefaultUnitNames.end(),
              std::back_inserter(outputNames));

    timers.fill({});
    slaveTriggers.fill({});
    stackBusy.fill({});
    ioNIM.fill({});
    ioECL.fill({});
}

// Level 1 connections including internal ones between the LUTs.
const std::array<LUT_Connections, trigger_io::Level1::LUTCount> Level1::StaticConnections =
{
    {
        // L1.LUT0
        { { {0, 16}, {0, 17}, {0, 18}, {0, 19}, {0, 20}, {0, 21} } },
        // L1.LUT1
        { { {0, 20}, {0, 21}, {0, 22}, {0, 23}, {0, 24}, {0, 25} } },
        // L1.LUT2
        { { {0,  24}, {0,  25}, {0, 26}, {0, 27}, {0, 28}, {0, 29} }, },

        // L1.LUT3
        { { {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2} }, },
        // L1.LUT4
        { { {1, 1, 0}, {1, 1, 1}, {1, 1, 2}, {1, 2, 0}, {1, 2, 1}, {1, 2, 2} }, },
    },
};

Level1::Level1()
{
    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].outputNames[output] = QString("L1.LUT%1.OUT%2").arg(unit).arg(output);
        }
    }
}

// Level 2 connections. This table includes fixed and dynamic connections.
// When constructing a new Level2 object a copy of this table is stored in the
// Level2::connections member variable. The dynamic connections of this table
// can then be modified.
// TODO: additionally a rule table is needed which stored the allowed input
// sources for each of the dynamic onnections.
// TODO: level 2 strobe GG inputs are still missing
static const UnitConnection Dynamic = UnitConnection::makeDynamic();
static const UnitConnection DynamicUnavail = UnitConnection::makeDynamic(false);

// Using level 1 unit + output address values (basically full addresses
// without the need for a Level1::OutputPinMapping).
const std::array<LUT_Connections, trigger_io::Level2::LUTCount> Level2::StaticConnections =
{
    {
        // L2.LUT0
        { Dynamic, Dynamic, Dynamic , { 1, 3, 0 }, { 1, 3, 1 }, { 1, 3, 2 } },
        // L2.LUT1
        { Dynamic, Dynamic, Dynamic , { 1, 4, 0 }, { 1, 4, 1 }, { 1, 4, 2 } },
    },
};

// Unit is 0/1 for LUT0/1
Level2LUTsDynamicInputChoices make_level2_input_choices(unsigned unit)
{
    std::vector<UnitAddress> common(14);

    for (unsigned i = 0; i < common.size(); i++)
        common[i] = { 0, i };

    Level2LUTsDynamicInputChoices ret;
    ret.inputChoices = { common, common, common };
    ret.strobeInputChoices = common;

    if (unit == 0)
    {
        for (unsigned i = 0; i < 3; i++)
            ret.inputChoices[i].push_back({ 1, 4, i });
    }
    else if (unit == 1)
    {
        for (unsigned i = 0; i < 3; i++)
            ret.inputChoices[i].push_back({ 1, 3, i });
    }

    // Strobe inputs can connect to all 6 level 1 outputs
    for (unsigned i = 0; i < 3; i++)
        ret.strobeInputChoices.push_back({ 1, 3, i });

    for (unsigned i = 0; i < 3; i++)
        ret.strobeInputChoices.push_back({ 1, 4, i });

    return ret;
};

Level2::Level2()
{
    for (size_t unit = 0; unit < luts.size(); unit++)
    {
        for (size_t output = 0; output < luts[unit].outputNames.size(); output++)
        {
            luts[unit].outputNames[output] = QString("L2.LUT%1.OUT%2").arg(unit).arg(output);
        }
    }
}

const std::array<QString, trigger_io::Level3::UnitCount> UnitNames =
{
    "StackStart0",
    "StackStart1",
    "StackStart2",
    "StackStart3",
    "MasterTrigger0",
    "MasterTrigger1",
    "MasterTrigger2",
    "MasterTrigger3",
    "Counter0",
    "Counter1",
    "Counter2",
    "Counter3",
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

// Note: these lists reference the subunits of the respective level, not the output pins.
static const std::array<UnitConnection, 6> Level3OutputsDynamicInputChoices =
{
    {
        { 2, 0 },
        { 2, 1 },
        { 2, 2 },
        { 2, 3 },
        { 2, 4 },
        { 2, 5 },
    }
};

static const std::array<UnitConnection, 18> Level3StackStartDynamicInputChoices =
{
    {
        { 2, 0 },
        { 2, 1 },
        { 2, 2 },
        { 2, 3 },
        { 2, 4 },
        { 2, 5 },

        { 0, 0 },
        { 0, 1 },
        { 0, 2 },
        { 0, 3 },
        { 0, 4 },
        { 0, 5 },
        { 0, 6 },
        { 0, 7 },
        { 0, 8 },
        { 0, 9 },
        { 0, 10 },
        { 0, 11 },
    },
};

static const std::array<UnitConnection, 18> Level3MasterTriggerDynamicInputChoices =
{
    {
        { 2, 0 },
        { 2, 1 },
        { 2, 2 },
        { 2, 3 },
        { 2, 4 },
        { 2, 5 },

        { 0, 0 },
        { 0, 1 },
        { 0, 2 },
        { 0, 3 },
        { 0, 4 },
        { 0, 5 },
        { 0, 6 },
        { 0, 7 },
        { 0, 8 },
        { 0, 9 },
        { 0, 10 },
        { 0, 11 },
    }
};

QString lookup_name(const Config &cfg, const UnitAddress &addr)
{
    switch (addr[0])
    {
        case 0:
            return cfg.l0.outputNames.value(addr[1]);

        case 1:
            return cfg.l1.luts[addr[1]].outputNames[addr[2]];

        case 2:
            return cfg.l2.luts[addr[1]].outputNames[addr[2]];

        case 3:
            break;
    }

    return {};
}

TriggerIOGraphicsScene::TriggerIOGraphicsScene(QObject *parent)
    : QGraphicsScene(parent)
{
    auto make_lut_item = [] (const QRectF &lutRect, int lutIdx, QGraphicsItem *parent) -> QGraphicsItem *
    {
        auto lutItem = new QGraphicsRectItem(lutRect, parent);
        lutItem->setBrush(QBrush("#fffbcc"));

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

        return lutItem;
    };

    auto make_level0_items = [&] () -> Level0Items
    {
        Level0Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            3 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM+ECL IO Item
        {
            result.nim_io_item = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.nim_io_item->setBrush(QBrush("#fffbcc"));
            result.nim_io_item->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Inputs"), result.nim_io_item);
            label->moveBy((result.nim_io_item->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L0", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    auto make_level1_items = [&] () -> Level1Items
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
            auto lutItem = make_lut_item(lutRect, lutIdx, result.parent);
            result.luts[lutIdx] = lutItem;
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

    auto make_level2_items = [&] () -> Level2Items
    {
        Level2Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        // background box containing the 2 LUTs
        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            3 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = make_lut_item(lutRect, lutIdx, result.parent);
            result.luts[lutIdx] = lutItem;
        }

        lutRect.moveTo(lutRect.width() + 50, 0);
        lutRect.translate(25, 25);
        lutRect.translate(0, (lutRect.height() + 25) / 2.0);
        result.luts[1]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[0]->setPos(lutRect.topLeft());

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L2", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    auto make_level3_items = [&] () -> Level3Items
    {
        Level3Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            3 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM+ECL IO Item
        {
            result.ioItem = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.ioItem->setBrush(QBrush("#fffbcc"));
            result.ioItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM/ECL Out"), result.ioItem);
            label->moveBy((result.ioItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L3", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    m_level0Items = make_level0_items();
    m_level0Items.parent->moveBy(-300, 0);
    m_level1Items = make_level1_items();
    m_level2Items = make_level2_items();
    m_level2Items.parent->moveBy(300, 0);
    m_level3Items = make_level3_items();
    m_level3Items.parent->moveBy(600, 0);

    this->addItem(m_level0Items.parent);
    this->addItem(m_level1Items.parent);
    this->addItem(m_level2Items.parent);
    this->addItem(m_level3Items.parent);
};

void TriggerIOGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev)
{
    auto items = this->items(ev->scenePos());

    // level0
    if (items.indexOf(m_level0Items.nim_io_item) >= 0)
    {
        ev->accept();
        emit editNIM_IOs();
        return;
    }

    // level1
    for (size_t unit = 0; unit < m_level1Items.luts.size(); unit++)
    {
        if (items.indexOf(m_level1Items.luts[unit]) >= 0)
        {
            ev->accept();
            emit editLUT(1, unit);
            return;
        }
    }

    // level2
    for (size_t unit = 0; unit < m_level2Items.luts.size(); unit++)
    {
        if (items.indexOf(m_level2Items.luts[unit]) >= 0)
        {
            ev->accept();
            emit editLUT(2, unit);
            return;
        }
    }
}

static void reverse_rows(QTableWidget *table)
{
    for (int row = 0; row < table->rowCount() / 2; row++)
    {
        auto vView = table->verticalHeader();
        vView->swapSections(row, table->rowCount() - 1 - row);
    }
}

NIM_IO_Table_UI make_nim_io_settings_table()
{
    NIM_IO_Table_UI ret = {};

    auto table = new QTableWidget(trigger_io::NIM_IO_Count, 7);
    ret.table = table;

    table->setHorizontalHeaderLabels(
        {"Activate", "Direction", "Delay", "Width", "Holdoff", "Invert", "Name"});

    for (int row = 0; row < table->rowCount(); ++row)
    {
        table->setVerticalHeaderItem(row, new QTableWidgetItem(
                QString("NIM%1").arg(row)));

        auto combo_dir = new QComboBox;
        combo_dir->addItem("IN");
        combo_dir->addItem("OUT");

        auto check_activate = new QCheckBox;
        auto check_invert = new QCheckBox;

        ret.combos_direction.push_back(combo_dir);
        ret.checks_activate.push_back(check_activate);
        ret.checks_invert.push_back(check_invert);

        table->setCellWidget(row, 0, make_centered(check_activate));
        table->setCellWidget(row, 1, combo_dir);
        table->setCellWidget(row, 5, make_centered(check_invert));
        table->setItem(row, 6, new QTableWidgetItem(
                QString("NIM%1").arg(row)));
    }

    reverse_rows(table);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    return ret;
}

IOSettingsWidget::IOSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    // NIM IO
    auto page_NIM = new QWidget;
    {
        auto table_ui = make_nim_io_settings_table();
        auto l = new QHBoxLayout(page_NIM);
        l->addWidget(table_ui.table);
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

//
// NIM_IO_SettingsDialog
//
NIM_IO_SettingsDialog::NIM_IO_SettingsDialog(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    QWidget *parent)
    : QDialog(parent)
{
    m_tableUi = make_nim_io_settings_table();
    auto &ui = m_tableUi;

    for (int row = 0; row < ui.table->rowCount(); row++)
    {
        auto name = names.value(row);
        auto io = settings.value(row);

        ui.table->setItem(row, ui.ColName, new QTableWidgetItem(name));
        ui.table->setItem(row, ui.ColDelay, new QTableWidgetItem(QString::number(io.delay)));
        ui.table->setItem(row, ui.ColWidth, new QTableWidgetItem(QString::number(io.width)));
        ui.table->setItem(row, ui.ColHoldoff, new QTableWidgetItem(QString::number(io.holdoff)));

        ui.combos_direction[row]->setCurrentIndex(static_cast<int>(io.direction));
        ui.checks_activate[row]->setChecked(io.activate);
        ui.checks_invert[row]->setChecked(io.invert);
    }

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addWidget(ui.table, 1);
    widgetLayout->addWidget(bb);

    resize(500, 500);
}

QStringList NIM_IO_SettingsDialog::getNames() const
{
    auto &ui = m_tableUi;
    QStringList ret;

    for (int row = 0; row < ui.table->rowCount(); row++)
        ret.push_back(ui.table->item(row, ui.ColName)->text());

    return ret;
}

QVector<trigger_io::IO> NIM_IO_SettingsDialog::getSettings() const
{
    auto &ui = m_tableUi;
    QVector<trigger_io::IO> ret;

    for (int row = 0; row < ui.table->rowCount(); row++)
    {
        trigger_io::IO nim;

        nim.delay = ui.table->item(row, ui.ColDelay)->text().toUInt();
        nim.width = ui.table->item(row, ui.ColWidth)->text().toUInt();
        nim.holdoff = ui.table->item(row, ui.ColHoldoff)->text().toUInt();

        nim.invert = ui.checks_invert[row]->isChecked();
        nim.direction = static_cast<trigger_io::IO::Direction>(
            ui.combos_direction[row]->currentIndex());
        nim.activate = ui.checks_activate[row]->isChecked();

        ret.push_back(nim);
    }

    return ret;
}

// TODO: add AND, OR, invert and [min, max] bits setup helpers
LUTOutputEditor::LUTOutputEditor(
    int outputNumber,
    const QVector<QStringList> &inputNameLists,
    QWidget *parent)
    : QWidget(parent)
{
    // LUT input bit selection
    auto table_inputs = new QTableWidget(trigger_io::LUT::InputBits, 2);
    table_inputs->setHorizontalHeaderLabels({"Use", "Name" });

    for (int row = 0; row < table_inputs->rowCount(); row++)
    {
        table_inputs->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

        auto cb = new QCheckBox;
        m_inputCheckboxes.push_back(cb);

        table_inputs->setCellWidget(row, 0, make_centered(cb));

        auto inputNames = inputNameLists.value(row);

        // Multiple names in the list -> use a combobox to select which to use
        if (inputNames.size() > 1)
        {
            auto combo = new QComboBox;
            combo->addItems(inputNames);
            table_inputs->setCellWidget(row, 1, combo);

            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, row] (int index) {
                        if (index >= 0)
                        {
                            emit inputConnectionChanged(row, index);
                        }
                    });

            m_inputConnectionCombos.push_back(combo);
        }
        else // Single name -> use a plain table item
        {
            auto nameItem = new QTableWidgetItem(inputNames.value(0));
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            table_inputs->setItem(row, 1, nameItem);
            m_inputConnectionCombos.push_back(nullptr);
        }
    }

    // Reverse the row order by swapping the vertical header view sections.
    // This way the bits are ordered the same way as in the rows of the output
    // state table: bit 0 is the rightmost bit.
    reverse_rows(table_inputs);

    table_inputs->setMinimumWidth(250);
    table_inputs->setSelectionMode(QAbstractItemView::NoSelection);
    table_inputs->resizeColumnsToContents();
    table_inputs->resizeRowsToContents();

    for (auto cb: m_inputCheckboxes)
    {
        connect(cb, &QCheckBox::stateChanged,
                this, &LUTOutputEditor::onInputUsageChanged);
    }

    // Initially empty output value table. Populated in onInputUsageChanged().
    m_outputTable = new QTableWidget(0, 1);
    m_outputTable->setHorizontalHeaderLabels({"State"});

    auto widget_inputSelect = new QWidget;
    auto layout_inputSelect = make_layout<QVBoxLayout>(widget_inputSelect);
    layout_inputSelect->addWidget(new QLabel("Input Selection"));
    layout_inputSelect->addWidget(table_inputs, 1);

    auto widget_outputActivation = new QWidget;
    auto layout_outputActivation = make_layout<QVBoxLayout>(widget_outputActivation);
    layout_outputActivation->addWidget(new QLabel("Output Activation"));
    layout_outputActivation->addWidget(m_outputTable);

    auto layout = make_layout<QVBoxLayout>(this);
    layout->addWidget(widget_inputSelect, 35);
    layout->addWidget(widget_outputActivation, 65);
}

void LUTOutputEditor::onInputUsageChanged()
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

void LUTOutputEditor::setInputConnection(unsigned input, unsigned value)
{
    if (auto combo = m_inputConnectionCombos.value(input))
    {
        combo->setCurrentIndex(value);
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
    const QVector<QStringList> &inputNameLists,
    const QStringList &outputNames,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lutName);

    auto editorLayout = make_hbox<0, 0>();

    for (int output = 0; output < trigger_io::LUT::OutputBits; output++)
    {
        auto lutOutputEditor = new LUTOutputEditor(output, inputNameLists);

        auto nameEdit = new QLineEdit;
        nameEdit->setText(outputNames.value(output));

        auto nameEditLayout = make_hbox();
        nameEditLayout->addWidget(new QLabel("Output Name:"));
        nameEditLayout->addWidget(nameEdit, 1);

        auto gb = new QGroupBox(QString("Out%1").arg(output));
        auto gbl = make_vbox(gb);
        gbl->addLayout(nameEditLayout);
        gbl->addWidget(lutOutputEditor);

        editorLayout->addWidget(gb);

        m_outputEditors.push_back(lutOutputEditor);
        m_outputNameEdits.push_back(nameEdit);

        // Propagate input connection changes from each editor to all editors.
        // This keeps the connection combo boxes in sync.
        connect(lutOutputEditor, &LUTOutputEditor::inputConnectionChanged,
                this, [this] (unsigned input, unsigned value) {
                    for (auto &editor: m_outputEditors)
                        editor->setInputConnection(input, value);
                });
    }

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto scrollWidget = new QWidget;
    auto scrollLayout = make_layout<QVBoxLayout>(scrollWidget);
    scrollLayout->addLayout(editorLayout, 1);
    scrollLayout->addWidget(bb);

    auto scrollArea = new QScrollArea;
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    auto widgetLayout = make_hbox<0, 0>(this);
    widgetLayout->addWidget(scrollArea);
}

QStringList LUTEditor::getOutputNames() const
{
    QStringList ret;

    for (auto le: m_outputNameEdits)
        ret.push_back(le->text());

    return ret;
}

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config
