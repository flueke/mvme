#include "mvlc/mvlc_trigger_io_editor_p.h"

#include <cassert>
#include <cmath>
#include <QBoxLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHeaderView>
#include <QWheelEvent>

#include <boost/range/adaptor/indexed.hpp>
#include <minbool.h>
#include <QDebug>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>

#include "qt_util.h"

using boost::adaptors::indexed;

namespace
{

void reverse_rows(QTableWidget *table)
{
    for (int row = 0; row < table->rowCount() / 2; row++)
    {
        auto vView = table->verticalHeader();
        vView->swapSections(row, table->rowCount() - 1 - row);
    }
}

}

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
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

TriggerIOView::TriggerIOView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setRenderHints(
        QPainter::Antialiasing
        | QPainter::TextAntialiasing
        | QPainter::SmoothPixmapTransform
        | QPainter::HighQualityAntialiasing
        );
}

void TriggerIOView::scaleView(qreal scaleFactor)
{
    double zoomOutLimit = 0.25;
    double zoomInLimit = 10;

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

namespace gfx
{

ConnectorBase::~ConnectorBase() { }

static const QBrush Block_Brush("#fffbcc");
static const QBrush Block_Brush_Hover("#eeee77");

static const QBrush Connector_Brush(Qt::blue);
static const QBrush Connector_Brush_Hover("#5555ff");
static const QBrush Connector_Brush_Disabled(Qt::lightGray);

//
// ConnectorCircleItem
//
ConnectorCircleItem::ConnectorCircleItem(QGraphicsItem *parent)
    : ConnectorCircleItem({}, parent)
{}

ConnectorCircleItem::ConnectorCircleItem(const QString &label, QGraphicsItem *parent)
    : QGraphicsEllipseItem(0, 0, 2*ConnectorRadius, 2*ConnectorRadius, parent)
{
    setPen(Qt::NoPen);
    setBrush(Qt::blue);
    setLabel(label);
}

void ConnectorCircleItem::labelSet_(const QString &label)
{
    if (!m_label)
    {
        m_label = new QGraphicsSimpleTextItem(this);
        auto font = m_label->font();
        font.setPixelSize(LabelPixelSize);
        m_label->setFont(font);
    }

    m_label->setText(label);
    adjust();
}

void ConnectorCircleItem::alignmentSet_(const Qt::Alignment &align)
{
    adjust();
}

void ConnectorCircleItem::enabledSet_(bool b)
{
}

// TODO: support top and bottom alignment
void ConnectorCircleItem::adjust()
{
    if (!m_label) return;

    m_label->setPos(0, 0);

    switch (getLabelAlignment())
    {
        case Qt::AlignLeft:
            m_label->moveBy(-(m_label->boundingRect().width() + LabelOffset),
                            -1.5); // magic number
            break;

        case Qt::AlignRight:
            m_label->moveBy(2*ConnectorRadius + LabelOffset,
                            -1.5); // magic number
            break;
    }
}

//
// ConnectorDiamondItem
//
ConnectorDiamondItem::ConnectorDiamondItem(int baseLength, QGraphicsItem *parent)
    : QAbstractGraphicsShapeItem(parent)
    , m_baseLength(baseLength)
{
    setPen(Qt::NoPen);
    setBrush(Qt::blue);
    //setRotation(45.0);
}

ConnectorDiamondItem::ConnectorDiamondItem(QGraphicsItem *parent)
    : ConnectorDiamondItem(SideLength, parent)
{ }

QRectF ConnectorDiamondItem::boundingRect() const
{
    return QRectF(0, 0, m_baseLength, m_baseLength);
}

void ConnectorDiamondItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
           QWidget *widget)
{
    auto br = boundingRect();

    auto poly = QPolygonF()
        << QPointF{ br.width() * 0.5, 0 }
        << QPointF{ br.width(), br.height() * 0.5 }
        << QPointF{ br.width() * 0.5, br.height() }
        << QPointF{ 0, br.height() * 0.5}
        ;

    painter->setPen(pen());
    painter->setBrush(brush());
    painter->drawPolygon(poly);
}

void ConnectorDiamondItem::labelSet_(const QString &label)
{
    if (!m_label)
    {
        m_label = new QGraphicsSimpleTextItem(this);
        auto font = m_label->font();
        font.setPixelSize(LabelPixelSize);
        m_label->setFont(font);
    }

    m_label->setText(label);
    adjust();
}

void ConnectorDiamondItem::alignmentSet_(const Qt::Alignment &align)
{
    adjust();
}

void ConnectorDiamondItem::enabledSet_(bool b)
{
}

// TODO: support top and bottom alignment
void ConnectorDiamondItem::adjust()
{
    if (!m_label) return;
    m_label->setPos(0, 0);

    switch (getLabelAlignment())
    {
        case Qt::AlignLeft:
            m_label->moveBy(-(m_label->boundingRect().width() + LabelOffset),
                            -1.5); // magic number
            break;

        case Qt::AlignRight:
            m_label->moveBy(boundingRect().width() + LabelOffset,
                            0);
                            //-boundingRect().height() * 0.5);
            break;
    }
}

//
// BlockItem
//
BlockItem::BlockItem(
    int width, int height,
    int inputCount, int outputCount,
    int inputConnectorMargin,
    int outputConnectorMargin,
    QGraphicsItem *parent)
    : BlockItem(
        width, height,
        inputCount, outputCount,
        inputConnectorMargin, inputConnectorMargin,
        outputConnectorMargin, outputConnectorMargin,
        parent)
{ }

BlockItem::BlockItem(
    int width, int height,
    int inputCount, int outputCount,
    int inConMarginTop, int inConMarginBottom,
    int outConMarginTop, int outConMarginBottom,
    QGraphicsItem *parent)
    : QGraphicsRectItem(0, 0, width, height, parent)
    , m_inConMargins(std::make_pair(inConMarginTop, inConMarginBottom))
    , m_outConMargins(std::make_pair(outConMarginTop, outConMarginBottom))
{
    setAcceptHoverEvents(true);
    setBrush(Block_Brush);

    // input connectors
    {
        const int conTotalHeight = height - (inConMarginTop + inConMarginBottom);
        const int conSpacing = conTotalHeight / (inputCount - 1);

        //qDebug() << __PRETTY_FUNCTION__ << "input: conTotalHeight =" << conTotalHeight
        //    << ", conSpacing =" << conSpacing;

        int y = inConMarginTop;

        for (int input = inputCount-1; input >= 0; input--)
        {
            auto circle = new ConnectorCircleItem(QString::number(input), this);
            circle->setLabelAlignment(Qt::AlignRight);
            circle->moveBy(-circle->boundingRect().width() * 0.5,
                           -circle->boundingRect().height() * 0.5);

            circle->moveBy(0, y);
            //qDebug() << "  input:" << input << ", dy =" << y << ", y =" << circle->pos().y();
            y += conSpacing;
            m_inputConnectors.push_back(circle);
        }

        std::reverse(m_inputConnectors.begin(), m_inputConnectors.end());
    }

    // output connectors
    {
        const int conTotalHeight = height - (outConMarginTop + outConMarginBottom);
        const int conSpacing = conTotalHeight / (outputCount - 1);

        //qDebug() << __PRETTY_FUNCTION__ << "output: conTotalHeight =" << conTotalHeight
        //    << ", conSpacing =" << conSpacing;

        int y = outConMarginTop;

        for (int output = outputCount-1; output >= 0; output--)
        {
            auto circle = new ConnectorCircleItem(QString::number(output), this);
            circle->setLabelAlignment(Qt::AlignLeft);
            circle->moveBy(width - circle->boundingRect().width() * 0.5,
                           -circle->boundingRect().height() * 0.5);

            circle->moveBy(0, y);
            //qDebug() << "  output:" << input << ", dy =" << y << ", y =" << circle->pos().y();
            y += conSpacing;
            m_outputConnectors.push_back(circle);
        }

        std::reverse(m_outputConnectors.begin(), m_outputConnectors.end());
    }
}

void BlockItem::hoverEnterEvent(QGraphicsSceneHoverEvent *ev)
{
    setBrush(Block_Brush_Hover);
    for (auto con: m_inputConnectors)
        con->setBrush(Connector_Brush_Hover);
    for (auto con: m_outputConnectors)
        con->setBrush(Connector_Brush_Hover);
    QGraphicsRectItem::update();
}

void BlockItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *ev)
{
    setBrush(Block_Brush);
    for (auto con: m_inputConnectors)
        con->setBrush(Connector_Brush);
    for (auto con: m_outputConnectors)
        con->setBrush(Connector_Brush);
    QGraphicsRectItem::update();
}


