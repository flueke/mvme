/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc/mvlc_trigger_io_editor_p.h"

#include <boost/range/adaptor/indexed.hpp>
#include <cassert>
#include <cmath>
#include <minbool.h>
#include <QBoxLayout>
#include <QDebug>
#include <QDialogButtonBox>
#include <QGraphicsPolygonItem>
#include <QGraphicsSceneMouseEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QWheelEvent>

#include "qt_util.h"
#include "mvme_qthelp.h"

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

} // end anon namespace

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_config
{

namespace gfx
{

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
    : ConnectorBase(parent)
    , m_circle(new QGraphicsEllipseItem(
            0, 0, 2*ConnectorRadius, 2*ConnectorRadius, this))
{
    setPen(Qt::NoPen);
    setBrush(Qt::blue);
    setLabel(label);
}

QRectF ConnectorCircleItem::boundingRect() const
{
    return m_circle->boundingRect();
}

void ConnectorCircleItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
           QWidget *widget)
{
    m_circle->setPen(pen());
    m_circle->setBrush(brush());
    m_circle->paint(painter, option, widget);
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

void ConnectorCircleItem::alignmentSet_(const Qt::Alignment &)
{
    adjust();
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
    : ConnectorBase(parent)
    , m_baseLength(baseLength)
{
    setPen(Qt::NoPen);
    setBrush(Qt::blue);
}

ConnectorDiamondItem::ConnectorDiamondItem(QGraphicsItem *parent)
    : ConnectorDiamondItem(SideLength, parent)
{ }

QRectF ConnectorDiamondItem::boundingRect() const
{
    return QRectF(0, 0, m_baseLength, m_baseLength);
}

void ConnectorDiamondItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
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

void ConnectorDiamondItem::alignmentSet_(const Qt::Alignment &)
{
    adjust();
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

void BlockItem::setInputNames(const QStringList &names)
{
    for (const auto &kv: names | indexed(0))
    {
        if (auto connector = getInputConnector(kv.index()))
        {
            connector->setLabel(kv.value());
        }
    }

    QGraphicsRectItem::update();
}

void BlockItem::setOutputNames(const QStringList &names)
{
    for (const auto &kv: names | indexed(0))
    {
        if (auto connector = getOutputConnector(kv.index()))
        {
            connector->setLabel(kv.value());
        }
    }
}

void BlockItem::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    setBrush(Block_Brush_Hover);

    for (auto con: m_inputConnectors)
    {
        if (con->isEnabled())
            con->setBrush(Connector_Brush_Hover);
        else
            con->setBrush(Connector_Brush_Disabled);
    }

    for (auto con: m_outputConnectors)
    {
        if (con->isEnabled())
            con->setBrush(Connector_Brush_Hover);
        else
            con->setBrush(Connector_Brush_Disabled);
    }

    QGraphicsRectItem::update();
}

void BlockItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    setBrush(Block_Brush);
    for (auto con: m_inputConnectors)
    {
        if (con->isEnabled())
            con->setBrush(Connector_Brush);
        else
            con->setBrush(Connector_Brush_Disabled);
    }

    for (auto con: m_outputConnectors)
    {
        if (con->isEnabled())
            con->setBrush(Connector_Brush);
        else
            con->setBrush(Connector_Brush_Disabled);
    }

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
        m_strobeConnector = con;
    }
}

//
// CounterItem
//
CounterItem::CounterItem(unsigned counterIndex, QGraphicsItem *parent)
    : BlockItem(
        Width, Height,
        Inputs, Outputs,
        InputConnectorMargin, InputConnectorMargin,
        OutputConnectorMargin, OutputConnectorMargin,
        parent)
    , m_labelItem(new QGraphicsSimpleTextItem(this))
{
    {
        auto font = m_labelItem->font();
        font.setPixelSize(LabelPixelSize);
        m_labelItem->setFont(font);
    }

    getInputConnector(0)->setLabel("counter");
    getInputConnector(1)->setLabel("latch");
    setCounterName(QString("Counter%1").arg(counterIndex));
}

void CounterItem::setCounterName(const QString &name)
{
    m_labelItem->setText(name);
    m_labelItem->setPos({0, 0});

    m_labelItem->moveBy(
        (this->boundingRect().width() - m_labelItem->boundingRect().width()) * .75,
        (this->boundingRect().height() - m_labelItem->boundingRect().height()) * 0.5
        );
}

template<typename T>
QPointF get_center_point(T *item)
{
    return item->boundingRect().center();
}

template<typename T>
QPointF get_scene_center_point(T *item)
{
    return item->mapToScene(item->boundingRect().center());
}

//
// LineAndArrow
//
LineAndArrow::LineAndArrow(const QPointF &sceneEnd, QGraphicsItem *parent)
    : QAbstractGraphicsShapeItem(parent)
{
    //setFlags(QGraphicsItem::ItemSendsGeometryChanges);
    setBrush(Qt::black);
    setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    setEnd(sceneEnd);
}

QVariant LineAndArrow::itemChange(GraphicsItemChange change, const QVariant &value)
{
#if 0
    qDebug() << __PRETTY_FUNCTION__ << this << change << value;

    if (change == GraphicsItemChange::ItemSceneChange
        || change == GraphicsItemChange::ItemVisibleChange
        || change == GraphicsItemChange::ItemPositionChange)
    {
        qDebug() << __PRETTY_FUNCTION__ << this << change << value << "calling adjust()";
        adjust();
    }
#endif

    return QAbstractGraphicsShapeItem::itemChange(change, value);
}

void LineAndArrow::setArrowSize(qreal arrowSize)
{
    m_arrowSize = arrowSize;
    adjust();
}

void LineAndArrow::setEnd(const QPointF &p)
{
    m_sceneEnd = p;
    //qDebug() << __PRETTY_FUNCTION__ <<  "given endPoint = " << p << ", mapFromScene =" << mapFromScene(p);
    adjust();
}

QPointF LineAndArrow::getAdjustedEnd() const
{
    QPointF lineStart{0, 0};
    QPointF lineEnd = mapFromScene(m_sceneEnd);

    QLineF line(lineStart, lineEnd);
    qreal length = line.length();

    if (length > qreal(2.)) {
        // Shortens the line by 'offset' pixels at the end.
        qreal offset = 6;
        qreal offsetPercent = (length - offset) / length;
        //m_adjustedStart = line.pointAt(1.0 - offsetPercent);
        lineEnd = line.pointAt(offsetPercent);
    } else {
         lineEnd = line.p1();
    }

    return lineEnd;
}

void LineAndArrow::adjust()
{
    prepareGeometryChange();
}

QRectF LineAndArrow::boundingRect() const
{
    qreal penWidth = 1;
    qreal extra = (penWidth + m_arrowSize) / 2.0;

    QPointF lineStart{0, 0};
    QPointF lineEnd = getAdjustedEnd();

    return QRectF(lineStart,
                  QSizeF(lineEnd.x() - lineStart.x(),
                         lineEnd.y() - lineStart.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void LineAndArrow::paint(
    QPainter *painter,
    const QStyleOptionGraphicsItem *,
    QWidget *)
{
    QPointF lineStart{0, 0};
    QPointF lineEnd = getAdjustedEnd();

    QLineF line(lineStart, lineEnd);
    if (qFuzzyCompare(line.length(), qreal(0.)))
        return;

    // Draw the line itself
    painter->setPen(pen());
    painter->setBrush(brush());
    painter->drawLine(line);

    // Draw the arrows
    double angle = std::atan2(-line.dy(), line.dx());

    QPointF destArrowP1 = lineEnd + QPointF(sin(angle - M_PI / 3) * m_arrowSize,
                                            cos(angle - M_PI / 3) * m_arrowSize);

    QPointF destArrowP2 = lineEnd + QPointF(sin(angle - M_PI + M_PI / 3) * m_arrowSize,
                                            cos(angle - M_PI + M_PI / 3) * m_arrowSize);

    // Note: the alphas of the brush and the pen seem to get composed. This
    // leads to the pen outline being very visible. I didn't get it to work as
    // I wanted when playing with different composition modes so I decided not
    // to use any alpha for the line and arrow.

    painter->drawPolygon(QPolygonF() << line.p2() << destArrowP1 << destArrowP2);
}

//
// HorizontalBusBar
//
HorizontalBusBar::HorizontalBusBar(const QString &labelText, QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
    // the bar
    m_bar = new QGraphicsRectItem(0, 0, 90, 10, this);
    m_bar->setPen(Qt::NoPen);

    // right side arrow
    m_arrow = new LineAndArrow(m_dest, this);
    m_arrow->setPos({ m_bar->boundingRect().right(), m_bar->boundingRect().center().y() });
    m_arrow->setArrowSize(6);

    // label centered in the bar
    if (!labelText.isEmpty())
    {
        m_label = new QGraphicsSimpleTextItem(labelText, this);
        auto font = m_label->font();
        font.setPixelSize(8);
        m_label->setFont(font);

        // center the text in the bar rectangle
        auto w1 = m_bar->boundingRect().width();
        auto w2 = m_label->boundingRect().width();
        auto h1 = m_bar->boundingRect().height();
        auto h2 = m_label->boundingRect().height();
        auto x = (w1 - w2) * 0.5;
        auto y = (h1 - h2) * 0.5;
        m_label->setPos(x, y);
    }
}

void HorizontalBusBar::setDestPoint(const QPointF &p)
{
    m_arrow->setEnd(p);
}

void HorizontalBusBar::setBarBrush(const QBrush &brush)
{
    m_bar->setBrush(brush);
}

void HorizontalBusBar::setLabelBrush(const QBrush &brush)
{
    if (m_label)
        m_label->setBrush(brush);
}

LineAndArrow *HorizontalBusBar::getLineAndArrow() const
{
    return m_arrow;
}

QRectF HorizontalBusBar::boundingRect() const
{
    auto result = m_bar->boundingRect();

    if (m_label)
        result = result.united(m_label->boundingRect());

    return result;
}

void HorizontalBusBar::paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *)
{
}

//
// MoveableRect
//
MovableRect::MovableRect(int w, int h, QGraphicsItem *parent)
    : QGraphicsRectItem(0, 0, w, h, parent)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
}

QVariant MovableRect::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == GraphicsItemChange::ItemPositionChange)
        qDebug() << __PRETTY_FUNCTION__ << this << change << value;

    return QAbstractGraphicsShapeItem::itemChange(change, value);
}

//
// Edge
//
Edge::Edge(QAbstractGraphicsShapeItem *sourceItem, QAbstractGraphicsShapeItem *destItem)
    : m_source(sourceItem)
    , m_dest(destItem)
    , m_arrowSize(8)
{
    setAcceptedMouseButtons(0);
    setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    setBrush(Qt::black);
    adjust();
}

void Edge::setSourceItem(QAbstractGraphicsShapeItem *item)
{
    m_source = item;
    adjust();
}

void Edge::setDestItem(QAbstractGraphicsShapeItem *item)
{
    m_dest = item;
    adjust();
}

void Edge::adjust()
{
    if (!m_source || !m_dest)
    {
        return;
    }

    QLineF line(mapFromItem(m_source, get_center_point(m_source)),
                mapFromItem(m_dest, get_center_point(m_dest)));

    qreal length = line.length();

    prepareGeometryChange();

    if (length > qreal(20.)) {
        // Shortens the line by 'offset' pixels at the start and
        // end.
        qreal offset = 4;
        qreal offsetPercent = (length - offset) / length;
        m_sourcePoint = line.pointAt(1.0 - offsetPercent);
        m_destPoint = line.pointAt(offsetPercent);
    } else {
        m_sourcePoint = m_destPoint = line.p1();
    }
}

