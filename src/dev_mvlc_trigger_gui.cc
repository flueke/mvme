#include <cassert>
#include <memory>
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
    irqUnits.fill({});
    slaveTriggers.fill({});
    stackBusy.fill({});
    ioNIM.fill({});
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
static const UnitConnection Dynamic = UnitConnection::makeDynamic();

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
Level2LUTDynamicInputChoices make_level2_input_choices(unsigned unit)
{
    // Common to all inputs: can connect to all Level0 utility outputs.
    std::vector<UnitAddress> common(trigger_io::Level0::UtilityUnitCount);

    for (unsigned i = 0; i < common.size(); i++)
        common[i] = { 0, i };

    Level2LUTDynamicInputChoices ret;
    ret.inputChoices = { common, common, common };
    ret.strobeInputChoices = common;

    if (unit == 0)
    {
        // L2.LUT0 can connect to L1.LUT4
        for (unsigned i = 0; i < 3; i++)
            ret.inputChoices[i].push_back({ 1, 4, i });
    }
    else if (unit == 1)
    {
        // L2.LUT1 can connecto to L1.LUT3
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

const std::array<QString, trigger_io::Level3::UnitCount> Level3::DefaultUnitNames =
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
    "N/A",
    "N/A",
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

static std::vector<UnitAddressVector> make_level3_dynamic_input_choice_lists()
{
    static const std::vector<UnitAddress> Level2Full =
    {
        { 2, 0, 0 },
        { 2, 0, 1 },
        { 2, 0, 2 },
        { 2, 1, 0 },
        { 2, 1, 1 },
        { 2, 1, 2 },
    };

    std::vector<UnitAddressVector> result;

    for (size_t i = 0; i < trigger_io::Level3::StackStartCount; i++)
    {
        std::vector<UnitAddress> choices = Level2Full;

        // Can connect up to L0.SlaveTriggers[3]
        for (unsigned unit = 0; unit <= 11; unit++)
            choices.push_back({0, unit });

        result.emplace_back(choices);
    }

    for (size_t i = 0; i < trigger_io::Level3::MasterTriggerCount; i++)
    {
        std::vector<UnitAddress> choices = Level2Full;

        // Can connect up to the IRQ units
        for (unsigned unit = 0; unit <= 5; unit++)
            choices.push_back({0, unit });

        result.emplace_back(choices);
    }

    for (size_t i = 0; i < trigger_io::Level3::CountersCount; i++)
    {
        std::vector<UnitAddress> choices = Level2Full;

        // Can connect all L0 utilities up to StackBusy1
        for (unsigned unit = 0; unit <= 13; unit++)
            choices.push_back({0, unit });

        result.emplace_back(choices);
    }

    // 4 unused inputs between the last counter (11) and the first NIM_IO (16)
    for (size_t i = 0; i < 4; i++)
    {
        result.push_back({});
    }

    // NIM and ECL outputs can connect to Level2 only
    for (size_t i = 0; i < (trigger_io::NIM_IO_Count + trigger_io::ECL_OUT_Count); i++)
    {
        std::vector<UnitAddress> choices = Level2Full;
        result.emplace_back(choices);
    }

    return result;
}

Level3::Level3()
    : dynamicInputChoiceLists(make_level3_dynamic_input_choice_lists())
{
    stackStart.fill({});
    masterTrigger.fill({});
    counters.fill({});
    ioNIM.fill({});
    ioECL.fill({});

    connections.fill(0);

    std::copy(DefaultUnitNames.begin(), DefaultUnitNames.end(),
              std::back_inserter(unitNames));
}

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
            return cfg.l3.unitNames.value(addr[1]);
    }

    return {};
}

TriggerIOView::TriggerIOView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setRenderHints(
        QPainter::Antialiasing
        | QPainter::TextAntialiasing
        | QPainter::SmoothPixmapTransform
        | QPainter::HighQualityAntialiasing);
}

void TriggerIOView::scaleView(qreal scaleFactor)
{
    double zoomOutLimit = 0.5;
    double zoomInLimit = 5;

    qreal factor = transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < zoomOutLimit || factor > zoomInLimit)
        return;

    scale(scaleFactor, scaleFactor);
}