//
// LUTItem
//
LUTItem::LUTItem(int lutIdx, bool hasStrobeGG, QGraphicsItem *parent)
    : BlockItem(
        Width, Height,
        Inputs, Outputs,
        hasStrobeGG ? WithStrobeInputConnectorMarginTop : InputConnectorMargin,
        hasStrobeGG ? WithStrobeInputConnectorMarginBottom : InputConnectorMargin,
        OutputConnectorMargin, OutputConnectorMargin,
        parent)
{
    auto label = new QGraphicsSimpleTextItem(QString("LUT%1").arg(lutIdx), this);
    label->moveBy((this->boundingRect().width()
                   - label->boundingRect().width()) / 2.0, 0);

    if (hasStrobeGG)
    {
        auto con = new ConnectorDiamondItem(this);

        int dY = this->boundingRect().height()
            - WithStrobeInputConnectorMarginBottom * 0.5;

        con->moveBy(0, dY);
        con->moveBy(-con->boundingRect().width() * 0.5,
                    -con->boundingRect().height() * 0.5);

        con->setLabelAlignment(Qt::AlignRight);
        con->setLabel("strobe GG");

        addInputConnector(con);
    }
}

template<typename T>
QPointF get_center_point(T *item)
{
    return item->boundingRect().center();
}