QRectF Edge::boundingRect() const
{
    qreal penWidth = 1;
    qreal extra = (penWidth + m_arrowSize) / 2.0;

    return QRectF(m_sourcePoint, QSizeF(m_destPoint.x() - m_sourcePoint.x(),
                                        m_destPoint.y() - m_sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void Edge::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    QLineF line(m_sourcePoint, m_destPoint);
    if (qFuzzyCompare(line.length(), qreal(0.)))
        return;

    // Draw the line itself
    painter->setPen(pen());
    painter->setBrush(brush());
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
    auto make_level0_input_items = [&] () -> Level0InputItems
    {
        Level0InputItems result = {};

        result.parent = new QGraphicsRectItem(
            0, 0,
            150, 670);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM+ECL IO Item
        {
            result.nimItem = new gfx::BlockItem(
                100, 470, // width, height
                trigger_io::NIM_IO_Count, // inputCount: connectors symbolizing the pyhsical NIM inputs
                trigger_io::NIM_IO_Count, // outputCount: NIM connectors towards L1
                48, // inputConnectorMargin
                48, // outputConnectorMargin
                result.parent);

            for (auto c: result.nimItem->inputConnectors())
            {
                c->setEnabled(false);
                c->setBrush(gfx::Connector_Brush_Disabled);
            }

            result.nimItem->moveBy(25, 25 + 140 + 5);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Inputs"), result.nimItem);
            label->moveBy((result.nimItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        // IRQ inputs
        {
            result.irqItem = new gfx::BlockItem(
                100, 140, // width, height
                trigger_io::Level0::IRQ_Inputs_Count, // inputCount
                trigger_io::Level0::IRQ_Inputs_Count, // outputCount
                16, // inputConnectorMargin
                16, // outputConnectorMargin
                result.parent);

            int irq = 1;
            for (auto c: result.irqItem->inputConnectors())
            {
                c->setEnabled(false);
                c->setBrush(gfx::Connector_Brush_Disabled);
                c->setLabel(QSL("IRQ%1").arg(irq++));
            }

            result.irqItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("IRQs"), result.irqItem);
            label->moveBy((result.irqItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L0", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        {
            auto text = new QGraphicsSimpleTextItem(
                QSL("Front Panel NIM/TTL inputs"),
                result.parent);
            text->setRotation(-90);

            auto pos = gfx::get_scene_center_point(result.nimItem);
            pos.setX(0);
            pos.setY(pos.y() + text->boundingRect().width() * 0.5);
            text->setPos(pos);
        }

        {
            auto text = new QGraphicsSimpleTextItem(
                QSL("VME IRQ lines"),
                result.parent);
            text->setRotation(-90);

            auto pos = gfx::get_scene_center_point(result.irqItem);
            pos.setX(0);
            pos.setY(pos.y() + text->boundingRect().width() * 0.5);
            text->setPos(pos);
        }

        {
            auto ggText = new QGraphicsSimpleTextItem(
                QSL("Gate Generators"),
                result.nimItem);
            ggText->setRotation(-90);
            ggText->setPos(ggText->parentItem()->boundingRect().center());
            ggText->moveBy(-ggText->boundingRect().height() * 0.5,
                           +ggText->boundingRect().width() * 0.5);
        }

        return result;
    };

    auto make_level1_items = [&] () -> Level1Items
    {
        Level1Items result = {};

        // background box containing the LUTs
        result.parent = new QGraphicsRectItem(
            0, 0,
            260, 670);

        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = new gfx::LUTItem(lutIdx, false, result.parent);
            result.luts[lutIdx] = lutItem;
        }

        QRectF lutRect = result.luts[0]->rect();

        lutRect.translate(25, 25);
        result.luts[5]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[2]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[1]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[0]->setPos(lutRect.topLeft());

        lutRect.moveTo(lutRect.width() + 50, 0);
        lutRect.translate(25, 25);
        lutRect.translate(0, (lutRect.height() + 25) / 2.0);
        result.luts[6]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
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
            260,
            1.5 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // Single box for utils
        {
            result.utilsItem = new gfx::BlockItem(
                result.parent->boundingRect().width() - 2 * 25,
                result.parent->boundingRect().height() - 2 * 25,
                0, trigger_io::Level0::UtilityUnitCount,
                0, 8,
                result.parent);
            result.utilsItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(
                QString("Timers\nIRQs\nSoft Triggers\nSlave Trigger Input\nStack Busy\nVME System Clock"),
                result.utilsItem);
            label->moveBy(5, 5);
            //label->moveBy((result.utilsItem->boundingRect().width()
            //               - label->boundingRect().width()) / 2.0, 0);
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L0 Utilities", result.parent);
        result.label->setFont(labelFont);
        //result.label->moveBy(result.parent->boundingRect().width()
        //                     - result.label->boundingRect().width(), 0);

        return result;
    };

    auto make_level2_items = [&] () -> Level2Items
    {
        Level2Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        // background box containing the 2 LUTs
        result.parent = new QGraphicsRectItem(
            0, 0,
            260, 670);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = new gfx::LUTItem(lutIdx, true, result.parent);
            result.luts[lutIdx] = lutItem;
        }

        int xPos = (result.parent->boundingRect().width() * 0.5
                    - lutRect.width() * 0.5);

        lutRect.moveTo(xPos, 0);
        lutRect.translate(0, 25);
        lutRect.translate(0, (lutRect.height() + 25) / 2.0);
        result.luts[2]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
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

        //QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            260, 520);
            //2 * (lutRect.width() + 50) + 25,
            //4 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // NIM IO Item
        {
            result.nimItem = new gfx::BlockItem(
                100, 370, // width, height
                trigger_io::NIM_IO_Count, // inputCount: internal connectors
                trigger_io::NIM_IO_Count, // outputCount: symbolizing front panel LEMO connectors
                48, 48, // in and out connector margins
                result.parent);

            for (auto c: result.nimItem->outputConnectors())
            {
                c->setEnabled(false);
                c->setBrush(gfx::Connector_Brush_Disabled);
            }

            result.nimItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(QString("NIM Outputs"), result.nimItem);
            label->moveBy((result.nimItem->boundingRect().width()
                           - label->boundingRect().width()) / 2.0, 0);

            {
                auto ggText = new QGraphicsSimpleTextItem(
                    QSL("Gate Generators"),
                    result.nimItem);
                ggText->setRotation(-90);
                ggText->setPos(ggText->parentItem()->boundingRect().center());
                ggText->moveBy(-ggText->boundingRect().height() * 0.5,
                               +ggText->boundingRect().width() * 0.5);
            }
        }

        auto yOffset = result.nimItem->boundingRect().height() + 25;

        // ECL Out
        {
            result.eclItem = new gfx::BlockItem(
                100, 80, // width, height
                trigger_io::ECL_OUT_Count, // inputCount: internal connectors
                trigger_io::ECL_OUT_Count, // outputCount: symbolizing front panel ECL outputs
                24, 24,
                result.parent);

            for (auto c: result.eclItem->outputConnectors())
            {
                c->setEnabled(false);
                c->setBrush(gfx::Connector_Brush_Disabled);
            }

            result.eclItem->moveBy(25, 25);

            {
                auto label = new QGraphicsSimpleTextItem(QString("LVDS Outputs"), result.eclItem);
                label->moveBy((result.eclItem->boundingRect().width()
                               - label->boundingRect().width()) / 2.0, 0);

                result.eclItem->moveBy(0, yOffset);

                {
                    auto ggText = new QGraphicsSimpleTextItem(
                        QSL("GGs"),
                        result.eclItem);
                    ggText->setRotation(-90);
                    ggText->setPos(ggText->parentItem()->boundingRect().center());
                    ggText->moveBy(-ggText->boundingRect().height() * 0.5,
                                   +ggText->boundingRect().width() * 0.5);
                }
            }
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L3", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        auto frontPanelInputsText = new QGraphicsSimpleTextItem(
            QSL("Front Panel NIM/TTL and LVDS outputs"),
            result.parent);
        frontPanelInputsText->setRotation(-90);

        frontPanelInputsText->moveBy(25+100+frontPanelInputsText->boundingRect().height(), 0);
        frontPanelInputsText->moveBy(0, result.parent->boundingRect().height() * 0.5);
        frontPanelInputsText->moveBy(0, frontPanelInputsText->boundingRect().width() * 0.5);

        return result;
    };

    auto make_level3_util_items = [&] () -> Level3UtilItems
    {
        Level3UtilItems result = {};

        QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            260,
            2.0 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f3f3f3"));

        // Single box for utils.
        {
            result.utilsItem = new gfx::BlockItem(
                result.parent->boundingRect().width() - 2 * 25,     // width
                result.parent->boundingRect().height() - 2 * 25,    // height
                // inputCount, outputCount
                trigger_io::Level3::UtilityUnitCount - trigger_io::Level3::CountersCount, 0,
                // inputConnectorMarginTop, inputConnectorMarginBottom
                (gfx::CounterItem::Height + 4) * Level3::CountersCount, 12,
                // outputConnectorMarginTop, outputConnectorMarginBottom
                0, 0,
                // parent
                result.parent);
            result.utilsItem->moveBy(25, 25);

            auto label = new QGraphicsSimpleTextItem(
                QString("Stack Start\nMaster Triggers\nCounters"), result.utilsItem);
            label->moveBy(105, 5);

            // Special items for the counters which have two connectors instead of one.

            for (unsigned counter=0; counter<Level3::CountersCount; counter++)
            {
                auto counterItem = new gfx::CounterItem(counter, result.utilsItem);

                if (counter == 0) // make the counter+stamper block wider
                {
                    auto rect = counterItem->rect();
                    rect.setWidth(rect.width() + 75);
                    counterItem->setRect(rect);
                }

                counterItem->moveBy(
                    0,
                    (counterItem->boundingRect().height() + 2) * (Level3::CountersCount - 1 - counter));
                result.counterItems.push_back(counterItem);

            }
        }

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L3 Utilities", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width()
                             - result.label->boundingRect().width(), 0);

        return result;
    };

    // Top row, side by side gray boxes for each level
    m_level0InputItems = make_level0_input_items();
    m_level0InputItems.parent->moveBy(-160, 0);

    m_level0UtilItems = make_level0_util_items();
    m_level0UtilItems.parent->moveBy(
        0, m_level0InputItems.parent->boundingRect().height() + 15);

    m_level1Items = make_level1_items();
    m_level2Items = make_level2_items();
    m_level2Items.parent->moveBy(270, 0);
    m_level3Items = make_level3_items();
    m_level3Items.parent->moveBy(540, 0);
    m_level3UtilItems = make_level3_util_items();
    m_level3UtilItems.parent->moveBy(
        540, m_level3Items.parent->boundingRect().height() + 15);

    this->addItem(m_level0InputItems.parent);
    this->addItem(m_level1Items.parent);
    this->addItem(m_level2Items.parent);
    this->addItem(m_level3Items.parent);
    this->addItem(m_level0UtilItems.parent);
    this->addItem(m_level3UtilItems.parent);

    // Add bus bar items serving as a kind of map of the connection
    // possibilities.
    // This is purposely done before the connection edges are created so that
    // the busbars naturally stack behind the edges (no z-value fiddling
    // needed).
    QColor barColor("#7da1d6");
    QBrush barBrush(barColor);
    QPen barPen(barBrush, 1.0);
    QColor labelColor(Qt::black);
    QBrush labelBrush(labelColor);

    auto add_h_busbar = [this, barBrush, labelBrush] (const QGraphicsItem *dest, QString label = {})
    {
        auto busBar = new gfx::HorizontalBusBar(label);
        busBar->setBarBrush(barBrush);
        busBar->setLabelBrush(labelBrush);

        auto destCenter = gfx::get_scene_center_point(dest);
        busBar->setDestPoint(destCenter);
        busBar->setPos(destCenter);
        busBar->moveBy(-(busBar->boundingRect().width() + 20), -(busBar->boundingRect().height() * 0.5));

        this->addItem(busBar);
        m_connectionBars.push_back(busBar);
    };

    auto make_big_arrow_bar = [barBrush, barPen] (const QRectF barRect, qreal arrowWidthFactor, qreal arrowHeightFactor)
        -> QGraphicsRectItem *
    {
        auto bar = new QGraphicsRectItem(barRect);
        bar->setPen(barPen);
        bar->setBrush(barBrush);

        auto rect = QRectF(0, 0, arrowWidthFactor*barRect.width(), arrowHeightFactor*barRect.width());
        auto p1 = rect.bottomLeft();
        auto p2 = rect.bottomRight();
        auto p3 = QPointF{ rect.center().x(), rect.top() };

        auto bigArrow = new QGraphicsPolygonItem(bar);
        bigArrow->setPen(Qt::NoPen);
        bigArrow->setPen(barPen);
        bigArrow->setBrush(barBrush);
        bigArrow->setPolygon(QPolygonF() << p1 << p2 << p3);
        bigArrow->moveBy(-((rect.width() - barRect.width()) * 0.5), -(rect.height()));

        QPointF newOriginPoint = { barRect.width() * 0.5, barRect.height() };
        bar->setTransformOriginPoint(newOriginPoint);

        return bar;
    };

    auto set_big_arrow_bar_pos = [](QGraphicsRectItem *bar, QPointF scenePos)
    {
        bar->setPos(scenePos);
        auto barOrigin = bar->mapToScene(bar->transformOriginPoint());
        bar->moveBy(-fabs(scenePos.x() - barOrigin.x()), -fabs(scenePos.y() - barOrigin.y()));
    };

    // l2 lut busbars
    {
        // strobe inputs
        for (unsigned i=0; i<Level2::LUTCount; ++i)
            add_h_busbar(m_level2Items.luts[i]->getStrobeConnector(), QSL("L0.Util, L1.LUT3/4/6, L2"));

        // l2.lut0
        for (int i=0; i<3; i++)
            add_h_busbar(m_level2Items.luts[0]->getInputConnector(i), QSL("L0.Util, L1.LUT4.%1/6").arg(i));

        // l2.lut1
        for (int i=0; i<3; i++)
            add_h_busbar(m_level2Items.luts[1]->getInputConnector(i), QSL("L0.Util, L1.LUT3.%1/6").arg(i));

        // l2.lut2
        for (int i=0; i<3; i++)
            add_h_busbar(m_level2Items.luts[2]->getInputConnector(i), QSL("L0.Util, L1.LUT3.%1/6").arg(i));
    }

    // busbars to l3 outputs
    {
        QGraphicsRectItem *vertBar = nullptr;
        // vertical bus bar for connections going into NIM/LVDS outputs
        {
            auto bar = new QGraphicsRectItem(520, 25, 18, 550);
            bar->setBrush(barBrush);
            bar->setPen(Qt::NoPen);

            this->addItem(bar);
            m_connectionBars.push_back(bar);

            vertBar = bar;
        }
        // vertbar text
        {
            auto text = new QGraphicsSimpleTextItem(
                QSL("L2 to Front Panel outputs"),
                vertBar);
            auto font = text->font();
            font.setPixelSize(16);
            text->setFont(font);
            text->setRotation(-90);

            auto pos = gfx::get_scene_center_point(vertBar);
            pos.setX(pos.x() - text->boundingRect().height() * 0.5);
            pos.setY(pos.y() + text->boundingRect().width() * 0.5);
            text->setPos(pos);
        }
        // l2.luts -> vertical bus bar to outputs
        for (unsigned i=0; i<Level2::LUTCount; ++i)
        {
            auto bar = make_big_arrow_bar({ 0, 0, 34, 64 }, 1.5, 0.3);
            auto barPos = get_scene_center_point(m_level2Items.luts[i]->getOutputConnector(1));
            barPos.setX(barPos.x() + 4.5);
            barPos.setY(barPos.y() + 0.33 * m_level2Items.luts[i]->boundingRect().height());
            set_big_arrow_bar_pos(bar, barPos);
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
        // vertical bar -> l3 nim out
        {
            auto bar = make_big_arrow_bar({ 0, 0, 34, 10 }, 1.5, 0.3);
            auto barPos = get_scene_center_point(m_level3Items.nimItem->getInputConnector(6));
            barPos.setX(vertBar->boundingRect().right());
            set_big_arrow_bar_pos(bar, barPos);
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
        // vertical bar -> l3 lvds out
        {
            auto bar = make_big_arrow_bar({ 0, 0, 34, 10 }, 1.5, 0.3);
            auto barPos = get_scene_center_point(m_level3Items.eclItem->getInputConnector(1));
            barPos.setX(vertBar->boundingRect().right());
            set_big_arrow_bar_pos(bar, barPos);
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
    }

    // busbars to l3 utils
    {
        QGraphicsRectItem *vertBar = nullptr;
        // vertical bus bar for connections going into l3 utils
        {
            auto bar = new QGraphicsRectItem(465, 107, 18, 780);
            bar->setBrush(barBrush);
            bar->setPen(Qt::NoPen);

            this->addItem(bar);
            m_connectionBars.push_back(bar);

            vertBar = bar;
        }
        // vertbar text
        {
            auto text = new QGraphicsSimpleTextItem(
                QSL("L0.Util/L2 to L3.Util"),
                vertBar);
            auto font = text->font();
            font.setPixelSize(16);
            text->setFont(font);
            text->setRotation(-90);

            auto pos = gfx::get_scene_center_point(vertBar);
            pos.setX(pos.x() - text->boundingRect().height() * 0.5);
            //pos.setY(pos.y() * 1.3);
            text->setPos(pos);
        }
        // l2.luts -> vertical bus bar to l3 utils
        for (unsigned i=0; i<Level2::LUTCount; ++i)
        {
            auto bar = make_big_arrow_bar({ 0, 0, 34, 10 }, 1.5, 0.3);
            auto barPos = get_scene_center_point(m_level2Items.luts[i]->getOutputConnector(1));
            barPos.setX(barPos.x() + 3.5);
            barPos.setY(barPos.y() - 0.33 * m_level2Items.luts[i]->boundingRect().height());
            set_big_arrow_bar_pos(bar, barPos);
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
        // vertical bar -> l3 utils
        {
            auto bar = make_big_arrow_bar({0, 0, 34, 60}, 1.5, 0.3);
            auto barPos = m_level3UtilItems.parent->mapToScene(m_level3UtilItems.parent->rect().center());
            barPos.setX(vertBar->boundingRect().right());
            set_big_arrow_bar_pos(bar, barPos);
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
        // l0 utils -> vertical bar to l3 utils
        {
            auto bar = make_big_arrow_bar({0, 0, 34, 200}, 1.5, 0.3);
            auto py = m_level0UtilItems.parent->mapToScene(m_level0UtilItems.parent->rect().center()).y();
            set_big_arrow_bar_pos(bar, { 250, py });
            bar->setRotation(90);

            this->addItem(bar);
            m_connectionBars.push_back(bar);
        }
    }

    // testbar
    //{
    //    auto bar = new gfx::MovableRect(18, 760);
    //    this->addItem(bar);
    //}

    // Create all connection edges contained in the trigger io config. The
    // logic in setTriggerIOConfig then decides if edges should be shown/hidden
    // or drawn in a different way.
    // Additionally edges for static connections are added.

    // static level 1 connections
    for (const auto &lutkv: Level1::StaticConnections | indexed(0))
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
            auto destConnector = getInputConnector({1, lutIndex, inputIndex});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
                addStaticConnectionEdge(sourceConnector, destConnector);
            }
        }
    }

    // L1.LUT2 dynamic connections
    for (unsigned input=0; input<LUT_DynamicInputCount; ++input)
    {
        unsigned conValue = ioCfg.l1.lut2Connections[input];
        UnitAddress conAddress = Level1::LUT2DynamicInputChoices[input][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({1, 2, input});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    // L1.LUT2 static edges for the dynamic connections (a special case)
    for (unsigned input=0; input<LUT_DynamicInputCount; ++input)
    {
        const auto &choices = Level1::LUT2DynamicInputChoices[input];

        for (const auto &srcAddr: choices)
        {
            auto sourceConnector = getOutputConnector(srcAddr);
            auto destConnector = getInputConnector({1, 2, input});

        if (sourceConnector && destConnector)
        {
            addStaticConnectionEdge(sourceConnector, destConnector);
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
                addStaticConnectionEdge(sourceConnector, destConnector);
            }
        }
    }

    // level2 dynamic connections
    for (const auto &lutkv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = lutkv.index();
        const auto &l2InputChoices = Level2::DynamicInputChoices[unitIndex];

        for (unsigned input = 0; input < LUT_DynamicInputCount; input++)
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

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

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

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

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

        // counter input
        {
            unsigned conValue = ioCfg.l3.connections[unitIndex][0];
            UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

            auto sourceConnector = getOutputConnector(conAddress);
            auto destConnector = getInputConnector({3, unitIndex, 0});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }

        // latch input
        // (FIXME: hack with the connection address which is invalid by default (l3.unit33 ("not connected"))
        {
            //unsigned conValue = ioCfg.l3.connections[unitIndex][1];
            //UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][1][conValue];
            UnitAddress conAddress = { 0, Level0::SysClockOffset };

            auto sourceConnector = getOutputConnector(conAddress);
            auto destConnector = getInputConnector({3, unitIndex, 1});

            if (sourceConnector && destConnector)
            {
                addEdge(sourceConnector, destConnector);
            }
        }
    }

    // Level3 NIM connections
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

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

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        auto sourceConnector = getOutputConnector(conAddress);
        auto destConnector = getInputConnector({3, unitIndex});

        if (sourceConnector && destConnector)
        {
            addEdge(sourceConnector, destConnector);
        }
    }

    // Set the config. This triggers the initial edge updates.
    setTriggerIOConfig(ioCfg);

    //qDebug() << __PRETTY_FUNCTION__ << "created" << m_edges.size() << " Edges";
    //qDebug() << __PRETTY_FUNCTION__ << "sceneRect=" << this->sceneRect();
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
            else if (trigger_io::Level3::CountersOffset <= addr[1]
                     && addr[1] < trigger_io::Level3::CountersOffset + trigger_io::Level3::CountersCount)
            {
                auto counterItem = m_level3UtilItems.counterItems.value(
                    addr[1] - trigger_io::Level3::CountersOffset);
                if (counterItem)
                    return counterItem->getInputConnector(addr[2]);
                return nullptr;
            }
            else
            {
                return m_level3UtilItems.utilsItem->inputConnectors().value(addr[1]);
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
                return m_level0InputItems.nimItem->outputConnectors().value(
                    addr[1] - trigger_io::Level0::NIM_IO_Offset);
            }
            else if (trigger_io::Level0::IRQ_Inputs_Offset <= addr[1]
                     && addr[1] < trigger_io::Level0::IRQ_Inputs_Offset + trigger_io::Level0::IRQ_Inputs_Count)
            {
                return m_level0InputItems.irqItem->outputConnectors().value(
                    addr[1] - trigger_io::Level0::IRQ_Inputs_Offset);
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

    assert(false);
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
    edge->setZValue(1.0);
    this->addItem(edge);
    return edge;
}

QList<gfx::Edge *> TriggerIOGraphicsScene::getEdgesBySourceConnector(
    QAbstractGraphicsShapeItem *sourceConnector) const
{
    return m_edgesBySource.values(sourceConnector);
}

gfx::Edge * TriggerIOGraphicsScene::getEdgeByDestConnector(
    QAbstractGraphicsShapeItem *sourceConnector) const
{
    return m_edgesByDest.value(sourceConnector);
}

gfx::Edge *TriggerIOGraphicsScene::addStaticConnectionEdge(
    QAbstractGraphicsShapeItem *sourceConnector,
    QAbstractGraphicsShapeItem *destConnector)
{
    static const QColor penColor("#7da1d6"); // same as the connection bars
    static const QColor brushColor(penColor);

    auto edge = new gfx::Edge(sourceConnector, destConnector);
    edge->setPen(QPen(penColor, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    edge->setBrush(brushColor);
    m_staticEdges.push_back(edge);
    edge->adjust();
    this->addItem(edge);
    return edge;
}

void TriggerIOGraphicsScene::setTriggerIOConfig(const TriggerIO &ioCfg)
{
    using namespace gfx;

    // Helper performing common edge and connector update tasks.
    //
    // unit is the actual unit struct being checked, e.g. IO, ECL,
    //   MasterTrigger, etc.
    // addr is the full UnitAddress of the unit being checked
    // cond is a predicate taking one argument: the unit structure itself.
    //   The dest connector and the edge are disabled/hidden if the predicate
    //   returns false.
    // The source connector is not modified but the edge may be assigned a new
    // source connector in case the connection value changed.
    auto update_connectors_and_edge = [this] (
        const auto &unit, const auto &addr, auto cond)
    {
        bool enable = cond(unit);

        auto dstCon = getInputConnector(addr);
        assert(dstCon);

        dstCon->setEnabled(enable);
        dstCon->setBrush(dstCon->isEnabled() ? Connector_Brush : Connector_Brush_Disabled);

        auto conAddr = get_connection_unit_address(m_ioCfg, addr);
        auto srcCon = getOutputConnector(conAddr);

        auto edge = getEdgeByDestConnector(dstCon);

        if (!edge)
            edge = addEdge(srcCon, dstCon);

        assert(edge);
        edge->setVisible(dstCon->isEnabled());

        // In case the connection value changed we have to update our source ->
        // dest mapping and then assign the new source to the edge.
        if (edge->sourceItem() != srcCon)
        {
            // Remove the edge from the hash
            auto it = m_edgesBySource.find(edge->sourceItem());

            while (it != m_edgesBySource.end() && it.key() == edge->sourceItem())
            {
                if (it.value() == edge)
                    it = m_edgesBySource.erase(it);
                else
                    it++;
            }

            // Set the new source item and re-add the edge to the hash.
            edge->setSourceItem(srcCon);
            m_edgesBySource.insertMulti(srcCon, edge);
        }
    };


    m_ioCfg = ioCfg;

    //
    // level0
    //

    // l0 NIM IO
    for (const auto &kv: ioCfg.l0.ioNIM | indexed(Level0::NIM_IO_Offset))
    {
        const auto &io = kv.value();
        const bool isInput = io.direction == IO::Direction::in;

        UnitAddress addr {0, static_cast<unsigned>(kv.index())};

        auto srcCon = getOutputConnector(addr);
        srcCon->setEnabled(isInput);
        srcCon->setBrush(isInput ? Connector_Brush : Connector_Brush_Disabled);

        for (auto edge: getEdgesBySourceConnector(srcCon))
        {
            edge->setVisible(io.direction == IO::Direction::in);
        }
    }

    //
    // level1
    //

    // L1.LUT2 edge update
    {
        unsigned unitIndex = 2;
        for (unsigned input=0; input < LUT_DynamicInputCount; input++)
        {
            UnitAddress addr {1, unitIndex, input};

            update_connectors_and_edge(
                ioCfg.l1.luts[unitIndex], addr, [] (const auto &) { return true; });
        }
    }

    //
    // level2
    //

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        for (unsigned input=0; input < LUT::InputBits; input++)
        {
            UnitAddress addr {2, static_cast<unsigned>(kv.index()), input};

            update_connectors_and_edge(
                kv.value(), addr, [] (const auto &) { return true; });
        }

        UnitAddress strobeGGAddress {2, static_cast<unsigned>(kv.index()), LUT::StrobeGGInput};

        update_connectors_and_edge(
            kv.value(), strobeGGAddress,
            [] (const LUT &lut)
            {
                return lut.strobedOutputs.any();
            });
    }

    //
    // level3
    //

    // level3 NIM IO
    for (const auto &kv: ioCfg.l3.ioNIM | indexed(Level3::NIM_IO_Unit_Offset))
    {
        UnitAddress addr {3, static_cast<unsigned>(kv.index())};

        auto cond = [] (const IO &io)
        {
            return io.direction == IO::Direction::out && io.activate;
        };

        update_connectors_and_edge(kv.value(), addr, cond);
    }

    // level3 ECL
    for (const auto &kv: ioCfg.l3.ioECL | indexed(Level3::ECL_Unit_Offset))
    {
        UnitAddress addr {3, static_cast<unsigned>(kv.index())};

        auto cond = [] (const IO &io)
        {
            return io.activate;
        };

        update_connectors_and_edge(kv.value(), addr, cond);
    }

    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        UnitAddress addr {3, static_cast<unsigned>(kv.index())};

        auto cond = [] (const StackStart &io)
        {
            return io.activate;
        };

        update_connectors_and_edge(kv.value(), addr, cond);
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(Level3::MasterTriggersOffset))
    {
        UnitAddress addr {3, static_cast<unsigned>(kv.index())};

        auto cond = [] (const MasterTrigger &io)
        {
            return io.activate;
        };

        update_connectors_and_edge(kv.value(), addr, cond);
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(Level3::CountersOffset))
    {
        // counter input
        {
            UnitAddress addr {3, static_cast<unsigned>(kv.index()), 0};

            auto cond = [&ioCfg, &addr] (const Counter &counter)
            {
                return counter.softActivate &&
                    // since FW0016
                    ioCfg.l3.connections[addr[1]][0] != Level3::CounterInputNotConnected;
            };

            update_connectors_and_edge(kv.value(), addr, cond);
        }

        // latch input
        {
            UnitAddress addr {3, static_cast<unsigned>(kv.index()), 1};

            auto cond = [&ioCfg, &addr] (const Counter &)
            {
                return ioCfg.l3.connections[addr[1]][1] != Level3::CounterInputNotConnected;
            };

            update_connectors_and_edge(kv.value(), addr, cond);
        }
    }

    // Post connector and edge update logic.
    // This needs to happen after the edges have been adjusted, otherwise the
    // code cannot get the correct edges via getEdgesBySourceConnector().

    // l0 timers soft_activate
    for (const auto &kv: ioCfg.l0.timers | indexed(0))
    {
        UnitAddress addr {0, static_cast<unsigned>(kv.index())};

        bool softActivate = kv.value().softActivate;

        auto srcCon = getOutputConnector(addr);
        srcCon->setEnabled(softActivate);
        srcCon->setBrush(softActivate ? Connector_Brush : Connector_Brush_Disabled);

        for (auto edge: getEdgesBySourceConnector(srcCon))
        {
            if (edge)
                edge->setVisible(edge->isVisible() && softActivate);
        }
    }

    // l1 LUTs
    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        auto minInputBits = minimize(kv.value());

        for (unsigned input = 0; input < LUT::InputBits; input++)
        {
            UnitAddress addr {1, static_cast<unsigned>(kv.index()), input};

            auto dstCon = getInputConnector(addr);
            assert(dstCon);
            dstCon->setEnabled(minInputBits.test(input));
            dstCon->setBrush(dstCon->isEnabled() ? Connector_Brush : Connector_Brush_Disabled);

            if (auto edge = getEdgeByDestConnector(dstCon))
                edge->setVisible(minInputBits.test(input) && edge->sourceItem()->isEnabled());
        }
    }

    // l2 LUTs
    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        auto minInputBits = minimize(kv.value());

        for (unsigned input = 0; input < LUT::InputBits; input++)
        {
            UnitAddress addr {2, static_cast<unsigned>(kv.index()), input};

            auto dstCon = getInputConnector(addr);
            assert(dstCon);
            dstCon->setEnabled(minInputBits.test(input));
            dstCon->setBrush(dstCon->isEnabled() ? Connector_Brush : Connector_Brush_Disabled);

            if (auto edge = getEdgeByDestConnector(dstCon))
                edge->setVisible(minInputBits.test(input) && edge->sourceItem()->isEnabled());
        }
    }

    // l3 Counters
    for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
    {
        // The logic is the same for counter and latch inputs.
        for (unsigned i=0; i<Counter::InputCount; ++i)
        {
            UnitAddress addr {3, static_cast<unsigned>(kv.index()), i};

            bool isConnected = ioCfg.l3.connections[addr[1]][i] != Level3::CounterInputNotConnected;

            bool softActivate = kv.value().softActivate;

            auto dstCon = getInputConnector(addr);
            assert(dstCon);
            dstCon->setEnabled(softActivate && isConnected);
            dstCon->setBrush(softActivate && isConnected ? Connector_Brush : Connector_Brush_Disabled);

            auto edge = getEdgeByDestConnector(dstCon);
            assert(edge);

            bool visible = edge->sourceItem() && edge->sourceItem()->isEnabled()
                && edge->destItem() && edge->destItem()->isEnabled();

            edge->setVisible(visible);
        }
    }

    auto update_names = [this] (const TriggerIO &ioCfg)
    {
        // l0 NIM
        {
            auto b = ioCfg.l0.unitNames.begin() + Level0::NIM_IO_Offset;
            auto e = b + NIM_IO_Count;
            QStringList names;

            std::copy(b, e, std::back_inserter(names));

            m_level0InputItems.nimItem->setOutputNames(names);
        }

        // l0 IRQ inputs
        {
            auto b = ioCfg.l0.unitNames.begin() + Level0::IRQ_Inputs_Offset;
            auto e = b + Level0::IRQ_Inputs_Count;
            QStringList names;

            std::copy(b, e, std::back_inserter(names));

            m_level0InputItems.irqItem->setOutputNames(names);
        }

        // l0 util
        {
            auto b = ioCfg.l0.unitNames.begin();
            auto e = b + Level0::UtilityUnitCount;
            QStringList names;

            std::copy(b, e, std::back_inserter(names));

            m_level0UtilItems.utilsItem->setOutputNames(names);
        }

        // l1 lut outputs
        {
            for (const auto &kv: ioCfg.l1.luts | indexed(0))
            {
                const auto &lut = kv.value();
                QStringList names;

                for (size_t output = 0; output < lut.outputNames.size(); output++)
                {
                    // Use short, numeric output pin names if the name equals
                    // the default. Otherwise use the modified name.
                    if (lut.defaultOutputNames[output] == lut.outputNames[output])
                    {
                        names.push_back(QString::number(output));
                    }
                    else
                    {
                        names.push_back(lut.outputNames[output]);
                    }
                }

                m_level1Items.luts[kv.index()]->setOutputNames(names);
            }
        }

        // l2 lut outputs
        {
            for (const auto &kv: ioCfg.l2.luts | indexed(0))
            {
                const auto &lut = kv.value();
                QStringList names;

                for (size_t output = 0; output < lut.outputNames.size(); output++)
                {
                    // Use short, numeric output pin names if the name equals
                    // the default. Otherwise use the modified name.
                    if (lut.defaultOutputNames[output] == lut.outputNames[output])
                        names.push_back(QString::number(output));
                    else
                        names.push_back(lut.outputNames[output]);
                }

                m_level2Items.luts[kv.index()]->setOutputNames(names);
            }
        }

        // l3 NIM
        {
            auto b = ioCfg.l3.unitNames.begin() + Level3::NIM_IO_Unit_Offset;
            auto e = b + NIM_IO_Count;
            QStringList names;

            std::copy(b, e, std::back_inserter(names));

            m_level3Items.nimItem->setInputNames(names);
        }

        // l3 ECL
        {
            auto b = ioCfg.l3.unitNames.begin() + Level3::ECL_Unit_Offset;
            auto e = b + ECL_OUT_Count;
            QStringList names;

            std::copy(b, e, std::back_inserter(names));

            m_level3Items.eclItem->setInputNames(names);
        }

        // l3 util
        {
            {
                auto b = ioCfg.l3.unitNames.begin();
                auto e = b + Level3::UtilityUnitCount;
                QStringList names;

                std::copy(b, e, std::back_inserter(names));

                m_level3UtilItems.utilsItem->setInputNames(names);
            }

            // FIXME: counters hack
            for (unsigned counter=0; counter < Level3::CountersCount; ++counter)
            {
                QString name = ioCfg.l3.unitNames.value(counter + Level3::CountersOffset);
                m_level3UtilItems.counterItems[counter]->setCounterName(name);
            }
        }
    };

    update_names(ioCfg);
}

void TriggerIOGraphicsScene::setStaticConnectionsVisible(bool visible)
{
#if 0
    std::for_each(std::begin(m_staticEdges), std::end(m_staticEdges),
                  [visible] (auto edge) { edge->setVisible(visible); });
#else
// beautiful, smelly code
    for (auto edge: m_staticEdges)
        edge->setVisible(visible);
#endif
}

void TriggerIOGraphicsScene::setConnectionBarsVisible(bool visible)
{
    for (auto bar: m_connectionBars)
        bar->setVisible(visible);
}

UnitAddress TriggerIOGraphicsScene::unitAt(const QPointF &scenePos) const
{
    auto items = this->items(scenePos);

    // level 0
    if (items.indexOf(m_level0InputItems.nimItem) >= 0)
        return { 0, Level0::NIM_IO_Offset, 0 };

    if (items.indexOf(m_level0InputItems.irqItem) >= 0)
        return { 0, Level0::IRQ_Inputs_Offset, 0 };

    if (items.indexOf(m_level0UtilItems.utilsItem) >= 0)
        return { 0, 0, 0 };

    // level1
    for (size_t unit = 0; unit < m_level1Items.luts.size(); unit++)
    {
        if (items.indexOf(m_level1Items.luts[unit]) >= 0)
            return { 1, static_cast<unsigned>(unit), 0 };
    }

    // level2
    for (size_t unit = 0; unit < m_level2Items.luts.size(); unit++)
    {
        if (items.indexOf(m_level2Items.luts[unit]) >= 0)
            return { 2, static_cast<unsigned>(unit), 0 };
    }

    // level3
    if (items.indexOf(m_level3Items.nimItem) >= 0)
        return { 3, Level3::NIM_IO_Unit_Offset, 0 };

    if (items.indexOf(m_level3Items.eclItem) >= 0)
        return { 3, Level3::ECL_Unit_Offset, 0 };

    if (items.indexOf(m_level3UtilItems.utilsItem) >= 0)
        return { 3, 0, 0 };

    throw std::runtime_error("No unit at given scene position");
}

void TriggerIOGraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev)
{
    auto items = this->items(ev->scenePos());

    // level0
    if (items.indexOf(m_level0InputItems.nimItem) >= 0)
    {
        ev->accept();
        emit editNIM_Inputs();
        return;
    }

    if (items.indexOf(m_level0InputItems.irqItem) >= 0)
    {
        ev->accept();
        emit editIRQ_Inputs();
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

    // level3
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

    if (items.indexOf(m_level3UtilItems.utilsItem) >= 0)
    {
        ev->accept();
        emit editL3Utils();
        return;
    }
}

class NanoSecondsItemDelegate: public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const override
        {
            Q_UNUSED(option);
            Q_UNUSED(index);
            auto result = new QSpinBox(parent);
            result->setMinimum(0);
            result->setMaximum(0xFFFF);
            result->setSingleStep(1);
            result->setSuffix(" ns");
            result->setSpecialValueText("off");
            return result;
        }

        QString displayText(const QVariant &value, const QLocale &locale) const override
        {
            if (value.isNull())
                return QString();

            if (value.toInt() == 0)
                return "off";

            return QStyledItemDelegate::displayText(value, locale) + " ns";;
        }
};

NIM_IO_Table_UI make_nim_io_settings_table(
    const trigger_io::IO::Direction dir)
{
    QStringList columnTitles = {
        "Activate", "Direction", "Delay", "Width", "Holdoff", "Invert", "Name", "Input", "Reset"
    };

    NIM_IO_Table_UI ret = {};

    auto table = new QTableWidget(trigger_io::NIM_IO_Count, columnTitles.size());
    ret.table = table;

    table->setHorizontalHeaderLabels(columnTitles);
    table->setItemDelegateForColumn(NIM_IO_Table_UI::ColDelay, new NanoSecondsItemDelegate(table));
    table->setItemDelegateForColumn(NIM_IO_Table_UI::ColWidth, new NanoSecondsItemDelegate(table));
    table->setItemDelegateForColumn(NIM_IO_Table_UI::ColHoldoff, new NanoSecondsItemDelegate(table));

    QObject::connect(table, &QTableWidget::itemChanged, table, [table] (QTableWidgetItem *item)
    {
        if (item->column() == NIM_IO_Table_UI::ColDelay ||
            item->column() == NIM_IO_Table_UI::ColWidth ||
            item->column() == NIM_IO_Table_UI::ColHoldoff)
        {
            auto intVal = item->data(Qt::EditRole).toInt();
            (void) intVal; // TODO: this was intended to be used for input verification, limits checks, spinbox adjustments, etc
        }

    });

    for (int row = 0; row < table->rowCount(); ++row)
    {
        table->setVerticalHeaderItem(row, new QTableWidgetItem(
                QString("NIM%1").arg(row)));

        auto combo_dir = new QComboBox;
        combo_dir->addItem("IN");
        combo_dir->addItem("OUT");

        auto check_activate = new QCheckBox;
        auto check_invert = new QCheckBox;
        auto combo_connection = new QComboBox;
        combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        auto pb_reset = new QPushButton("Reset");

        ret.combos_direction.push_back(combo_dir);
        ret.checks_activate.push_back(check_activate);
        ret.checks_invert.push_back(check_invert);
        ret.combos_connection.push_back(combo_connection);
        ret.buttons_reset.push_back(pb_reset);

        table->setCellWidget(row, 0, make_centered(check_activate));
        table->setCellWidget(row, 1, combo_dir);
        table->setCellWidget(row, 5, make_centered(check_invert));
        table->setItem(row, 6, new QTableWidgetItem(
                QString("NIM%1").arg(row)));

        table->setCellWidget(row, NIM_IO_Table_UI::ColConnection, combo_connection);
        table->setCellWidget(row, NIM_IO_Table_UI::ColReset, make_centered(pb_reset));
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
    else
        table->horizontalHeader()->hideSection(NIM_IO_Table_UI::ColConnection);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    return ret;
}

IRQ_Inputs_Table_UI make_irq_inputs_settings_table()
{
    QStringList columnTitles = {
        "Activate", "Delay", "Width", "Holdoff", "Invert", "Name", "Reset",
    };

    IRQ_Inputs_Table_UI ret = {};

    auto table = new QTableWidget(trigger_io::Level0::IRQ_Inputs_Count, columnTitles.size());
    ret.table = table;

    table->setHorizontalHeaderLabels(columnTitles);

    for (int row = 0; row < table->rowCount(); ++row)
    {
        table->setVerticalHeaderItem(row, new QTableWidgetItem(
                QString("IRQ%1").arg(row+1)));

        auto check_activate = new QCheckBox;
        auto check_invert = new QCheckBox;
        auto pb_reset = new QPushButton("Reset");

        ret.checks_activate.push_back(check_activate);
        ret.checks_invert.push_back(check_invert);
        ret.buttons_reset.push_back(pb_reset);

        table->setCellWidget(row, 0, make_centered(check_activate));
        table->setCellWidget(row, 4, make_centered(check_invert));
        table->setItem(row, 5, new QTableWidgetItem(
                QString("IRQ%1").arg(row)));
        table->setCellWidget(row, IRQ_Inputs_Table_UI::ColReset, make_centered(pb_reset));
    }

    reverse_rows(table);

    table->horizontalHeader()->moveSection(IRQ_Inputs_Table_UI::ColName, 0);
    // Hide the activate column. It might be used in future firmware revisions.
    table->horizontalHeader()->hideSection(IRQ_Inputs_Table_UI::ColActivate);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    return ret;
}

ECL_Table_UI make_ecl_table_ui(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    const QVector<std::vector<unsigned>> &inputConnections,
    const QVector<QStringList> &inputChoiceNameLists)
{
    ECL_Table_UI ui = {};

    QStringList columnTitles = {
        "Activate", "Delay", "Width", "Holdoff", "Invert", "Name", "Input", "Reset"
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
        auto pb_reset = new QPushButton("Reset");

        ui.checks_activate.push_back(check_activate);
        ui.checks_invert.push_back(check_invert);
        ui.combos_connection.push_back(combo_connection);
        ui.buttons_reset.push_back(pb_reset);

        check_activate->setChecked(settings.value(row).activate);
        check_invert->setChecked(settings.value(row).invert);
        combo_connection->addItems(inputChoiceNameLists.value(row));
        combo_connection->setCurrentIndex(inputConnections.value(row)[0]);
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
        table->setCellWidget(row, ui.ColReset, make_centered(pb_reset));
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
    const QStringList &defaultNames,
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

        connect(ui.buttons_reset[row], &QPushButton::clicked,
            this, [=]
            {
                ui.table->item(row, ui.ColName)->setText(defaultNames[row]);
                ui.table->item(row, ui.ColDelay)->setText("0");
                ui.table->item(row, ui.ColWidth)->setText("0");
                ui.table->item(row, ui.ColHoldoff)->setText("0");
                ui.combos_direction[row]->setCurrentIndex(0);
                ui.checks_activate[row]->setChecked(false);
                ui.checks_invert[row]->setChecked(false);
                if (row < ui.combos_connection.size())
                    ui.combos_connection[row]->setCurrentIndex(0);
            });
    }

    auto on_table_item_changed = [&] (QTableWidgetItem *item)
    {
        if (item->column() == ui.ColWidth)
        {
            auto intVal = item->data(Qt::EditRole).toInt();
            auto delayItem = ui.table->item(item->row(), ui.ColDelay);
            auto holdoffItem = ui.table->item(item->row(), ui.ColHoldoff);

            if (intVal == 0)
            {
                delayItem->setFlags(delayItem->flags() & ~Qt::ItemIsEnabled);
                holdoffItem->setFlags(holdoffItem->flags() & ~Qt::ItemIsEnabled);
            }
            else
            {
                delayItem->setFlags(delayItem->flags() | Qt::ItemIsEnabled);
                holdoffItem->setFlags(holdoffItem->flags() | Qt::ItemIsEnabled);
            }
        }
    };

    connect(ui.table, &QTableWidget::itemChanged, ui.table, on_table_item_changed);

    // force an initial update
    for (int row = 0; row < ui.table->rowCount(); row++)
    {
        on_table_item_changed(ui.table->item(row, ui.ColWidth));
    }

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_NIM")));

    auto explanation = new QLabel(QSL(
        "<table>"
        "  <th colspan=\"2\" align=\"left\">Gate Generator Settings:</th>"
        "  <tr>"
        "    <th align=\"left\">Delay</th>"
        "    <td>Delays the signal by the specified duration.</td>"
        "  </tr>"
        "  <tr>"
        "    <th align=\"left\">Width</th>"
        "    <td>Minimum width is 8ns. If width is set to 0 the gate generator is disabled."
        "        In this case delay and holdoff do not apply."
        "    </td>"
        "  </tr>"
        "  <tr>"
        "    <th align=\"left\">Holdoff</th>"
        "    <td>Inhibits input pulse processing until the holdoff time expires.</td>"
        "  </tr>"
        "</table>"
    ));


    explanation->setWordWrap(true);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addWidget(ui.table, 1);
    widgetLayout->addWidget(explanation);
    widgetLayout->addWidget(bb);
}

NIM_IO_SettingsDialog::NIM_IO_SettingsDialog(
    const QStringList &names,
    const QStringList &defaultNames,
    const QVector<trigger_io::IO> &settings,
    QWidget *parent)
    : NIM_IO_SettingsDialog(names, defaultNames, settings, trigger_io::IO::Direction::in, parent)
{
    m_tableUi.table->resizeColumnsToContents();
    m_tableUi.table->resizeRowsToContents();
}

NIM_IO_SettingsDialog::NIM_IO_SettingsDialog(
    const QStringList &names,
    const QStringList &defaultNames,
    const QVector<trigger_io::IO> &settings,
    const QVector<QStringList> &inputChoiceNameLists,
    const QVector<std::vector<unsigned>> &connections,
    QWidget *parent)
    : NIM_IO_SettingsDialog(names, defaultNames, settings, trigger_io::IO::Direction::out, parent)
{
    assert(m_tableUi.combos_connection.size() == m_tableUi.table->rowCount());

    for (int io = 0; io < m_tableUi.combos_connection.size(); io++)
    {
        m_tableUi.combos_connection[io]->addItems(
            inputChoiceNameLists.value(io));
        m_tableUi.combos_connection[io]->setCurrentIndex(connections.value(io)[0]);
    }

    for (int io = 0; io < m_tableUi.checks_activate.size(); io++)
    {
        auto cb_state = m_tableUi.checks_activate[io];
        auto combo_dir = m_tableUi.combos_direction[io];

        // Force the direction to 'out' if the user checks the 'activate' checkbox.
        connect(cb_state, &QCheckBox::stateChanged,
                this, [combo_dir] (int state)
                {
                    if (state == Qt::Checked)
                        combo_dir->setCurrentIndex(static_cast<int>(trigger_io::IO::Direction::out));
                });
    }

    m_tableUi.table->resizeColumnsToContents();
    m_tableUi.table->resizeRowsToContents();
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

QVector<std::vector<unsigned>> NIM_IO_SettingsDialog::getConnections() const
{
    QVector<std::vector<unsigned>> ret;

    for (auto combo: m_tableUi.combos_connection)
        ret.push_back({static_cast<unsigned>(combo->currentIndex())});

    return ret;
}

//
// IRQ_Inputs_SettingsDialog
//
IRQ_Inputs_SettingsDialog::IRQ_Inputs_SettingsDialog(
    const QStringList &names,
    const QVector<trigger_io::IO> &settings,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("IRQ Input Settings");

    m_tableUi = make_irq_inputs_settings_table();
    auto &ui = m_tableUi;

    for (int row = 0; row < ui.table->rowCount(); row++)
    {
        auto name = names.value(row);
        auto io = settings.value(row);

        ui.table->setItem(row, ui.ColName, new QTableWidgetItem(name));
        ui.table->setItem(row, ui.ColDelay, new QTableWidgetItem(QString::number(io.delay)));
        ui.table->setItem(row, ui.ColWidth, new QTableWidgetItem(QString::number(io.width)));
        ui.table->setItem(row, ui.ColHoldoff, new QTableWidgetItem(QString::number(io.holdoff)));

        ui.checks_activate[row]->setChecked(io.activate);
        ui.checks_invert[row]->setChecked(io.invert);

        connect(ui.buttons_reset[row], &QPushButton::clicked,
            this, [=]
            {
                ui.table->item(row, ui.ColName)->setText(QSL("IRQ%1").arg(row+1));
                ui.table->item(row, ui.ColDelay)->setText("0");
                ui.table->item(row, ui.ColWidth)->setText("0");
                ui.table->item(row, ui.ColHoldoff)->setText("0");
                ui.checks_invert[row]->setChecked(false);
            });
    }

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_IRQ")));

    auto widgetLayout = make_vbox(this);
    widgetLayout->addWidget(ui.table, 1);
    widgetLayout->addWidget(bb);

    m_tableUi.table->resizeColumnsToContents();
    m_tableUi.table->resizeRowsToContents();
    resize(400, 300);
}

QStringList IRQ_Inputs_SettingsDialog::getNames() const
{
    auto &ui = m_tableUi;
    QStringList ret;

    for (int row = 0; row < ui.table->rowCount(); row++)
        ret.push_back(ui.table->item(row, ui.ColName)->text());

    return ret;
}

QVector<trigger_io::IO> IRQ_Inputs_SettingsDialog::getSettings() const
{
    auto &ui = m_tableUi;
    QVector<trigger_io::IO> ret;

    for (int row = 0; row < ui.table->rowCount(); row++)
    {
        trigger_io::IO irq;

        irq.delay = ui.table->item(row, ui.ColDelay)->text().toUInt();
        irq.width = ui.table->item(row, ui.ColWidth)->text().toUInt();
        irq.holdoff = ui.table->item(row, ui.ColHoldoff)->text().toUInt();

        irq.invert = ui.checks_invert[row]->isChecked();

        ret.push_back(irq);
    }

    return ret;
}

//
// ECL_SettingsDialog
//
ECL_SettingsDialog::ECL_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<std::vector<unsigned>> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("ECL Output Settings");

    m_tableUi = make_ecl_table_ui(names, settings, inputConnections, inputChoiceNameLists);
    auto &ui = m_tableUi;

    for (int row =0; row < ui.table->rowCount(); ++row)
    {
        connect(ui.buttons_reset[row], &QPushButton::clicked,
            this, [=]
        {
            ui.table->item(row, ui.ColName)->setText(QSL("LVDS%1").arg(row+1));
            ui.table->item(row, ui.ColDelay)->setText("0");
            ui.table->item(row, ui.ColWidth)->setText("0");
            ui.table->item(row, ui.ColHoldoff)->setText("0");
            ui.checks_activate[row]->setChecked(false);
            ui.checks_invert[row]->setChecked(false);
            ui.combos_connection[row]->setCurrentIndex(0);
        });
    }

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_LVDS")));

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

QVector<std::vector<unsigned>> ECL_SettingsDialog::getConnections() const
{
    QVector<std::vector<unsigned>> ret;

    for (auto combo: m_tableUi.combos_connection)
        ret.push_back({static_cast<unsigned>(combo->currentIndex())});

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
            const QStringList &vmeEventNames,
            QWidget *parent)
    : QDialog(parent)
    , m_l0(l0)
{
    setWindowTitle("Level0 Utility Settings");

    auto make_timers_table_ui = [](const Level0 &l0)
    {
        static const QString RowTitleFormat = "Timer%1";
        static const QStringList ColumnTitles = {
            "Name", "Range", "Period", "Delay", "Soft Activate", "Reset" };
        const size_t rowCount = l0.timers.size();

        TimersTable_UI ret = {};
        ret.table = new QTableWidget(rowCount, ColumnTitles.size());
        ret.table->setHorizontalHeaderLabels(ColumnTitles);

        for (int row = 0; row < ret.table->rowCount(); ++row)
        {
            ret.table->setVerticalHeaderItem(
                row, new QTableWidgetItem(RowTitleFormat.arg(row)));

            auto combo_range = new QComboBox;
            ret.combos_range.push_back(combo_range);
            combo_range->addItem("ns", 0);
            combo_range->addItem("µs", 1);
            combo_range->addItem("ms", 2);
            combo_range->addItem("s",  3);

            combo_range->setCurrentIndex(static_cast<int>(l0.timers[row].range));

            auto cb_softActivate = new QCheckBox;
            ret.checks_softActivate.push_back(cb_softActivate);
            cb_softActivate->setChecked(l0.timers[row].softActivate);


            ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l0.unitNames.value(row)));

            ret.table->setCellWidget(row, ret.ColRange, combo_range);

            ret.table->setItem(row, ret.ColPeriod, new QTableWidgetItem(
                    QString::number(l0.timers[row].period)));

            ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(
                    QString::number(l0.timers[row].delay_ns)));

            ret.table->setCellWidget(row, ret.ColSoftActivate, make_centered(cb_softActivate));

            auto pb_reset = new QPushButton("Reset");
            ret.buttons_reset.push_back(pb_reset);
            ret.table->setCellWidget(row, ret.ColReset, make_centered(pb_reset));
            connect(pb_reset, &QPushButton::clicked, ret.table, [=] ()
                    {
                        ret.table->item(row, ret.ColName)->setText(QSL("timer%1").arg(row));
                        combo_range->setCurrentIndex(2);
                        ret.table->item(row, ret.ColPeriod)->setText("0");
                        ret.table->item(row, ret.ColDelay)->setText("0");
                        cb_softActivate->setChecked(false);
                    });
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        ret.parentWidget = new QWidget;
        auto parentLayout = make_vbox<0, 4>(ret.parentWidget);
        parentLayout->addWidget(ret.table);

        return ret;
    };

    auto make_stack_busy_table_ui = [&vmeEventNames](const Level0 &l0)
    {
        static const QString RowTitleFormat = "StackBusy%1";
        static const QStringList ColumnTitles = { "Name", "Stack#", "Reset" };
        const size_t rowCount = l0.stackBusy.size();
        const int nameOffset = l0.StackBusyOffset;

        StackBusy_UI ret = {};
        ret.table = new QTableWidget(rowCount, ColumnTitles.size());
        ret.table->setHorizontalHeaderLabels(ColumnTitles);

        for (int row = 0; row < ret.table->rowCount(); ++row)
        {
            ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

            const auto &stackBusy = l0.stackBusy[row];

            auto combo_stack = new QComboBox;
            ret.combos_stack.push_back(combo_stack);

            for (unsigned stack=0; stack < mvlc::stacks::StackCount; ++stack)
            {
                auto eventName = (stack == 0) ? "Command Stack" :  vmeEventNames.value(stack-1);
                auto str = QString::number(stack);
                if (!eventName.isEmpty())
                    str += " (" + eventName + ")";

                combo_stack->addItem(str, stack);
            }

            combo_stack->setCurrentIndex(stackBusy.stackIndex);

            ret.table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l0.unitNames.value(row + nameOffset)));

            ret.table->setCellWidget(row, ret.ColStackIndex, combo_stack);

            auto pb_reset = new QPushButton("Reset");
            ret.buttons_reset.push_back(pb_reset);
            ret.table->setCellWidget(row, ret.ColReset, make_centered(pb_reset));
            connect(pb_reset, &QPushButton::clicked, ret.table, [=] ()
                    {
                        ret.table->item(row, ret.ColName)->setText(QSL("stack_busy%1").arg(row));
                        combo_stack->setCurrentIndex(0);
                    });
        }

        ret.table->resizeColumnsToContents();
        ret.table->resizeRowsToContents();

        return ret;
    };

    auto make_trigger_resource_ui = [](const Level0 &l0)
    {
        static const QString RowTitleFormat = "TriggerResource%1";
        static const QStringList ColumnTitles =
        {
            "Name",
            "Type",
            "IRQ Index",
            "Trigger Index",
            "Delay",
            "Width",
            "Holdoff",
            "Invert",
            "Reset",
        };

        const size_t rowCount = l0.triggerResources.size();
        const int nameOffset = l0.TriggerResourceOffset;

        TriggerResource_UI ret = {};

        ret.table = new QTableWidget(rowCount, ColumnTitles.size());
        ret.table->setHorizontalHeaderLabels(ColumnTitles);
        auto table = ret.table;

        for (int row = 0; row < ret.table->rowCount(); ++row)
        {
            ret.table->setVerticalHeaderItem(row, new QTableWidgetItem(RowTitleFormat.arg(row)));

            const auto &tr = l0.triggerResources[row];

            // Type
            auto combo_type = new QComboBox;
            combo_type->addItem("IRQ", static_cast<int>(TriggerResource::Type::IRQ));
            combo_type->addItem("SoftTrigger", static_cast<int>(TriggerResource::Type::SoftTrigger));
            combo_type->addItem("SlaveTrigger", static_cast<int>(TriggerResource::Type::SlaveTrigger));
            combo_type->setCurrentIndex(combo_type->findData(static_cast<int>(tr.type)));

            // Enable/disable cells based on the resource type selection.
            auto on_type_changed = [combo_type, table, row] ()
            {
                auto update_enabled = [](QTableWidget *table, int row, int col, bool enabled)
                {
                    if (auto item = table->item(row, col))
                    {
                        if (enabled)
                            item->setFlags(item->flags() | Qt::ItemIsEnabled);
                        else
                            item->setFlags(item->flags() & (~Qt::ItemIsEnabled));
                    }
                    else if (auto widget = table->cellWidget(row, col))
                    {
                        widget->setEnabled(enabled);
                    }
                };

                using UI = TriggerResource_UI;
                using Type = TriggerResource::Type;

                auto type = static_cast<Type>(combo_type->currentData().toInt());

                update_enabled(table, row, UI::ColIRQIndex, type == Type::IRQ);
                update_enabled(table, row, UI::ColSlaveTriggerIndex, type == Type::SlaveTrigger);
                update_enabled(table, row, UI::ColDelay, type == Type::SlaveTrigger);
                update_enabled(table, row, UI::ColWidth, type == Type::SlaveTrigger);
                update_enabled(table, row, UI::ColHoldoff, type == Type::SlaveTrigger);
                update_enabled(table, row, UI::ColInvert, type == Type::SlaveTrigger);
            };

            connect(combo_type, qOverload<int>(&QComboBox::currentIndexChanged),
                    table, on_type_changed);

            // IRQ
            auto spin_irqIndex = new QSpinBox;
            spin_irqIndex->setRange(1, 16);
            spin_irqIndex->setValue(tr.irqUtil.irqIndex + 1);

            // SlaveTrigger
            auto spin_slaveTriggerIndex = new QSpinBox;
            spin_slaveTriggerIndex->setRange(0, 3);
            spin_slaveTriggerIndex->setValue(tr.slaveTrigger.triggerIndex);

            auto check_invert = new QCheckBox;
            check_invert->setChecked(tr.slaveTrigger.gateGenerator.invert);

            // Populate the table
            ret.table->setItem(row, ret.ColName, new QTableWidgetItem(l0.unitNames.value(row + nameOffset)));
            ret.table->setCellWidget(row, ret.ColType, combo_type);
            ret.table->setCellWidget(row, ret.ColIRQIndex, spin_irqIndex);
            ret.table->setCellWidget(row, ret.ColSlaveTriggerIndex, spin_slaveTriggerIndex);
            ret.table->setItem(row, ret.ColDelay, new QTableWidgetItem(QString::number(tr.slaveTrigger.gateGenerator.delay)));
            ret.table->setItem(row, ret.ColWidth, new QTableWidgetItem(QString::number(tr.slaveTrigger.gateGenerator.width)));
            ret.table->setItem(row, ret.ColHoldoff, new QTableWidgetItem(QString::number(tr.slaveTrigger.gateGenerator.holdoff)));
            ret.table->setCellWidget(row, ret.ColInvert, make_centered(check_invert));

            // Force initial type update
            on_type_changed();

            // Store input elements
            ret.combos_type.push_back(combo_type);
            ret.spins_irqIndex.push_back(spin_irqIndex);
            ret.checks_invert.push_back(check_invert);
            ret.spins_slaveTriggerIndex.push_back(spin_slaveTriggerIndex);

            auto pb_reset = new QPushButton("Reset");
            ret.buttons_reset.push_back(pb_reset);
            ret.table->setCellWidget(row, ret.ColReset, make_centered(pb_reset));
            connect(pb_reset, &QPushButton::clicked, ret.table, [=] ()
                    {
                        ret.table->item(row, ret.ColName)->setText(QSL("trigger_resource%1").arg(row));
                        combo_type->setCurrentIndex(0);
                        spin_irqIndex->setValue(1);
                        spin_slaveTriggerIndex->setValue(0);
                        ret.table->item(row, ret.ColDelay)->setText("0");
                        ret.table->item(row, ret.ColWidth)->setText("0");
                        ret.table->item(row, ret.ColHoldoff)->setText("0");
                        check_invert->setChecked(false);
                    });
        }

        return ret;
    };

    ui_timers = make_timers_table_ui(l0);
    ui_stackBusy = make_stack_busy_table_ui(l0);
    ui_triggerResources = make_trigger_resource_ui(l0);

    std::vector<Table_UI_Base *> tableUIs =
    {
        &ui_timers,
        &ui_stackBusy,
        &ui_triggerResources
    };

    for (auto ui: tableUIs)
    {
        reverse_rows(ui->table);
        ui->table->resizeColumnsToContents();
        ui->table->resizeRowsToContents();
    }

    auto grid = new QGridLayout;
    grid->addWidget(make_groupbox(ui_timers.parentWidget, "Timers"), 0, 0);
    grid->addWidget(make_groupbox(ui_stackBusy.table, "StackBusy"), 1, 0);
    grid->addWidget(make_groupbox(ui_triggerResources.table, "Trigger Resources"), 0, 1, 2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 2);

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_Timer")));

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
            unit.softActivate = ui.checks_softActivate[row]->isChecked();
        }
    }

    {
        auto &ui = ui_triggerResources;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &tr = m_l0.triggerResources[row];
            tr.type = static_cast<TriggerResource::Type>(ui.combos_type[row]->currentData().toInt());
            tr.irqUtil.irqIndex = ui.spins_irqIndex[row]->value() - 1;

            tr.slaveTrigger.triggerIndex = ui.spins_slaveTriggerIndex[row]->value();
            tr.slaveTrigger.gateGenerator.delay = ui.table->item(row, ui.ColDelay)->text().toUInt();
            tr.slaveTrigger.gateGenerator.width = ui.table->item(row, ui.ColWidth)->text().toUInt();
            tr.slaveTrigger.gateGenerator.holdoff = ui.table->item(row, ui.ColHoldoff)->text().toUInt();
            tr.slaveTrigger.gateGenerator.invert = ui.checks_invert[row]->isChecked();
        }
    }

    {
        auto &ui = ui_stackBusy;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l0.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();

            auto &unit = m_l0.stackBusy[row];
            unit.stackIndex = ui.combos_stack[row]->currentData().toUInt();
        }
    }
    return m_l0;
}