void TriggerIOView::wheelEvent(QWheelEvent *event)
{
    bool invert = false;
    auto keyMods = event->modifiers();
    double divisor = 300.0;

    if (keyMods & Qt::ControlModifier)
        divisor *= 3.0;

    scaleView(pow((double)2, event->delta() / divisor));
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
            result.nimItem = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.nimItem->setBrush(QBrush("#fffbcc"));
            result.nimItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Inputs"), result.nimItem);
            label->moveBy((result.nimItem->boundingRect().width()
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

    auto make_level0_util_items = [&] () -> Level0UtilItems
    {
        Level0UtilItems result = {};

        QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            1 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // Single box for utils
        {
            result.utilsItem = new QGraphicsRectItem(
                0, 0,
                result.parent->boundingRect().width() - 2 * 25,
                result.parent->boundingRect().height() - 2 * 25,
                result.parent);
            result.utilsItem->setBrush(QBrush("#fffbcc"));
            result.utilsItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(
                QString("Timers\nIRQs\nSoft Triggers\nSlave Trigger Input\nStack Busy"),
                result.utilsItem);
            label->moveBy(5, 5);
            //label->moveBy((result.utilsItem->boundingRect().width()
            //               - label->boundingRect().width()) / 2.0, 0);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L0 Utilities", result.parent);
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
            4 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM IO Item
        {
            result.nimItem = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.nimItem->setBrush(QBrush("#fffbcc"));
            result.nimItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Outputs"), result.nimItem);
            label->moveBy((result.nimItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        auto yOffset = result.nimItem->boundingRect().height() + 25;

        // ECL Out
        {
            result.eclItem = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.eclItem->setBrush(QBrush("#fffbcc"));
            result.eclItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("ECL Outputs"), result.eclItem);
            label->moveBy((result.eclItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            result.eclItem->moveBy(0, yOffset);
        }

        // Utils
        {
            result.utilsItem = new QGraphicsRectItem(0, 0, 100, 140, result.parent);
            result.utilsItem->setBrush(QBrush("#fffbcc"));
            result.utilsItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(
                QString("L3 Utilities\nStack Start\nMaster trig"), result.utilsItem);
            label->moveBy((result.utilsItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            result.utilsItem->moveBy(0, 2 * yOffset);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L3", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    // Top row, side by side gray boxes for each level
    m_level0Items = make_level0_items();
    m_level0Items.parent->moveBy(-300, 0);
    m_level1Items = make_level1_items();
    m_level2Items = make_level2_items();
    m_level2Items.parent->moveBy(300, 0);
    m_level3Items = make_level3_items();
    m_level3Items.parent->moveBy(600, 0);

    m_level0UtilItems = make_level0_util_items();
    m_level0UtilItems.parent->moveBy(
        300, m_level0Items.parent->boundingRect().height() + 15);

    this->addItem(m_level0Items.parent);
    this->addItem(m_level1Items.parent);
    this->addItem(m_level2Items.parent);
    this->addItem(m_level3Items.parent);
    this->addItem(m_level0UtilItems.parent);
};

void TriggerIOGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev)
{
    auto items = this->items(ev->scenePos());

    // level0
    if (items.indexOf(m_level0Items.nimItem) >= 0)
    {
        ev->accept();
        emit editNIM_Inputs();
        return;
    }

    if (items.indexOf(m_level0UtilItems.utilsItem) >= 0)
    {
        ev->accept();
        emit editL0Utils();
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

    // leve3
    if (items.indexOf(m_level3Items.nimItem) >= 0)
    {
        ev->accept();
        emit editNIM_Outputs();
        return;
    }

    if (items.indexOf(m_level3Items.eclItem) >= 0)
    {
        ev->accept();
        emit editECL_Outputs();
        return;
    }

    if (items.indexOf(m_level3Items.utilsItem) >= 0)
    {
        ev->accept();
        emit editL3Utils();
        return;
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

NIM_IO_Table_UI make_nim_io_settings_table(
    const trigger_io::IO::Direction dir)
{
    QStringList columnTitles = {
        "Activate", "Direction", "Delay", "Width", "Holdoff", "Invert", "Name"
    };

    if (dir == trigger_io::IO::Direction::out)
        columnTitles.push_back("Input");

    NIM_IO_Table_UI ret = {};

    auto table = new QTableWidget(trigger_io::NIM_IO_Count, columnTitles.size());
    ret.table = table;

    table->setHorizontalHeaderLabels(columnTitles);

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

        if (dir == trigger_io::IO::Direction::out)
        {
            auto combo_connection = new QComboBox;
            ret.combos_connection.push_back(combo_connection);
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

            table->setCellWidget(row, NIM_IO_Table_UI::ColConnection, combo_connection);
        }
    }

    reverse_rows(table);

    table->horizontalHeader()->moveSection(NIM_IO_Table_UI::ColName, 0);

    if (dir == trigger_io::IO::Direction::out)
        table->horizontalHeader()->moveSection(NIM_IO_Table_UI::ColConnection, 1);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    return ret;
}

ECL_Table_UI make_ecl_table_ui(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    const QVector<unsigned> &inputConnections,
    const QVector<QStringList> &inputChoiceNameLists)
{
    ECL_Table_UI ui = {};

    QStringList columnTitles = {
        "Activate", "Delay", "Width", "Holdoff", "Invert", "Name", "Input"
    };

    auto table = new QTableWidget(trigger_io::ECL_OUT_Count, columnTitles.size());
    ui.table = table;

    table->setHorizontalHeaderLabels(columnTitles);

    for (int row = 0; row < table->rowCount(); ++row)
    {
        table->setVerticalHeaderItem(row, new QTableWidgetItem(
                QString("ECL%1").arg(row)));

        auto check_activate = new QCheckBox;
        auto check_invert = new QCheckBox;
        auto combo_connection = new QComboBox;

        ui.checks_activate.push_back(check_activate);
        ui.checks_invert.push_back(check_invert);
        ui.combos_connection.push_back(combo_connection);

        check_activate->setChecked(settings.value(row).activate);
        check_invert->setChecked(settings.value(row).invert);
        combo_connection->addItems(inputChoiceNameLists.value(row));
        combo_connection->setCurrentIndex(inputConnections.value(row));
        combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);


        table->setCellWidget(row, ui.ColActivate, make_centered(check_activate));
        table->setItem(row, ui.ColDelay, new QTableWidgetItem(QString::number(
                    settings.value(row).delay)));
        table->setItem(row, ui.ColWidth, new QTableWidgetItem(QString::number(
                    settings.value(row).width)));
        table->setItem(row, ui.ColHoldoff, new QTableWidgetItem(QString::number(
                    settings.value(row).holdoff)));
        table->setCellWidget(row, ui.ColInvert, make_centered(check_invert));
        table->setItem(row, ui.ColName, new QTableWidgetItem(names.value(row)));
        table->setCellWidget(row, ui.ColConnection, combo_connection);
    }

    reverse_rows(table);

    table->horizontalHeader()->moveSection(ui.ColName, 0);
    table->horizontalHeader()->moveSection(ui.ColConnection, 1);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    return ui;
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
    const trigger_io::IO::Direction &dir,
    QWidget *parent)
    : QDialog(parent)
{
    m_tableUi = make_nim_io_settings_table(dir);
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
}

NIM_IO_SettingsDialog::NIM_IO_SettingsDialog(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    QWidget *parent)
    : NIM_IO_SettingsDialog(names, settings, trigger_io::IO::Direction::in, parent)
{
    assert(m_tableUi.combos_connection.size() == 0);

    m_tableUi.table->resizeColumnsToContents();
    m_tableUi.table->resizeRowsToContents();
    resize(500, 500);
}

NIM_IO_SettingsDialog::NIM_IO_SettingsDialog(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    const QVector<QStringList> &inputChoiceNameLists,
    const QVector<unsigned> &connections,
    QWidget *parent)
    : NIM_IO_SettingsDialog(names, settings, trigger_io::IO::Direction::out, parent)
{
    assert(m_tableUi.combos_connection.size() == m_tableUi.table->rowCount());

    for (int io = 0; io < m_tableUi.combos_connection.size(); io++)
    {
        m_tableUi.combos_connection[io]->addItems(
            inputChoiceNameLists.value(io));
        m_tableUi.combos_connection[io]->setCurrentIndex(connections.value(io));
    }

    m_tableUi.table->resizeColumnsToContents();
    m_tableUi.table->resizeRowsToContents();
    resize(600, 500);
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

QVector<unsigned> NIM_IO_SettingsDialog::getConnections() const
{
    QVector<unsigned> ret;

    for (auto combo: m_tableUi.combos_connection)
        ret.push_back(combo->currentIndex());

    return ret;
}

//
// ECL_SettingsDialog
//
ECL_SettingsDialog::ECL_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<unsigned> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent)
    : QDialog(parent)
{
    m_tableUi = make_ecl_table_ui(names, settings, inputConnections, inputChoiceNameLists);
    auto &ui = m_tableUi;

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addWidget(ui.table, 1);
    widgetLayout->addWidget(bb);

    resize(600, 500);
}

QStringList ECL_SettingsDialog::getNames() const
{
    auto &ui = m_tableUi;
    QStringList ret;

    for (int row = 0; row < ui.table->rowCount(); row++)
        ret.push_back(ui.table->item(row, ui.ColName)->text());

    return ret;
}

QVector<trigger_io::IO> ECL_SettingsDialog::getSettings() const
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
        nim.activate = ui.checks_activate[row]->isChecked();

        ret.push_back(nim);
    }

    return ret;
}

QVector<unsigned> ECL_SettingsDialog::getConnections() const
{
    QVector<unsigned> ret;

    for (auto combo: m_tableUi.combos_connection)
        ret.push_back(combo->currentIndex());

    return ret;
}

QGroupBox *make_groupbox(QWidget *mainWidget, const QString &title = {},
                         QWidget *parent = nullptr)
{
    auto *ret = new QGroupBox(title, parent);
    auto l = make_hbox<0, 0>(ret);
    l->addWidget(mainWidget);
    return ret;
}

//
// Level0UtilsDialog
//
TimersTable_UI make_timers_table_ui(const Level0 &l0)
{
    static const QString RowTitleFormat = "Timer%1";
    static const QStringList ColumnTitles = { "Name", "Range", "Period", "Delay" };
    const size_t rowCount = l0.timers.size();

    TimersTable_UI ret = {};
    ret.table = new QTableWidget(rowCount, ColumnTitles.size());
    ret.table->setHorizontalHeaderLabels(ColumnTitles);

    for (int row = 0; row < ret.table->rowCount(); ++row)
    {
        ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

        auto combo_range = new QComboBox;
        combo_range->addItem("ns", 0);
        combo_range->addItem("µs", 1);
        combo_range->addItem("ms", 2);
        combo_range->addItem("s",  3);

        combo_range->setCurrentIndex(static_cast<int>(l0.timers[row].range));

        ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                l0.outputNames.value(row)));

        ret.table->setCellWidget(row, ret.ColRange, combo_range);

        ret.table->setItem(row, ret.ColPeriod, new QTableWidgetItem(
                QString::number(l0.timers[row].period)));

        ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(
                l0.timers[row].delay_ns));


        ret.combos_range.push_back(combo_range);
    }

    ret.table->resizeColumnsToContents();
    ret.table->resizeRowsToContents();

    return ret;
}

IRQUnits_UI make_irq_units_table_ui(const Level0 &l0)
{
    static const QString RowTitleFormat = "IRQ%1";
    static const QStringList ColumnTitles = { "Name", "IRQ Index" };
    const size_t rowCount = l0.irqUnits.size();
    const int nameOffset = l0.IRQ_UnitOffset;

    IRQUnits_UI ret = {};
    ret.table = new QTableWidget(rowCount, ColumnTitles.size());
    ret.table->setHorizontalHeaderLabels(ColumnTitles);

    for (int row = 0; row < ret.table->rowCount(); ++row)
    {
        ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

        auto spin_irqIndex = new QSpinBox;
        spin_irqIndex->setRange(1, 7);
        spin_irqIndex->setValue(l0.irqUnits[row].irqIndex + 1);

        ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                l0.outputNames.value(row + nameOffset)));

        ret.table->setCellWidget(row, ret.ColIRQIndex, spin_irqIndex);

        ret.spins_irqIndex.push_back(spin_irqIndex);
    }

    ret.table->resizeColumnsToContents();
    ret.table->resizeRowsToContents();

    return ret;
}

SoftTriggers_UI make_soft_triggers_table_ui(const Level0 &l0)
{
    static const QString RowTitleFormat = "SoftTrigger%1";
    static const QStringList ColumnTitles = { "Name" };
    const int rowCount = l0.SoftTriggerCount;
    const int nameOffset = l0.SoftTriggerOffset;

    SoftTriggers_UI ret = {};
    ret.table = new QTableWidget(rowCount, ColumnTitles.size());
    ret.table->setHorizontalHeaderLabels(ColumnTitles);

    for (int row = 0; row < ret.table->rowCount(); ++row)
    {
        ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

        ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                l0.outputNames.value(row + nameOffset)));
    }

    ret.table->resizeColumnsToContents();
    ret.table->resizeRowsToContents();

    return ret;
}

SlaveTriggers_UI make_slave_triggers_table_ui(const Level0 &l0)
{
    static const QString RowTitleFormat = "SlaveTrigger%1";
    static const QStringList ColumnTitles = { "Name", "Delay", "Width", "Holdoff", "Invert" };
    const size_t rowCount = l0.slaveTriggers.size();
    const int nameOffset = l0.SlaveTriggerOffset;

    SlaveTriggers_UI ret = {};
    ret.table = new QTableWidget(rowCount, ColumnTitles.size());
    ret.table->setHorizontalHeaderLabels(ColumnTitles);

    for (int row = 0; row < ret.table->rowCount(); ++row)
    {
        ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

        const auto &io = l0.slaveTriggers[row];

        auto check_invert = new QCheckBox;
        check_invert->setChecked(io.invert);

        ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                l0.outputNames.value(row + nameOffset)));

        ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(QString::number(io.delay)));
        ret.table->setItem(row, ret.ColWidth, new QTableWidgetItem(QString::number(io.width)));
        ret.table->setItem(row, ret.ColHoldoff, new QTableWidgetItem(QString::number(io.holdoff)));
        ret.table->setCellWidget(row, ret.ColInvert, make_centered(check_invert));

        ret.checks_invert.push_back(check_invert);
    }

    ret.table->resizeColumnsToContents();
    ret.table->resizeRowsToContents();

    return ret;
}