//
// Edge
//
Edge::Edge(QGraphicsItem *sourceItem, QGraphicsItem *destItem)
    : m_source(sourceItem)
    , m_dest(destItem)
    , m_arrowSize(8)
{
    setAcceptedMouseButtons(0);
    adjust();
}

void Edge::setSourceItem(QGraphicsItem *item)
{
    m_source = item;
    adjust();
}

void Edge::setDestItem(QGraphicsItem *item)
{
    m_dest = item;
    adjust();
}

void Edge::adjust()
{
    if (!m_source || !m_dest)
    {
        hide();
        return;
    }

    QLineF line(mapFromItem(m_source, get_center_point(m_source)),
                mapFromItem(m_dest, get_center_point(m_dest)));

    qreal length = line.length();

    prepareGeometryChange();

    if (length > qreal(20.)) {
        // Shortens the line to be drawn by 'offset' pixels at the start and
        // end.
        qreal offset = 4;
        qreal offsetPercent = (length - offset) / length;
        m_sourcePoint = line.pointAt(1.0 - offsetPercent);
        m_destPoint = line.pointAt(offsetPercent);
    } else {
        m_sourcePoint = m_destPoint = line.p1();
    }

    show();
}

QRectF Edge::boundingRect() const
{
    if (!m_source || !m_dest)
        return QRectF();

    qreal penWidth = 1;
    qreal extra = (penWidth + m_arrowSize) / 2.0;

    return QRectF(m_sourcePoint, QSizeF(m_destPoint.x() - m_sourcePoint.x(),
                                        m_destPoint.y() - m_sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void Edge::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
           QWidget *widget)
{
    if (!m_source || !m_dest)
        return;

    QLineF line(m_sourcePoint, m_destPoint);
    if (qFuzzyCompare(line.length(), qreal(0.)))
        return;

    // Draw the line itself
    painter->setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawLine(line);

    // Draw the arrows
    double angle = std::atan2(-line.dy(), line.dx());

    //QPointF sourceArrowP1 = m_sourcePoint + QPointF(sin(angle + M_PI / 3) * m_arrowSize,
    //                                                cos(angle + M_PI / 3) * m_arrowSize);
    //QPointF sourceArrowP2 = m_sourcePoint + QPointF(sin(angle + M_PI - M_PI / 3) * m_arrowSize,
    //                                                cos(angle + M_PI - M_PI / 3) * m_arrowSize);
    QPointF destArrowP1 = m_destPoint + QPointF(sin(angle - M_PI / 3) * m_arrowSize,
                                                cos(angle - M_PI / 3) * m_arrowSize);
    QPointF destArrowP2 = m_destPoint + QPointF(sin(angle - M_PI + M_PI / 3) * m_arrowSize,
                                                cos(angle - M_PI + M_PI / 3) * m_arrowSize);

    painter->setBrush(Qt::black);
    //painter->drawPolygon(QPolygonF() << line.p1() << sourceArrowP1 << sourceArrowP2);
    painter->drawPolygon(QPolygonF() << line.p2() << destArrowP1 << destArrowP2);
}

} // end namespace gfx

TriggerIOGraphicsScene::TriggerIOGraphicsScene(
    const TriggerIO &ioCfg,
    QObject *parent)
: QGraphicsScene(parent)
, m_ioCfg(ioCfg)
{
    auto make_level0_nim_items = [&] () -> Level0NIMItems
    {
        Level0NIMItems result = {};

        result.parent = new QGraphicsRectItem(
            0, 0,
            150, 520);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM+ECL IO Item
        {
            result.nimItem = new gfx::BlockItem(
                100, 470,
                0, trigger_io::NIM_IO_Count,
                0, 48,
                result.parent);

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

        // background box containing the 5 LUTs
        result.parent = new QGraphicsRectItem(
            0, 0,
            260, 520);

        qDebug() << __PRETTY_FUNCTION__ << "level1 parent rect: " << result.parent->rect();
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = new gfx::LUTItem(lutIdx, false, result.parent);
            result.luts[lutIdx] = lutItem;
        }

        QRectF lutRect = result.luts[0]->rect();
        qDebug() << __PRETTY_FUNCTION__ << "lutRect =" << lutRect;

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
            result.utilsItem = new gfx::BlockItem(
                result.parent->boundingRect().width() - 2 * 25,
                result.parent->boundingRect().height() - 2 * 25,
                0, trigger_io::Level0::UtilityUnitCount,
                0, 16,
                result.parent);
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
            auto lutItem = new gfx::LUTItem(lutIdx, true, result.parent);
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
            result.nimItem = new gfx::BlockItem(
                100, 470,
                trigger_io::NIM_IO_Count, 0,
                48, 0,
                result.parent);

            result.nimItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Outputs"), result.nimItem);
            label->moveBy((result.nimItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        auto yOffset = result.nimItem->boundingRect().height() + 25;

        // ECL Out
        {
            result.eclItem = new gfx::BlockItem(
                100, 140,
                trigger_io::ECL_OUT_Count, 0,
                48, 0,
                result.parent);
            result.eclItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("ECL Outputs"), result.eclItem);
            label->moveBy((result.eclItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            result.eclItem->moveBy(0, yOffset);
        }

        // Utils
        {
            result.utilsItem = new gfx::BlockItem(
                100, 140,
                trigger_io::Level3::UtilityUnitCount, 0,
                16, 0,
                result.parent);
            result.utilsItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(
                QString("L3 Utilities\nStack Start\nMaster trig"), result.utilsItem);
            label->moveBy((result.utilsItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            result.utilsItem->moveBy(0, yOffset + result.eclItem->boundingRect().height() + 25);
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
    m_level0NIMItems = make_level0_nim_items();
    m_level0NIMItems.parent->moveBy(-160, 0);
    m_level1Items = make_level1_items();
    m_level2Items = make_level2_items();
    m_level2Items.parent->moveBy(300, 0);
    m_level3Items = make_level3_items();
    m_level3Items.parent->moveBy(600, 0);

    m_level0UtilItems = make_level0_util_items();
    m_level0UtilItems.parent->moveBy(
        300, m_level0NIMItems.parent->boundingRect().height() + 15);

    this->addItem(m_level0NIMItems.parent);
    this->addItem(m_level1Items.parent);
    this->addItem(m_level2Items.parent);
    this->addItem(m_level3Items.parent);
    this->addItem(m_level0UtilItems.parent);

    // Create all connection edges contained in the trigger io config. The
    // logic in setTriggerIOConfig then decides if edges should be hidden or
    // drawn in a different way.

    // static level 1 connections
    for (const auto &lutkv: Level1::StaticConnections | indexed(0))
    {
        unsigned lutIndex = lutkv.index();
        auto &lutConnections = lutkv.value();

        for (const auto &conkv: lutConnections | indexed(0))
        {
            unsigned inputIndex = conkv.index();
            UnitConnection con = conkv.value();

            auto sourceConnector = getOutputConnector(con.address);
            auto destConnector = getInputConnector({1, lutIndex, inputIndex});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }
    }

    // static level 2 connections
    for (const auto &lutkv: Level2::StaticConnections | indexed(0))
    {
        unsigned lutIndex = lutkv.index();
        auto &lutConnections = lutkv.value();

        for (const auto &conkv: lutConnections | indexed(0))
        {
            unsigned inputIndex = conkv.index();
            UnitConnection con = conkv.value();

            if (con.isDynamic)
                continue;

            auto sourceConnector = getOutputConnector(con.address);
            auto destConnector = getInputConnector({2, lutIndex, inputIndex});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }
    }

    // level2 dynamic connections
    for (const auto &lutkv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = lutkv.index();
        const auto &l2InputChoices = Level2::DynamicInputChoices[unitIndex];

        for (unsigned input = 0; input < Level2::LUT_DynamicInputCount; input++)
        {
            unsigned conValue = ioCfg.l2.lutConnections[unitIndex][input];
            UnitAddress conAddress = l2InputChoices.lutChoices[input][conValue];

            auto sourceConnector = getOutputConnector(conAddress);
            auto destConnector = getInputConnector({2, unitIndex, input});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }

        // strobe
        {
            assert(getInputConnector({2, unitIndex, gfx::LUTItem::StrobeConnectorIndex}));
            unsigned conValue = ioCfg.l2.strobeConnections[unitIndex];
            UnitAddress conAddress = l2InputChoices.strobeChoices[conValue];

            auto sourceConnector = getOutputConnector(conAddress);
            auto destConnector = getInputConnector(
                {2, unitIndex, gfx::LUTItem::StrobeConnectorIndex});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }
    }

    // level3 dynamic connections
    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        unsigned unitIndex = kv.index();

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.MasterTriggersOffset;

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.CountersOffset;

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    // Level3 NIM connections
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.ECL_Unit_Offset;

        unsigned conValue = ioCfg.l3.connections[unitIndex];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }
};

QAbstractGraphicsShapeItem *
    TriggerIOGraphicsScene::getInputConnector(const UnitAddress &addr) const
{
    switch (addr[0])
    {
        case 0:
            return nullptr;
        case 1:
            return m_level1Items.luts[addr[1]]->inputConnectors().value(addr[2]);
        case 2:
            return m_level2Items.luts[addr[1]]->inputConnectors().value(addr[2]);
        case 3:
            if (trigger_io::Level3::NIM_IO_Unit_Offset <= addr[1]
                && addr[1] < trigger_io::Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                return m_level3Items.nimItem->inputConnectors().value(
                    addr[1] - trigger_io::Level3::NIM_IO_Unit_Offset);
            }
            else if (trigger_io::Level3::ECL_Unit_Offset <= addr[1]
                     && addr[1] < trigger_io::Level3::ECL_Unit_Offset + trigger_io::ECL_OUT_Count)
            {
                return m_level3Items.eclItem->inputConnectors().value(
                    addr[1] - trigger_io::Level3::ECL_Unit_Offset);
            }
            else
            {
                return m_level3Items.utilsItem->inputConnectors().value(addr[1]);
            }
            break;
    }

    return nullptr;
}

QAbstractGraphicsShapeItem *
    TriggerIOGraphicsScene::getOutputConnector(const UnitAddress &addr) const
{
    switch (addr[0])
    {
        case 0:
            if (trigger_io::Level0::NIM_IO_Offset <= addr[1]
                && addr[1] < trigger_io::Level0::NIM_IO_Offset + trigger_io::NIM_IO_Count)
            {
                return m_level0NIMItems.nimItem->outputConnectors().value(
                    addr[1] - trigger_io::Level0::NIM_IO_Offset);
            }
            else
            {
                return m_level0UtilItems.utilsItem->outputConnectors().value(addr[1]);
            }
            break;
        case 1:
            return m_level1Items.luts[addr[1]]->outputConnectors().value(addr[2]);
        case 2:
            return m_level2Items.luts[addr[1]]->outputConnectors().value(addr[2]);
        case 3:
            return nullptr;
    }

    return nullptr;
}

gfx::Edge *TriggerIOGraphicsScene::addEdge(
    QAbstractGraphicsShapeItem *sourceConnector,
    QAbstractGraphicsShapeItem *destConnector)
{
    auto edge = new gfx::Edge(sourceConnector, destConnector);
    m_edges.push_back(edge);
    assert(!m_edgesByDest.contains(destConnector));
    m_edgesByDest.insert(destConnector, edge);
    m_edgesBySource.insertMulti(sourceConnector, edge);

    edge->adjust();
    this->addItem(edge);
    return edge;
}

QList<gfx::Edge *> TriggerIOGraphicsScene::getEdgesBySourceConnector(
    QAbstractGraphicsShapeItem *sourceConnector) const
{
    return m_edgesBySource.values(sourceConnector);
}

QList<gfx::Edge *> TriggerIOGraphicsScene::getEdgesByDestConnector(
    QAbstractGraphicsShapeItem *sourceConnector) const
{
    return m_edgesByDest.values(sourceConnector);
}

void TriggerIOGraphicsScene::setTriggerIOConfig(const TriggerIO &ioCfg)
{
    using namespace gfx;

    m_ioCfg = ioCfg;

    // level0 NIM IO
    for (const auto &kv: ioCfg.l0.ioNIM | indexed(Level0::NIM_IO_Offset))
    {
        const auto &io = kv.value();
        const bool isInput = io.direction == IO::Direction::in;

        UnitAddress addr {0, static_cast<unsigned>(kv.index())};

        auto con = getOutputConnector(addr);
        con->setBrush(isInput ? Connector_Brush : Connector_Brush_Disabled);

        for (auto edge: getEdgesBySourceConnector(con))
        {
            edge->setVisible(io.direction == IO::Direction::in);
        }
    }
}

void TriggerIOGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev)
{
    auto items = this->items(ev->scenePos());

    // level0
    if (items.indexOf(m_level0NIMItems.nimItem) >= 0)
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

    if (dir == trigger_io::IO::Direction::in)
    {
        // Hide the 'activate' column here. The checkboxes will still be there and
        // populated and the result will contain the correct activation flags so
        // that we don't mess with level 3 settings when synchronizing both levels.
        table->horizontalHeader()->hideSection(NIM_IO_Table_UI::ColActivate);
    }

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
    setWindowTitle("NIM Input/Output Settings");

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

        // NIM inputs do not work if 'activate' is set to 1.
        if (nim.direction == trigger_io::IO::Direction::in)
            nim.activate = false;
        else
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
    setWindowTitle("ECL Output Settings");

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
Level0UtilsDialog::Level0UtilsDialog(
            const Level0 &l0,
            QWidget *parent)
    : QDialog(parent)
    , m_l0(l0)
{
    setWindowTitle("Level0 Utility Settings");

    auto make_timers_table_ui = [](const Level0 &l0)
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
                    l0.unitNames.value(row)));

            ret.table->setCellWidget(row, ret.ColRange, combo_range);

            ret.table->setItem(row, ret.ColPeriod, new QTableWidgetItem(
                    QString::number(l0.timers[row].period)));

            ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(
                    QString::number(l0.timers[row].delay_ns)));


            ret.combos_range.push_back(combo_range);
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    auto make_irq_units_table_ui = [](const Level0 &l0)
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
                    l0.unitNames.value(row + nameOffset)));

            ret.table->setCellWidget(row, ret.ColIRQIndex, spin_irqIndex);

            ret.spins_irqIndex.push_back(spin_irqIndex);
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    auto make_soft_triggers_table_ui = [](const Level0 &l0)
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
                    l0.unitNames.value(row + nameOffset)));
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    auto make_slave_triggers_table_ui = [](const Level0 &l0)
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
                    l0.unitNames.value(row + nameOffset)));

            ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(QString::number(io.delay)));
            ret.table->setItem(row, ret.ColWidth, new QTableWidgetItem(QString::number(io.width)));
            ret.table->setItem(row, ret.ColHoldoff, new QTableWidgetItem(QString::number(io.holdoff)));
            ret.table->setCellWidget(row, ret.ColInvert, make_centered(check_invert));

            ret.checks_invert.push_back(check_invert);
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    auto make_stack_busy_table_ui = [](const Level0 &l0)
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
                    l0.unitNames.value(row + nameOffset)));

            ret.table->setCellWidget(row, ret.ColStackIndex, spin_stack);

            ret.spins_stackIndex.push_back(spin_stack);
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    ui_timers = make_timers_table_ui(l0);
    ui_irqUnits = make_irq_units_table_ui(l0);
    ui_softTriggers = make_soft_triggers_table_ui(l0);
    ui_slaveTriggers = make_slave_triggers_table_ui(l0);
    ui_stackBusy = make_stack_busy_table_ui(l0);

    auto grid = new QGridLayout;
    grid->addWidget(make_groupbox(ui_timers.table, "Timers"), 0, 0);
    grid->addWidget(make_groupbox(ui_irqUnits.table, "IRQ Units"), 0, 1);
    grid->addWidget(make_groupbox(ui_softTriggers.table, "SoftTriggers"), 0, 2);
    grid->addWidget(make_groupbox(ui_slaveTriggers.table, "SlaveTriggers"), 1, 0);
    grid->addWidget(make_groupbox(ui_stackBusy.table, "StackBusy"), 1, 1);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addLayout(grid);
    widgetLayout->addWidget(bb);
}