//
// Level3UtilsDialog
//
Level3UtilsDialog::Level3UtilsDialog(
    const Level3 &l3,
    const QVector<QVector<QStringList>> &inputChoiceNameLists,
    const QStringList &vmeEventNames,
    QWidget *parent)
    : QDialog(parent)
    , m_l3(l3)
{
    setWindowTitle("Level3 Utility Settings");

    auto make_ui_stack_starts = [&vmeEventNames] (
        const Level3 &l3,
        const QVector<QVector<QStringList>> inputChoiceNameLists)
    {
        StackStart_UI ret;

        QStringList columnTitles = {
            "Name", "Input", "Stack#", "Start Delay", "Activate",
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
            auto combo_stack = new QComboBox;

            ret.combos_connection.push_back(combo_connection);
            ret.checks_activate.push_back(check_activate);
            ret.combos_stack.push_back(combo_stack);

            const auto choiceNames = inputChoiceNameLists.value(row + ret.FirstUnitIndex)[0];

            // This takes into account that we have to skip over the syslock
            // entry. It's the only entry with an empty name so we skip that
            // but we still need the correct "connect value" to setup the MVLC,
            // so the connect value is stored as the data() of the combo box
            // item.
            for (int conval = 0; conval < choiceNames.size(); ++conval)
            {
                const auto &name = choiceNames.value(conval);
                if (!name.isEmpty())
                    combo_connection->addItem(name, conval);
            }

            int indexToSelect = combo_connection->findData(l3.connections[row + ret.FirstUnitIndex][0]);
            combo_connection->setCurrentIndex(indexToSelect);

            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.stackStart[row].activate);

            for (unsigned stack=1; stack < mvlc::stacks::StackCount; ++stack)
            {
                auto eventName = vmeEventNames.value(stack-1);
                auto str = QString::number(stack);
                if (!eventName.isEmpty())
                    str += " (" + eventName + ")";

                combo_stack->addItem(str, stack);
            }

            int comboIndex = 0;

            if (l3.stackStart[row].stackIndex > 0)
                comboIndex = l3.stackStart[row].stackIndex - 1;

            combo_stack->setCurrentIndex(comboIndex);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
            table->setCellWidget(row, ret.ColStack, combo_stack);

            ret.table->setItem(row, ret.ColStartDelay, new QTableWidgetItem(
                    QString::number(l3.stackStart[row].delay_ns)));

            table->setCellWidget(row, ret.ColActivate, make_centered(check_activate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto noticeLabel = new QLabel(QSL(
                "<b>Note</b>: The VME Events trigger condition has to be set to '<i>MVLC Trigger I/O</i>'"
                " for the StackStart units to have an effect.<br/>"
                "StackStart0+1 are used by the GUI to implement periodic events."
                ));
        noticeLabel->setWordWrap(true);

        ret.parentWidget = new QWidget;
        auto parentLayout = make_vbox<0, 4>(ret.parentWidget);
        parentLayout->addWidget(noticeLabel);
        parentLayout->addWidget(ret.table);

        return ret;
    };

    auto make_ui_master_triggers = [] (
        const Level3 &l3,
        const QVector<QVector<QStringList>> inputChoiceNameLists)
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

            const auto choiceNames = inputChoiceNameLists.value(row + ret.FirstUnitIndex)[0];
            // This takes into account that we have to skip over the syslock
            // entry. It's the only entry with an empty name so we skip that
            // but we still need the correct "connect value" to setup the MVLC,
            // so the connect value is stored as the data() of the combo box
            // item.
            for (int conval = 0; conval < choiceNames.size(); ++conval)
            {
                const auto &name = choiceNames.value(conval);
                if (!name.isEmpty())
                    combo_connection->addItem(name, conval);
            }

            int indexToSelect = combo_connection->findData(l3.connections[row + ret.FirstUnitIndex][0]);
            combo_connection->setCurrentIndex(indexToSelect);

            combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            check_activate->setChecked(l3.masterTriggers[row].activate);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColConnection, combo_connection);
            table->setCellWidget(row, ret.ColActivate, make_centered(check_activate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto noticeLabel = new QLabel(QSL(
            "These units produce a MasterTrigger signal when triggered. These"
            " signals are available as <i>SlaveN</i> triggers on both the master and"
            " connected slave crates."));
        noticeLabel->setWordWrap(true);

        ret.parentWidget = new QWidget;
        auto parentLayout = make_vbox<0, 4>(ret.parentWidget);
        parentLayout->addWidget(noticeLabel);
        parentLayout->addWidget(ret.table);

        return ret;
    };

    auto make_ui_counters = [] (
        const Level3 &l3,
        const QVector<QVector<QStringList>> inputChoiceNameLists)
    {
        Counters_UI ret;

        QStringList columnTitles = {
            "Name", "Counter Input", "Latch Input", "Frequency Counter Mode", "Soft Activate"
        };

        auto table = new QTableWidget(l3.counters.size(), columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        ret.table = table;

        for (int row = 0; row < table->rowCount(); ++row)
        {
            QString counterUnitName;
            if (row == 0)
                counterUnitName = QSL("Counter0+Timestamper");
            else
                counterUnitName = QSL("Counter%1").arg(row);

            table->setVerticalHeaderItem(row, new QTableWidgetItem(counterUnitName));

            auto combo_counter_connection = new QComboBox;

            ret.combos_connection.push_back(combo_counter_connection);

            combo_counter_connection->addItems(inputChoiceNameLists.value(row + ret.FirstUnitIndex)[0]);
            combo_counter_connection->setCurrentIndex(l3.connections[row + ret.FirstUnitIndex][0]);
            combo_counter_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

            auto combo_latch_connection = new QComboBox;
            ret.combos_latch_connection.push_back(combo_latch_connection);

            auto latchInputChoicesNames = inputChoiceNameLists.value(row + ret.FirstUnitIndex)[1];

            combo_latch_connection->addItems(latchInputChoicesNames);
            combo_latch_connection->setCurrentIndex(l3.connections[row + ret.FirstUnitIndex][1]);
            combo_latch_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

            auto cb_clearOnLatch = new QCheckBox;
            ret.checks_clearOnLatch.push_back(cb_clearOnLatch);
            cb_clearOnLatch->setChecked(l3.counters[row].clearOnLatch);

            auto cb_softActivate = new QCheckBox;
            ret.checks_softActivate.push_back(cb_softActivate);
            cb_softActivate->setChecked(l3.counters[row].softActivate);

            table->setItem(row, ret.ColName, new QTableWidgetItem(
                    l3.unitNames.value(row + ret.FirstUnitIndex)));
            table->setCellWidget(row, ret.ColCounterConnection, combo_counter_connection);
            table->setCellWidget(row, ret.ColLatchConnection, combo_latch_connection);
            table->setCellWidget(row, ret.ColClearOnLatch, make_centered(cb_clearOnLatch));
            table->setCellWidget(row, ret.ColSoftActivate, make_centered(cb_softActivate));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto noticeLabel = new QLabel(QSL(
                "<b>Note</b>: If '<i>Frequency Counter Mode</i>' is enabled the latch input"
                " also resets the counter to 0 after latching."));

        noticeLabel->setWordWrap(true);

        ret.parentWidget = new QWidget;
        auto parentLayout = make_vbox<0, 4>(ret.parentWidget);
        parentLayout->addWidget(noticeLabel);
        parentLayout->addWidget(ret.table);

        return ret;
    };

    ui_stackStart = make_ui_stack_starts(l3, inputChoiceNameLists);
    ui_masterTriggers = make_ui_master_triggers(l3, inputChoiceNameLists);
    ui_counters = make_ui_counters(l3, inputChoiceNameLists);

    std::array<Table_UI_Base *, 3> tableUIs =
    {
        &ui_stackStart,
        &ui_masterTriggers,
        &ui_counters
    };

    for (auto ui: tableUIs)
        reverse_rows(ui->table);

    auto grid = new QGridLayout;
    grid->addWidget(make_groupbox(ui_stackStart.parentWidget, "Stack Start"), 0, 0);
    grid->addWidget(make_groupbox(ui_masterTriggers.parentWidget, "Master Triggers"), 0, 1);
    grid->addWidget(make_groupbox(ui_counters.parentWidget, "Counters"), 1, 0, 1, 2);

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_StackStart")));

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
            m_l3.connections[row + ui.FirstUnitIndex] =
                { ui.combos_connection[row]->currentData().toUInt() };
            auto &unit = m_l3.stackStart[row];
            unit.activate = ui.checks_activate[row]->isChecked();
            unit.stackIndex = ui.combos_stack[row]->currentData().toUInt();
            unit.delay_ns = ui.table->item(row, ui.ColStartDelay)->text().toUInt();
        }
    }

    {
        auto &ui = ui_masterTriggers;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l3.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
            m_l3.connections[row + ui.FirstUnitIndex] =
                { ui.combos_connection[row]->currentData().toUInt() };
            auto &unit = m_l3.masterTriggers[row];
            unit.activate = ui.checks_activate[row]->isChecked();
        }
    }

    {
        auto &ui = ui_counters;

        for (int row = 0; row < ui.table->rowCount(); row++)
        {
            m_l3.unitNames[row + ui.FirstUnitIndex] = ui.table->item(row, ui.ColName)->text();
            m_l3.connections[row + ui.FirstUnitIndex] =
                {
                    static_cast<unsigned>(ui.combos_connection[row]->currentIndex()),
                    static_cast<unsigned>(ui.combos_latch_connection[row]->currentIndex()),
                };
            auto &unit = m_l3.counters[row];
            unit.clearOnLatch = ui.checks_clearOnLatch[row]->isChecked();
            unit.softActivate = ui.checks_softActivate[row]->isChecked();
        }
    }

    return m_l3;
}

//
// LUTOutputEditor - UI for a 6 => 1 bit LUT
//

LUTOutputEditor::LUTOutputEditor(
    const QVector<QStringList> &inputNameLists,
    const LUT_DynamicConnections &dynamicInputValues,
    QWidget *parent)
    : QWidget(parent)
    , m_outputFixedValueButton(new QPushButton(this))
    , m_outputWidgetStack(new QStackedWidget(this))
    , m_inputNameLists(inputNameLists)
{
    // LUT input bit selection
    auto table_inputs = new QTableWidget(trigger_io::LUT::InputBits, 2);
    m_inputTable = table_inputs;
    table_inputs->setHorizontalHeaderLabels({"Use", "Name" });
    table_inputs->horizontalHeader()->setStretchLastSection(true);

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

    // Output side (lower side of the widget)
    {
        auto set_and = [this] ()
        {
            for (const auto &kv: m_outputStateWidgets | indexed(0))
                kv.value()->setChecked(false);

            if (!m_outputStateWidgets.empty())
                m_outputStateWidgets.back()->setChecked(true);
        };

        auto set_or = [this] ()
        {
            for (const auto &kv: m_outputStateWidgets | indexed(0))
            {
                kv.value()->setChecked(kv.index() != 0);
            }
        };

        auto do_invert = [this] ()
        {
            for (const auto &button: m_outputStateWidgets)
                button->setChecked(!button->isChecked());
        };

        auto buttonBar = new QWidget;
        auto buttonLayout = make_hbox<0, 0>(buttonBar);

        auto buttonAnd = new QPushButton("AND");
        auto buttonOr = new QPushButton("OR");
        auto buttonInvert = new QPushButton("INVERT");

        connect(buttonAnd, &QPushButton::clicked, this, set_and);
        connect(buttonOr, &QPushButton::clicked, this, set_or);
        connect(buttonInvert, &QPushButton::clicked, this, do_invert);

        buttonLayout->addWidget(buttonAnd);
        buttonLayout->addWidget(buttonOr);
        buttonLayout->addWidget(buttonInvert);
        buttonLayout->addStretch(1);

        // Initially empty output value table. Populated in onInputUsageChanged().
        m_outputTable = new QTableWidget(0, 1);
        m_outputTable->setHorizontalHeaderLabels({"State"});
        m_outputTable->horizontalHeader()->setStretchLastSection(true);

        //auto explanation = new QLabel(QSL(
        //        "The bit values on the left side are ordered from <b>right to left</b>"
        //        ", the first selected input bit is in the rightmost position."));
        auto explanation = new QLabel(QSL(
                "Top selection table: <b>MSB top</b>, <b>LSB bottom</b>.<br/>"
                " Bits lower table left side: <b>MSB left</b>, <b>LSB right</b>"));
        explanation->setWordWrap(true);

        auto container = new QWidget;
        auto layout = make_vbox<0, 0>(container);
        layout->addWidget(buttonBar);
        layout->addWidget(m_outputTable);
        layout->addWidget(explanation);

        m_outputWidgetStack->addWidget(container);
    }

    {
        m_outputFixedValueButton->setCheckable(true);

        auto on_button_checked = [this] (bool checked)
        {
            m_outputFixedValueButton->setText(
                checked ? "Always 1" : "Always 0");
        };

        connect(m_outputFixedValueButton, &QPushButton::toggled,
                this, on_button_checked);

        on_button_checked(false);

        auto hCentered = make_centered(m_outputFixedValueButton);
        auto container = new QWidget;
        auto vTop = make_vbox<8, 2>(container);
        vTop->addWidget(hCentered);
        vTop->addStretch(1);

        m_outputWidgetStack->addWidget(container);
    }

    auto widget_outputActivation = new QWidget;
    auto layout_outputActivation = make_layout<QVBoxLayout>(widget_outputActivation);
    layout_outputActivation->addWidget(new QLabel("Output Activation"));
    layout_outputActivation->addWidget(m_outputWidgetStack);

    auto layout = make_layout<QVBoxLayout>(this);
    layout->addWidget(widget_inputSelect, 0);
    layout->addWidget(widget_outputActivation, 1);

    onInputUsageChanged();
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

    if (totalBits > 0)
    {
        m_outputWidgetStack->setCurrentIndex(0);

        unsigned rows = 1u << totalBits;
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
    }
    else
    {
        m_outputWidgetStack->setCurrentIndex(1);
    }
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

    if (bitMap.isEmpty())
    {
        // None of the input bits are selected. This means the fixed output value
        // button is shown and determines the result.
        assert(m_outputWidgetStack->currentIndex() == 1);

        if (m_outputFixedValueButton->isChecked())
            result.set();

        return result;
    }

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
    // Note: changing the checked state of any of the m_inputCheckboxes causes
    // onInputUsageChanged() to be called which in turn changes the number of
    // rows in the output state table.

    if (mapping.all() || mapping.none())
    {
        for (auto cb: m_inputCheckboxes)
            cb->setChecked(false);
        assert(m_outputStateWidgets.size() == 0);
        assert(m_outputTable->rowCount() == 0);

        m_outputFixedValueButton->setChecked(mapping.all());
    }
    else
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
    const LUT_DynamicConnections &dynConValues,
    const QStringList &outputNames,
    QWidget *parent)
    : LUTEditor(lutName, lut, inputNameLists, dynConValues, outputNames, {}, 0u, {}, {}, parent)
{
}

LUTEditor::LUTEditor(
    const QString &lutName,
    const LUT &lut,
    const QVector<QStringList> &inputNameLists,
    const LUT_DynamicConnections &dynConValues,
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
    auto scrollLayout = make_vbox<2, 2>(scrollWidget);

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
        auto lutOutputEditor = new LUTOutputEditor(inputNameLists, dynConValues);

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
            "Input", "Delay", "Holdoff"
        };

        auto &ui = m_strobeTableUi;

        auto table = new QTableWidget(1, columnTitles.size());
        table->setHorizontalHeaderLabels(columnTitles);
        table->setItemDelegateForColumn(ui.ColDelay, new NanoSecondsItemDelegate(table));
        table->setItemDelegateForColumn(ui.ColHoldoff, new NanoSecondsItemDelegate(table));
        table->verticalHeader()->hide();

        auto combo_connection = new QComboBox;

        ui.table = table;
        ui.combo_connection = combo_connection;

        combo_connection->addItems(strobeInputNames);
        combo_connection->setCurrentIndex(strobeConValue);
        combo_connection->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        table->setCellWidget(0, ui.ColConnection, combo_connection);
        table->setItem(0, ui.ColDelay, new QTableWidgetItem(QString::number(strobeSettings.delay)));
        table->setItem(0, ui.ColHoldoff, new QTableWidgetItem(QString::number(strobeSettings.holdoff)));

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto gb_strobe = new QGroupBox("Strobe Gate Generator Settings");
        auto l_strobe = make_hbox<0, 0>(gb_strobe);
        auto label_expl = new QLabel(
            "<b>Strobe</b>: LUT outputs may only change when the strobe input changes.<br/>"
            "<b>Holdoff</b>: Inhibits input pulse processing until the holdoff time expires.");
        label_expl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        l_strobe->addWidget(table, 1);
        l_strobe->addWidget(label_expl, 1);

        scrollLayout->addWidget(gb_strobe, 2);
    }

    auto bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply | QDialogButtonBox::Help,
        this);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &QDialog::accepted);
    connect(bb, &QDialogButtonBox::clicked,
            this, mvme::make_help_keyword_handler(bb, QSL("mvlc_trigger_io_LUT")));

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

LUT_DynamicConnections LUTEditor::getDynamicConnectionValues()
{
    LUT_DynamicConnections ret = {};

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

    // Create a default initialized LUT strobe GG and update it with the values
    // from the UI.
    auto ret = LUT().strobeGG;

    ret.delay = ui.table->item(0, ui.ColDelay)->text().toUInt();
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


} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io_config