StackBusy_UI make_stack_busy_table_ui(const Level0 &l0)
{
    static const QString RowTitleFormat = "StackBusy%1";
    static const QStringList ColumnTitles = { "Name", "Stack#" };
    const size_t rowCount = l0.stackBusy.size();
    const int nameOffset = l0.StackBusyOffset;

    StackBusy_UI ret = {};
    ret.table = new QTableWidget(rowCount, ColumnTitles.size());
    ret.table->setHorizontalHeaderLabels(ColumnTitles);

    for (int row = 0; row < ret.table->rowCount(); ++row)
    {
        ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

        const auto &stackBusy = l0.stackBusy[row];

        auto spin_stack = new QSpinBox;
        spin_stack->setRange(0, 7);
        spin_stack->setValue(stackBusy.stackIndex);

        ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                l0.outputNames.value(row + nameOffset)));

        ret.table->setCellWidget(row, ret.ColStackIndex, spin_stack);

        ret.spins_stackIndex.push_back(spin_stack);
    }

    ret.table->resizeColumnsToContents();
    ret.table->resizeRowsToContents();

    return ret;
}

Level0UtilsDialog::Level0UtilsDialog(
            const Level0 &l0,
            QWidget *parent)
    : QDialog(parent)
{
    m_timersUi = make_timers_table_ui(l0);
    m_irqUnitsUi = make_irq_units_table_ui(l0);
    m_softTriggersUi = make_soft_triggers_table_ui(l0);
    m_slaveTriggersUi = make_slave_triggers_table_ui(l0);
    m_stackBusyUi = make_stack_busy_table_ui(l0);

    auto grid = new QGridLayout;
    grid->addWidget(make_groupbox(m_timersUi.table, "Timers"), 0, 0);
    grid->addWidget(make_groupbox(m_irqUnitsUi.table, "IRQ Units"), 0, 1);
    grid->addWidget(make_groupbox(m_softTriggersUi.table, "SoftTriggers"), 0, 2);
    grid->addWidget(make_groupbox(m_slaveTriggersUi.table, "SlaveTriggers"), 1, 0);
    grid->addWidget(make_groupbox(m_stackBusyUi.table, "StackBusy"), 1, 1);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addLayout(grid);
    widgetLayout->addWidget(bb);
}