Level0 Level0UtilsDialog::getSettings() const
{
    {
        auto &ui = ui_timers;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &unit = m_l0.timers[row];
            unit.range = static_cast<trigger_io::Timer::Range>(ui.combos_range[row]->currentIndex());
            unit.period = ui.table->item(row, ui.ColPeriod)->text().toUInt();
            unit.delay_ns = ui.table->item(row, ui.ColDelay)->text().toUInt();
        }
    }

    {
        auto &ui = ui_irqUnits;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &unit = m_l0.irqUnits[row];
            unit.irqIndex = ui.spins_irqIndex[row]->value() - 1;
        }
    }

    {
        auto &ui = ui_softTriggers;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
        }
    }

    {
        auto &ui = ui_slaveTriggers;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &unit = m_l0.slaveTriggers[row];

            unit.delay = ui.table->item(row, ui.ColDelay)->text().toUInt();
            unit.width = ui.table->item(row, ui.ColWidth)->text().toUInt();
            unit.holdoff = ui.table->item(row, ui.ColHoldoff)->text().toUInt();
            unit.invert = ui.checks_invert[row]->isChecked();
        }
    }

    {
        auto &ui = ui_stackBusy;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &unit = m_l0.stackBusy[row];
            unit.stackIndex = ui.spins_stackIndex[row]->value();
        }
    }
    return m_l0;
}

