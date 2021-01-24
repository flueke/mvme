#include <QStandardItemModel>
#include "mvlc/trigger_io_sim_ui.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using Item = QStandardItem;
using ItemList = QList<Item *>;

namespace
{

Item *make_item(const QString &name = {})
{
        auto item = new Item(name);
        item->setEditable(false);
        item->setDragEnabled(false);
        item->setDropEnabled(false);
        return item;
}

Item *make_trace_item (const QString &name = {})
{
    auto item = make_item(name);
    item->setDragEnabled(true);
    return item;
};

ItemList make_trace_row(const QString &name, const PinAddress &pin)
{
    auto unitItem = make_trace_item(name);
    auto nameItem = make_trace_item();

    unitItem->setData(QVariant::fromValue(pin));
    nameItem->setData(QVariant::fromValue(pin));

    //QVariantList data = { pin.unit[0], pin.unit[1], pin.unit[2], static_cast<int>(pin.pos) };
    //unitItem->setData(data);
    //nameItem->setData(data);

    return { unitItem, nameItem };
};

}

std::unique_ptr<TraceTreeModel> make_trace_tree_model()
{
    auto make_lut_item = []
        (const QString &name, UnitAddress unit, bool hasStrobe) -> Item *
    {
        auto lutRoot = make_item(name);

        for (auto i=0; i<LUT::InputBits; ++i)
        {
            unit[2] = i;
            lutRoot->appendRow(make_trace_row(
                    QString("in%1").arg(i), { unit, PinPosition::Input }));
        }

        if (hasStrobe)
        {
            unit[2] = LUT::StrobeGGInput;
            lutRoot->appendRow(make_trace_row(
                    "strobeIn", { unit, PinPosition::Input }));
        }

        for (auto i=0; i<LUT::OutputBits; ++i)
        {
            unit[2] = i;
            lutRoot->appendRow(make_trace_row(
                    QString("out%1").arg(i), { unit, PinPosition::Output }));

        }

        if (hasStrobe)
        {
            unit[2] = LUT::StrobeGGOutput;
            lutRoot->appendRow(make_trace_row(
                    "strobeOut", { unit, PinPosition::Output }));
        }

        return lutRoot;
    };

    auto model = std::make_unique<TraceTreeModel>();
    auto root = model->invisibleRootItem();

    // Sampled Traces
    auto samplesRoot = make_item("Sampled");
    root->appendRow({ samplesRoot, make_item() });

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::NIM_IO_Offset, 0 };
        samplesRoot->appendRow(make_trace_row(
                QString("NIM%1").arg(i), { unit, PinPosition::Input }));
    }

    // L0
    auto l0Root = make_item("L0");
    root->appendRow(l0Root);

    for (auto i=0u; i<TimerCount; ++i)
    {
        UnitAddress unit = { 0, i, 0 };
        l0Root->appendRow(make_trace_row(
                QString("timer%1").arg(i), { unit, PinPosition::Output }));
    }

    auto sysclockItem = make_trace_item("sysclock");
    l0Root->appendRow(sysclockItem);

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        auto item = make_trace_item(QString("NIM%1").arg(i));
        l0Root->appendRow(item);
    }

    // L1
    auto l1Root = make_item("L1");
    root->appendRow(l1Root);

    for (auto i=0u; i<Level1::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item(QString("LUT%1").arg(i), { 1, i, 0 }, false);
        l1Root->appendRow(lutRoot);
    }

    // L2
    auto l2Root = make_item("L2");
    root->appendRow(l2Root);

    for (auto i=0u; i<Level2::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item(QString("LUT%1").arg(i), { 2, i, 0 }, true);
        l2Root->appendRow(lutRoot);
    }

    // L3
    auto l3Root = make_item("L3");
    root->appendRow(l3Root);

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        auto item = make_trace_item(QString("NIM%1").arg(i));
        l3Root->appendRow(item);
    }

    for (auto i=0u; i<ECL_OUT_Count; ++i)
    {
        auto item = make_trace_item(QString("LVDS%1").arg(i));
        l3Root->appendRow(item);
    }

    // Finalize
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");

    return model;
}

std::unique_ptr<TraceTableModel> make_trace_table_model()
{
    auto model = std::make_unique<TraceTableModel>();
    model->setColumnCount(2);
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");

    /*
    auto root = model->invisibleRootItem();
    root->appendRow({ make_trace_item("NIM0"), make_trace_item("NIM0_name") });
    root->appendRow({ make_trace_item("NIM1"), make_trace_item("NIM1_name") });
    root->appendRow({ make_trace_item("NIM2"), make_trace_item("NIM2_name") });
    root->appendRow({ make_trace_item("NIM3"), make_trace_item("NIM3_name") });
    root->appendRow({ make_trace_item("NIM4"), make_trace_item("NIM4_name") });
    root->appendRow({ make_trace_item("NIM5"), make_trace_item("NIM5_name") });
    */

    return model;
}

#if 0
struct TraceSelectWidget::Private
{
};


TraceSelectWidget::TraceSelectWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
}

TraceSelectWidget::~TraceSelectWidget()
{
}
#endif

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

QDataStream &operator<<(QDataStream &out,
                        const mesytec::mvme_mvlc::trigger_io::PinAddress &pin)
{
    for (unsigned val: pin.unit)
        out << val;
    out << static_cast<unsigned>(pin.pos);
    return out;
}

QDataStream &operator>>(QDataStream &in,
                        mesytec::mvme_mvlc::trigger_io::PinAddress &pin)
{
    for (size_t i=0; i<pin.unit.size(); ++i)
        in >> pin.unit[i];
    unsigned pos;
    in >> pos;
    pin.pos = static_cast<mesytec::mvme_mvlc::trigger_io::PinPosition>(pos);
    return in;
}

QDebug operator<<(QDebug dbg, const mesytec::mvme_mvlc::trigger_io::PinAddress &pin)
{
    using namespace mesytec::mvme_mvlc::trigger_io;

    dbg.nospace() << "PinAddress("
        << "ua[0]=" << pin.unit[0]
        << ", ua[1]=" << pin.unit[1]
        << ", ua[2]=" << pin.unit[2]
        << ", pos=" << (pin.pos == PinPosition::Input ? "in" : "out")
        << ")";
    return dbg.maybeSpace();
}