//
// Level3UtilsDialog
//
Level3UtilsDialog::Level3UtilsDialog(
    const QStringList &names,
    const Level3 &l3,
    const QVector<unsigned> &inputConnections,
    const QVector<QStringList> &inputChoiceNameLists,
    QWidget *parent)
{
    struct Table_UI_Base
    {
        QTableWidget *table;
        QVector<QCheckBox *> checks_activate;
        QVector<QComboBox *> combos_connection;
    };

    struct StackStart_UI: public Table_UI_Base
    {
        enum Columns
        {
            ColName,
            ColConnection,
            ColStack,
            ColActivate,
        };

        const int FirstUnitIndex = 0;

        QVector<QSpinBox *> spins_stack;
    };

    struct MasterTriggers_UI: public Table_UI_Base
    {
        enum Columns
        {
            ColName,
            ColConnection,
            ColActivate,
        };

        const int FirstUnitIndex = 4;
    };

    struct Counters_UI: public Table_UI_Base
    {
        enum Columns
        {
            ColName,
            ColConnection,
            ColActivate,
        };

        const int FirstUnitIndex = 8;
    };

    auto make_ui_stack_starts = [] (
        const Level3 &l3,
        const QVector<unsigned> &inputConnections,
        const QVector<QStringList> inputChoiceNameLists)
    {
        StackStart_UI ret;

        QStringList columnTitles = {
            "Name", "Input", "Stack#", "Activate",
        };

        auto table = new QTableWidget(l3.stackStart.size(), columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        ret.table = table;

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("StackStart%1").arg(row)));

            auto combo_connection = new QComboBox;
            auto check_activate = new QCheckBox;
            auto spin_stack = new QSpinBox;

            ret.combos_connection.push_back(combo_connection);
            ret.checks_activate.push_back(check_activate);
            ret.spins_stack.push_back(spin_stack);

            combo_connection->addItems(inputChoiceNameLists.value(row + ret.FirstUnitIndex));
            combo_connection->setCurrentIndex(inputConnections.value(row + ret.FirstUnitIndex));
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.stackStart[row].activate);
            spin_stack->setMinimum(1);
            spin_stack->setMaximum(7);
            spin_stack->setValue(l3.stackStart[row].stackIndex);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
            table->setCellWidget(row, ret.ColStack, spin_stack);
            table->setCellWidget(row, ret.ColActivate, make_centered(check_activate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        return ret;
    };

    auto make_ui_master_triggers = [] (
        const Level3 &l3,
        const QVector<unsigned> &inputConnections,
        const QVector<QStringList> inputChoiceNameLists)
    {
        MasterTriggers_UI ret;

        QStringList columnTitles = {
            "Name", "Input", "Activate",
        };

        auto table = new QTableWidget(l3.masterTrigger.size(), columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        ret.table = table;

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("MasterTrigger%1").arg(row)));

            auto combo_connection = new QComboBox;
            auto check_activate = new QCheckBox;

            ret.combos_connection.push_back(combo_connection);
            ret.checks_activate.push_back(check_activate);

            combo_connection->addItems(inputChoiceNameLists.value(row + ret.FirstUnitIndex));
            combo_connection->setCurrentIndex(inputConnections.value(row + ret.FirstUnitIndex));
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.masterTrigger[row].activate);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
            table->setCellWidget(row, ret.ColActivate, make_centered(check_activate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        return ret;
    };

    auto make_ui_counters = [] (
        const Level3 &l3,
        const QVector<unsigned> &inputConnections,
        const QVector<QStringList> inputChoiceNameLists)
    {
        Counters_UI ret;

        QStringList columnTitles = {
            "Name", "Input", "Activate",
        };

        auto table = new QTableWidget(l3.counters.size(), columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        ret.table = table;

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("Counter%1").arg(row)));

            auto combo_connection = new QComboBox;
            auto check_activate = new QCheckBox;

            ret.combos_connection.push_back(combo_connection);
            ret.checks_activate.push_back(check_activate);

            combo_connection->addItems(inputChoiceNameLists.value(row + ret.FirstUnitIndex));
            combo_connection->setCurrentIndex(inputConnections.value(row + ret.FirstUnitIndex));
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.masterTrigger[row].activate);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
            table->setCellWidget(row, ret.ColActivate, make_centered(check_activate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        return ret;
    };

    auto ui_stackStart = make_ui_stack_starts(l3, inputConnections, inputChoiceNameLists);
    auto ui_masterTriggers = make_ui_master_triggers(l3, inputConnections, inputChoiceNameLists);
    auto ui_counters = make_ui_counters(l3, inputConnections, inputChoiceNameLists);

    auto grid = new QGridLayout;
    grid->addWidget(make_groupbox(ui_stackStart.table, "Stack Start"), 0, 0);
    grid->addWidget(make_groupbox(ui_masterTriggers.table, "Master Triggers"), 0, 1);
    grid->addWidget(make_groupbox(ui_counters.table, "Counters"), 1, 0);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addLayout(grid);
    widgetLayout->addWidget(bb);
}