//
// Level3UtilsDialog
//
Level3UtilsDialog::Level3UtilsDialog(
    const Level3 &l3,
    const QVector<QStringList> &inputChoiceNameLists,
    QWidget *parent)
    : QDialog(parent)
    , m_l3(l3)
{
    setWindowTitle("Level3 Utility Settings");

    auto make_ui_stack_starts = [] (
        const Level3 &l3,
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
            combo_connection->setCurrentIndex(l3.connections[row + ret.FirstUnitIndex]);
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
        const QVector<QStringList> inputChoiceNameLists)
    {
        MasterTriggers_UI ret;

        QStringList columnTitles = {
            "Name", "Input", "Activate",
        };

        auto table = new QTableWidget(l3.masterTriggers.size(), columnTitles.size());
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
            combo_connection->setCurrentIndex(l3.connections[row + ret.FirstUnitIndex]);
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.masterTriggers[row].activate);

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
        const QVector<QStringList> inputChoiceNameLists)
    {
        Counters_UI ret;

        QStringList columnTitles = {
            "Name", "Input",
        };

        auto table = new QTableWidget(l3.counters.size(), columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        ret.table = table;

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(
                    QString("Counter%1").arg(row)));

            auto combo_connection = new QComboBox;

            ret.combos_connection.push_back(combo_connection);

            combo_connection->addItems(inputChoiceNameLists.value(row + ret.FirstUnitIndex));
            combo_connection->setCurrentIndex(l3.connections[row + ret.FirstUnitIndex]);
            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        return ret;
    };

    ui_stackStart = make_ui_stack_starts(l3, inputChoiceNameLists);
    ui_masterTriggers = make_ui_master_triggers(l3, inputChoiceNameLists);
    ui_counters = make_ui_counters(l3, inputChoiceNameLists);

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

Level3 Level3UtilsDialog::getSettings() const
{
    {
        auto &ui = ui_stackStart;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l3.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
            m_l3.connections[row + ui.FirstUnitIndex] = ui.combos_connection[row]->currentIndex();
            auto &unit = m_l3.stackStart[row];
            unit.activate = ui.checks_activate[row]->isChecked();
            unit.stackIndex = ui.spins_stack[row]->value();
        }
    }

    {
        auto &ui = ui_masterTriggers;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l3.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
            m_l3.connections[row + ui.FirstUnitIndex] = ui.combos_connection[row]->currentIndex();
            auto &unit = m_l3.masterTriggers[row];
            unit.activate = ui.checks_activate[row]->isChecked();
        }
    }

    {
        auto &ui = ui_counters;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l3.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
            m_l3.connections[row + ui.FirstUnitIndex] = ui.combos_connection[row]->currentIndex();
        }
    }

    return m_l3;
}

// TODO: add AND, OR, invert and [min, max] bits setup helpers
LUTOutputEditor::LUTOutputEditor(
    int outputNumber,
    const QVector<QStringList> &inputNameLists,
    const Level2::DynamicConnections &dynamicInputValues,
    QWidget *parent)
    : QWidget(parent)
    , m_inputNameLists(inputNameLists)
{
    // LUT input bit selection
    auto table_inputs = new QTableWidget(trigger_io::LUT::InputBits, 2);
    m_inputTable = table_inputs;
    table_inputs->setHorizontalHeaderLabels({"Use", "Name" });

    for (int row = 0; row < table_inputs->rowCount(); row++)
    {
        table_inputs->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

        auto cb = new QCheckBox;
        m_inputCheckboxes.push_back(cb);

        table_inputs->setCellWidget(row, 0, make_centered(cb));

        auto inputNames = inputNameLists.value(row);

        QTableWidgetItem *nameItem = nullptr;

        // Multiple names in the list -> it's a dynamic input
        if (inputNames.size() > 1)
        {
            nameItem = new QTableWidgetItem(inputNames.value(dynamicInputValues[row]));
        }
        else // Single name -> use a plain table item
        {
            nameItem = new QTableWidgetItem(inputNames.value(0));
        }

        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table_inputs->setItem(row, 1, nameItem);
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

    //auto tb_inputSelect = new QToolbar();

    auto widget_inputSelect = new QWidget;
    auto layout_inputSelect = make_layout<QVBoxLayout>(widget_inputSelect);
    layout_inputSelect->addWidget(new QLabel("Input Bit Usage"));
    layout_inputSelect->addWidget(table_inputs, 1);

    // Initially empty output value table. Populated in onInputUsageChanged().
    m_outputTable = new QTableWidget(0, 1);
    m_outputTable->setHorizontalHeaderLabels({"State"});

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

    assert(rows <= trigger_io::LUT::InputCombinations);

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

#if 0
    // debug output of the full output bitmap
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
#endif
}

void LUTOutputEditor::setInputConnection(unsigned input, unsigned value)
{
    m_inputTable->setItem(input, 1, new QTableWidgetItem(
            m_inputNameLists[input][value]));
    m_inputTable->resizeColumnsToContents();
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
LUT::Bitmap LUTOutputEditor::getOutputMapping() const
{
    LUT::Bitmap result;

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

void LUTOutputEditor::setOutputMapping(const LUT::Bitmap &mapping)
{
    // Use the minbool lib to get the minimal set of input bits affecting the
    // output.
    std::vector<u8> minterms;

    for (size_t i = 0; i < mapping.size(); i++)
    {
        if (mapping[i])
            minterms.push_back(i);
    }

    auto solution = minbool::minimize_boolean<trigger_io::LUT::InputBits>(minterms, {});

    for (const auto &minterm: solution)
    {
        for (size_t bit = 0; bit < trigger_io::LUT::InputBits; bit++)
        {
            // Check all except the DontCare/Dash input bits
            if (minterm[bit] != minterm.Dash)
                m_inputCheckboxes[bit]->setChecked(true);
        }
    }

    const auto bitMap = getInputBitMapping();

    for (unsigned row = 0; row < static_cast<unsigned>(m_outputStateWidgets.size()); ++row)
    {
        // Calculate the full input value corresponding to this row.
        unsigned inputValue = 0u;

        for (int bitIndex = 0; bitIndex < bitMap.size(); ++bitIndex)
        {
            if (row & (1u << bitIndex))
                inputValue |= 1u << bitMap[bitIndex];
        }

        assert(inputValue < mapping.size());

        if (mapping[inputValue])
        {
            m_outputStateWidgets[row]->setChecked(true);
        }
    }
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const LUT &lut,
    const QVector<QStringList> &inputNameLists,
    const QStringList &outputNames,
    QWidget *parent)
    : LUTEditor(lutName, lut, inputNameLists, {}, outputNames, {}, 0u, {}, {}, parent)
{
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const LUT &lut,
    const QVector<QStringList> &inputNameLists,
    const Level2::DynamicConnections &dynConValues,
    const QStringList &outputNames,
    const QStringList &strobeInputNames,
    unsigned strobeConValue,
    const trigger_io::IO &strobeSettings,
    const std::bitset<trigger_io::LUT::OutputBits> strobedOutputs,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Lookup Table " + lutName + " Setup");

    auto scrollWidget = new QWidget;
    auto scrollLayout = make_layout<QVBoxLayout>(scrollWidget);

    // If there are dynamic inputs show selection combo boxes at the top of the
    // dialog.
    if (std::any_of(inputNameLists.begin(), inputNameLists.end(),
                    [] (const auto &l) { return l.size() > 1; }))
    {
        auto gb_inputSelect = new QGroupBox("Dynamic LUT Inputs");
        auto l_inputSelect = make_vbox(gb_inputSelect);

        // Note: goes from high to low to match the input bit selection table
        // below.
        for (int input = inputNameLists.size() - 1; input >= 0; input--)
        {
            const auto &inputNames = inputNameLists[input];

            if (inputNames.size() > 1)
            {
                auto label = new QLabel(QSL("Input%1").arg(input));
                auto combo = new QComboBox;
                combo->addItems(inputNames);

                if (input < static_cast<int>(dynConValues.size()))
                    combo->setCurrentIndex(dynConValues[input]);

                m_inputSelectCombos.push_back(combo);

                auto l = make_hbox();
                l->addWidget(label);
                l->addWidget(combo, 1);

                l_inputSelect->addLayout(l);

                // Notify the 3 output editors of changes to the dynamic inputs.
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                        this, [this, input] (int index)
                        {
                            for (auto outputEditor: m_outputEditors)
                                outputEditor->setInputConnection(input, index);
                        });
            }
        }

        // Undo the index reversal caused by the loop above.
        std::reverse(m_inputSelectCombos.begin(), m_inputSelectCombos.end());

        auto hl = make_hbox();
        hl->addWidget(gb_inputSelect);
        hl->addStretch(1);

        scrollLayout->addLayout(hl);
    }

    // 3 LUTOutputEditors side by side. Also a QLineEdit to set the
    // corresponding LUT output name.
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

        lutOutputEditor->setOutputMapping(lut.lutContents[output]);

        connect(nameEdit, &QLineEdit::textEdited,
                this, [this, output] (const QString &text)
                {
                    emit outputNameEdited(output, text);
                });
    }

    // Optional row to set which output should use the strobe GG.
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

    scrollLayout->addLayout(editorLayout, 10);

    // Optional single-row table for the strobe GG settings.
    if (!strobeInputNames.isEmpty())
    {
        QStringList columnTitles = {
            "Input", "Delay", "Width", "Holdoff"
        };

        auto &ui = m_strobeTableUi;

        auto table = new QTableWidget(1, columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        table->verticalHeader()->hide();

        auto combo_connection = new QComboBox;

        ui.table = table;
        ui.combo_connection = combo_connection;

        combo_connection->addItems(strobeInputNames);
        combo_connection->setCurrentIndex(strobeConValue);
        combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        table->setCellWidget(0, ui.ColConnection, combo_connection);
        table->setItem(0, ui.ColDelay, new QTableWidgetItem(QString::number(strobeSettings.delay)));
        table->setItem(0, ui.ColWidth, new QTableWidgetItem(QString::number(strobeSettings.width)));
        table->setItem(0, ui.ColHoldoff, new QTableWidgetItem(QString::number(strobeSettings.holdoff)));

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

LUT::Contents LUTEditor::getLUTContents() const
{
    LUT::Contents ret = {};

    for (int output = 0; output < m_outputEditors.size(); output++)
    {
        ret[output] = m_outputEditors[output]->getOutputMapping();
    }

    return ret;
}

QStringList LUTEditor::getOutputNames() const
{
    QStringList ret;

    for (auto &le: m_outputNameEdits)
        ret.push_back(le->text());

    return ret;
}

Level2::DynamicConnections LUTEditor::getDynamicConnectionValues()
{
    Level2::DynamicConnections ret = {};

    for (size_t input = 0; input < ret.size(); input++)
        ret[input] = m_inputSelectCombos[input]->currentIndex();

    return ret;
}

unsigned LUTEditor::getStrobeConnectionValue()
{
    return static_cast<unsigned>(m_strobeTableUi.combo_connection->currentIndex());
}

trigger_io::IO LUTEditor::getStrobeSettings()
{
    auto &ui = m_strobeTableUi;

    trigger_io::IO ret = {};

    ret.delay = ui.table->item(0, ui.ColDelay)->text().toUInt();
    ret.width = ui.table->item(0, ui.ColWidth)->text().toUInt();
    ret.holdoff = ui.table->item(0, ui.ColHoldoff)->text().toUInt();

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