// TODO: add AND, OR, invert and [min, max] bits setup helpers
LUTOutputEditor::LUTOutputEditor(
    int outputNumber,
    const QVector<QStringList> &inputNameLists,
    const std::array<unsigned, 3> &dynamicInputValues,
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

            if (row < static_cast<int>(dynamicInputValues.size()))
                combo->setCurrentIndex(dynamicInputValues[row]);

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
    layout->addWidget(widget_inputSelect, 40);
    layout->addWidget(widget_outputActivation, 60);
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

LUT_DynConValues LUTOutputEditor::getDynamicConnectionValues() const
{
    LUT_DynConValues ret = {};

    for (size_t input = 0; input < ret.size(); input++)
        ret[input] = m_inputConnectionCombos[input]->currentIndex();

    return ret;
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const QVector<QStringList> &inputNameLists,
    const QStringList &outputNames,
    QWidget *parent)
    : LUTEditor(lutName, inputNameLists, {}, outputNames, {}, 0u, {}, {}, parent)
{
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const QVector<QStringList> &inputNameLists,
    const LUT_DynConValues &dynConValues,
    const QStringList &outputNames,
    const QStringList &strobeInputNames,
    unsigned strobeConValue,
    const trigger_io::IO &strobeSettings,
    const std::bitset<trigger_io::LUT::OutputBits> strobedOutputs,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lutName);

    auto editorLayout = make_hbox<0, 0>();

    std::array<QVBoxLayout *, trigger_io::LUT::OutputBits> editorGroupBoxLayouts;

    for (int output = 0; output < trigger_io::LUT::OutputBits; output++)
    {
        auto lutOutputEditor = new LUTOutputEditor(output, inputNameLists, dynConValues);

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

        editorGroupBoxLayouts[output] = gbl;

        // Propagate input connection changes from each editor to all editors.
        // This keeps the connection combo boxes in sync.
        connect(lutOutputEditor, &LUTOutputEditor::inputConnectionChanged,
                this, [this] (unsigned input, unsigned value) {
                    for (auto &editor: m_outputEditors)
                        editor->setInputConnection(input, value);
                });
    }

    if (!strobeInputNames.isEmpty())
    {
        for (int output = 0; output < trigger_io::LUT::OutputBits; output++)
        {
            auto cb_useStrobe = new QCheckBox;
            cb_useStrobe->setChecked(strobedOutputs.test(output));
            m_strobeCheckboxes.push_back(cb_useStrobe);

            auto useStrobeLayout = make_hbox();
            useStrobeLayout->addWidget(new QLabel("Strobe Output:"));
            useStrobeLayout->addWidget(cb_useStrobe);
            useStrobeLayout->addStretch(1);

            editorGroupBoxLayouts[output]->addLayout(useStrobeLayout);
        }
    }

    auto scrollWidget = new QWidget;
    auto scrollLayout = make_layout<QVBoxLayout>(scrollWidget);
    scrollLayout->addLayout(editorLayout, 10);


    if (!strobeInputNames.isEmpty())
    {
        QStringList columnTitles = {
            "Input", "Delay", "Width", "Holdoff", "Invert"
        };

        auto &ui = m_strobeTableUi;

        auto table = new QTableWidget(1, columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        table->verticalHeader()->hide();

        auto check_invert = new QCheckBox;
        auto combo_connection = new QComboBox;

        ui.table = table;
        ui.check_invert = check_invert;
        ui.combo_connection = combo_connection;

        check_invert->setChecked(strobeSettings.invert);
        combo_connection->addItems(strobeInputNames);
        combo_connection->setCurrentIndex(strobeConValue);
        combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        table->setCellWidget(0, ui.ColConnection, combo_connection);
        table->setItem(0, ui.ColDelay, new QTableWidgetItem(QString::number(strobeSettings.delay)));
        table->setItem(0, ui.ColWidth, new QTableWidgetItem(QString::number(strobeSettings.width)));
        table->setItem(0, ui.ColHoldoff, new QTableWidgetItem(QString::number(strobeSettings.holdoff)));
        table->setCellWidget(0, ui.ColInvert, make_centered(check_invert));

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto gb_strobe = new QGroupBox("Strobe Gate Generator Settings");
        auto l_strobe = make_hbox<0, 0>(gb_strobe);
        l_strobe->addWidget(table, 1);
        l_strobe->addStretch(1);

        scrollLayout->addWidget(gb_strobe, 2);
    }

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    scrollLayout->addWidget(bb, 0);

    auto scrollArea = new QScrollArea;
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    auto widgetLayout = make_hbox<0, 0>(this);
    widgetLayout->addWidget(scrollArea);
}

QStringList LUTEditor::getOutputNames() const
{
    QStringList ret;

    for (auto &le: m_outputNameEdits)
        ret.push_back(le->text());

    return ret;
}

LUT_DynConValues LUTEditor::getDynamicConnectionValues()
{
    // The editors are synchronized internally and should all yield the same
    // values.
    for (auto editor: m_outputEditors)
    {
        assert(m_outputEditors[0]->getDynamicConnectionValues()
               == editor->getDynamicConnectionValues());
    }

    return m_outputEditors[0]->getDynamicConnectionValues();
}

unsigned LUTEditor::getStrobeConnectionValue()
{
    return static_cast<unsigned>(m_strobeTableUi.combo_connection->currentIndex());
}

trigger_io::IO LUTEditor::getStrobeSettings()
{
    auto &ui = m_strobeTableUi;

    trigger_io::IO ret = {};

    ret.delay = ui.table->item(0, ui.ColDelay)->data(Qt::DisplayRole).toUInt();
    ret.width = ui.table->item(0, ui.ColWidth)->data(Qt::DisplayRole).toUInt();
    ret.holdoff = ui.table->item(0, ui.ColHoldoff)->data(Qt::DisplayRole).toUInt();
    ret.invert = ui.check_invert->isChecked();

    return ret;
}

std::bitset<trigger_io::LUT::OutputBits> LUTEditor::getStrobedOutputMask()
{
    std::bitset<trigger_io::LUT::OutputBits> ret = {};

    for (size_t out = 0; out < ret.size(); out++)
    {
        if (m_strobeCheckboxes[out]->isChecked())
            ret.set(out);
    }

    return ret;
}

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config
