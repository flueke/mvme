/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__
#define __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGraphicsView>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>

#include <QGraphicsRectItem>

#include "mvlc/mvlc_trigger_io.h"
#include "util/qt_layouts.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_config
{

using namespace trigger_io;

class TriggerIOView: public QGraphicsView
{
    Q_OBJECT
    public:
        TriggerIOView(QGraphicsScene *scene, QWidget *parent = nullptr);

    protected:
        void wheelEvent(QWheelEvent *event);

    private:
        void scaleView(qreal scaleFactor);
};

namespace gfx
{

class ConnectorBase: public QAbstractGraphicsShapeItem
{
    public:
        ConnectorBase(QGraphicsItem *parent = nullptr)
            : QAbstractGraphicsShapeItem(parent)
        {}

        void setLabel(const QString &label)
        {
            m_label = label;
            labelSet_(label);
        }

        QString getLabel() const { return m_label; }

        void setLabelAlignment(const Qt::Alignment &align)
        {
            m_labelAlign = align;
            alignmentSet_(align);
        }

        Qt::Alignment getLabelAlignment() const { return m_labelAlign; }

    protected:
        virtual void labelSet_(const QString &label) = 0;
        virtual void alignmentSet_(const Qt::Alignment &align) = 0;

    private:
        QString m_label;
        Qt::Alignment m_labelAlign = Qt::AlignLeft;
};

class ConnectorCircleItem: public ConnectorBase
{
    public:
        static const int ConnectorRadius = 4;
        static constexpr float LabelPixelSize = 8.0f;
        static const int LabelOffset = 2;

        ConnectorCircleItem(QGraphicsItem *parent = nullptr);
        ConnectorCircleItem(const QString &label, QGraphicsItem *parent = nullptr);


        QRectF boundingRect() const override;

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *opt,
                   QWidget *widget = nullptr) override;

    protected:
        void labelSet_(const QString &label) override;
        void alignmentSet_(const Qt::Alignment &align) override;

    private:
        void adjust();

        QGraphicsSimpleTextItem *m_label = nullptr;
        QGraphicsEllipseItem *m_circle = nullptr;
};

class ConnectorDiamondItem: public ConnectorBase
{
    public:
        static const int SideLength = 12;
        static constexpr float LabelPixelSize = 8.0f;
        static const int LabelOffset = 2;

        ConnectorDiamondItem(int baseLength, QGraphicsItem *parent = nullptr);
        ConnectorDiamondItem(QGraphicsItem *parent = nullptr);

        QRectF boundingRect() const override;

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *opt,
                   QWidget *widget = nullptr) override;

    protected:
        void labelSet_(const QString &label) override;
        void alignmentSet_(const Qt::Alignment &align) override;

    private:
        void adjust();

        QGraphicsSimpleTextItem *m_label = nullptr;
        int m_baseLength = 0;
};

class ConnectableBase
{
    public:
        QVector<ConnectorBase *> inputConnectors() const
        {
            return m_inputConnectors;
        }

        QVector<ConnectorBase *> outputConnectors() const
        {
            return m_outputConnectors;
        }

        void addInputConnector(ConnectorBase *item)
        {
            m_inputConnectors.push_back(item);
        }

        void addOutputConnector(ConnectorBase *item)
        {
            m_outputConnectors.push_back(item);
        }

        ConnectorBase *getInputConnector(int index) const
        {
            return m_inputConnectors.value(index);
        }

        ConnectorBase *getOutputConnector(int index) const
        {
            return m_outputConnectors.value(index);
        }

    protected:
        QVector<ConnectorBase *> m_inputConnectors;
        QVector<ConnectorBase *> m_outputConnectors;
};

struct BlockItem: public QGraphicsRectItem, public ConnectableBase
{
    public:
        BlockItem(int width, int height,
                  int inputCount, int outputCount,
                  int inputConnectorMargin,
                  int outputConnectorMargin,
                  QGraphicsItem *parent = nullptr);

        BlockItem(int width, int height,
                  int inputCount, int outputCount,
                  int inputConnectorMarginTop, int inputConnectorMarginBottom,
                  int outputConnectorMarginTop, int outputConnectorMarginBottom,
                  QGraphicsItem *parent = nullptr);

        std::pair<int, int> getInputConnectorMargins() const
        {
            return m_inConMargins;
        }

        std::pair<int, int> getOutputConnectorMargins() const
        {
            return m_outConMargins;
        }

        void setInputNames(const QStringList &names);
        void setOutputNames(const QStringList &names);

    protected:
        void hoverEnterEvent(QGraphicsSceneHoverEvent *ev) override;
        void hoverLeaveEvent(QGraphicsSceneHoverEvent *ev) override;

    private:
        std::pair<int, int> m_inConMargins;
        std::pair<int, int> m_outConMargins;
};

struct LUTItem: public BlockItem
{
    public:
        static const int Inputs = 6;
        static const int Outputs = 3;

        static const int Width = 80;
        static const int Height = 140;

        static const int InputConnectorMargin = 16;
        static const int OutputConnectorMargin = 48;

        static const int WithStrobeInputConnectorMarginTop = InputConnectorMargin;
        static const int WithStrobeInputConnectorMarginBottom = 32;

        static const int StrobeConnectorIndex = LUT::InputBits;

        // If hasStrobeGG is set the item will have an additional diamond
        // connector for the strobe input added at the end.
        // This connector can be accessed either directly via
        // getStrobeConnector() or by using
        // TriggerIOGraphicsScene::getInputConnector({ level, unit, StrobeConnectorIndex })
        LUTItem(int lutIdx, bool hasStrobeGG = false, QGraphicsItem *parent = nullptr);

        QAbstractGraphicsShapeItem *getStrobeConnector() const
        {
            return m_strobeConnector;
        }

    private:
        ConnectorDiamondItem *m_strobeConnector = nullptr;
};

class CounterItem: public BlockItem
{
    public:
        static const int Inputs = 2;
        static const int Outputs = 0;

        static const int Width = 100;
        static const int Height = 20;

        static const int InputConnectorMargin = 6;
        static const int OutputConnectorMargin = 4;

        static constexpr float LabelPixelSize = 10.0f;

        explicit CounterItem(unsigned counterIndex, QGraphicsItem *parent = nullptr);

        void setCounterName(const QString &name);

    private:
        QGraphicsSimpleTextItem *m_labelItem = nullptr;
};

// Draw a line with an arrow at the end from the items position ({0, 0} in item
// coordinates to the endpoint in scene coordinates.
class LineAndArrow: public QAbstractGraphicsShapeItem
{
    public:
        constexpr static const qreal DefaultArrowSize = 8;

        LineAndArrow(const QPointF &scenePos, QGraphicsItem *parent = nullptr);

        void setArrowSize(qreal arrowSize);
        void setEnd(const QPointF &scenePos);

        QRectF boundingRect() const override;
        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget) override;

    protected:
        QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    private:
        QPointF getAdjustedEnd() const;
        void adjust();

        qreal m_arrowSize = DefaultArrowSize;
        QPointF m_sceneEnd;
};

// Horizontal rectangle with a connection arrow on the right side.
class HorizontalBusBar: public QGraphicsItem
{
    public:
        HorizontalBusBar(const QString &label, QGraphicsItem *parent = nullptr);

        HorizontalBusBar(QGraphicsItem *parent = nullptr)
            : HorizontalBusBar({}, parent)
        { }

        void setDestPoint(const QPointF &scenePoint);
        void setBarBrush(const QBrush &brush);
        void setLabelBrush(const QBrush &brush);
        LineAndArrow *getLineAndArrow() const;

        QRectF boundingRect() const override;

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget) override;

    private:
        QPointF m_dest;
        QGraphicsRectItem *m_bar = nullptr;
        LineAndArrow *m_arrow = nullptr;
        QGraphicsSimpleTextItem *m_label = nullptr;
};

class MovableRect: public QGraphicsRectItem
{
    public:
        MovableRect(int w, int h, QGraphicsItem *parent = nullptr);

    protected:
        QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
};

class Edge: public QAbstractGraphicsShapeItem
{
    public:
        Edge(QAbstractGraphicsShapeItem *sourceItem, QAbstractGraphicsShapeItem *destItem);

        QAbstractGraphicsShapeItem *sourceItem() const { return m_source; }
        QAbstractGraphicsShapeItem *destItem() const { return m_dest; }

        void setSourceItem(QAbstractGraphicsShapeItem *item);
        void setDestItem(QAbstractGraphicsShapeItem *item);

        void adjust();

        QRectF boundingRect() const override;

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget) override;

    private:
        QAbstractGraphicsShapeItem *m_source,
                                   *m_dest;

        QPointF m_sourcePoint,
                m_destPoint;

        qreal m_arrowSize;
};

} // end namespace gfx

class TriggerIOGraphicsScene: public QGraphicsScene
{
    Q_OBJECT
    signals:
        void editLUT(int level, int unit);
        void editNIM_Inputs();
        void editIRQ_Inputs();
        void editNIM_Outputs();
        void editECL_Outputs();
        void editL0Utils();
        void editL3Utils();

    public:
        TriggerIOGraphicsScene(
            const TriggerIO &ioCfg,
            QObject *parent = nullptr);

        void setTriggerIOConfig(const TriggerIO &ioCfg);
        void setStaticConnectionsVisible(bool visible);
        void setConnectionBarsVisible(bool visible);

        // throws std::runtime_error() if no valid unit is at the given scenePos.
        UnitAddress unitAt(const QPointF &pos) const;

    protected:
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev) override;

    private:
        struct Level0InputItems
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            gfx::BlockItem *nimItem;
            gfx::BlockItem *irqItem; // New in FW0016

        };

        struct Level0UtilItems
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            gfx::BlockItem *utilsItem;
        };

        struct Level1Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            std::array<gfx::LUTItem *, trigger_io::Level1::LUTCount> luts;
        };

        struct Level2Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            std::array<gfx::LUTItem *, trigger_io::Level2::LUTCount> luts;
        };

        struct Level3Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            gfx::BlockItem *nimItem;
            gfx::BlockItem *eclItem;
        };

        struct Level3UtilItems
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            gfx::BlockItem *utilsItem;
            QVector<gfx::CounterItem *> counterItems;
        };

        QAbstractGraphicsShapeItem *getInputConnector(const UnitAddress &addr) const;
        QAbstractGraphicsShapeItem *getOutputConnector(const UnitAddress &addr) const;

        gfx::Edge *addEdge(QAbstractGraphicsShapeItem *sourceConnector,
                           QAbstractGraphicsShapeItem *destConnector);

        QList<gfx::Edge *> getEdgesBySourceConnector(
            QAbstractGraphicsShapeItem *sourceConnector) const;

        gfx::Edge * getEdgeByDestConnector(
            QAbstractGraphicsShapeItem *destConnector) const;

        gfx::Edge *addStaticConnectionEdge(QAbstractGraphicsShapeItem *sourceConnector,
                                           QAbstractGraphicsShapeItem *destConnector);

        TriggerIO m_ioCfg;

        Level0InputItems m_level0InputItems;
        Level0UtilItems m_level0UtilItems;
        Level1Items m_level1Items;
        Level2Items m_level2Items;
        Level3Items m_level3Items;
        Level3UtilItems m_level3UtilItems;

        // Flat list of all connection edges. This includes edges from dynamic
        // and static connections. By default the edges are hidden unless the
        // logic determines that the particular edge is considered to be in
        // use.
        // Note: this does not contain the edges added via
        // addStaticConnectionEdge(). Those are used for the purpose of drawing
        // all hardwired and possible connections when the user wants to see
        // them.
        QVector<gfx::Edge *> m_edges;
        // Input Connector -> Edge
        QHash<QAbstractGraphicsShapeItem *, gfx::Edge *> m_edgesByDest;
        // Output Connector -> Edge list
        QHash<QAbstractGraphicsShapeItem *, gfx::Edge *> m_edgesBySource;

        // Used to show all hardwired connections no matter if they are
        // considered to be in use or not.
        QVector<gfx::Edge *> m_staticEdges;

        QVector<QGraphicsItem *> m_connectionBars;
};

struct NIM_IO_Table_UI
{
    enum Columns
    {
        ColActivate,
        ColDirection,
        ColDelay,
        ColWidth,
        ColHoldoff,
        ColInvert,
        ColName,
        ColConnection,
        ColReset,
    };

    QTableWidget *table;
    QVector<QComboBox *> combos_direction;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
    QVector<QComboBox *> combos_connection;
    QVector<QPushButton *> buttons_reset;
};

struct IRQ_Inputs_Table_UI
{
    enum Columns
    {
        ColActivate,
        ColDelay,
        ColWidth,
        ColHoldoff,
        ColInvert,
        ColName,
        ColReset,
    };

    QTableWidget *table;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
    QVector<QComboBox *> combos_connection;
    QVector<QPushButton *> buttons_reset;
};

struct ECL_Table_UI
{
    enum Columns
    {
        ColActivate,
        ColDelay,
        ColWidth,
        ColHoldoff,
        ColInvert,
        ColName,
        ColConnection,
        ColReset
    };

    QTableWidget *table;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
    QVector<QComboBox *> combos_connection;
    QVector<QPushButton *> buttons_reset;
};

NIM_IO_Table_UI make_nim_io_settings_table(
    const trigger_io::IO::Direction dir = trigger_io::IO::Direction::in);

class NIM_IO_SettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        // Use this when editing NIMs on Level0 (to be used as inputs)
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QStringList &defaultNames,
            const QVector<trigger_io::IO> &settings,
            QWidget *parent = nullptr);

        // Use this when editing NIMs on Level3 (to be used as outputs)
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QStringList &defaultNames,
            const QVector<trigger_io::IO> &settings,
            const QVector<QStringList> &inputChoiceNameLists,
            const QVector<std::vector<unsigned>> &connections,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<std::vector<unsigned>> getConnections() const;

    private:
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QStringList &defaultNames,
            const QVector<trigger_io::IO> &settings,
            const trigger_io::IO::Direction &dir,
            QWidget *parent = nullptr);

        NIM_IO_Table_UI m_tableUi;
};

class IRQ_Inputs_SettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        IRQ_Inputs_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<std::vector<unsigned>> getConnections() const;

    private:
        IRQ_Inputs_Table_UI m_tableUi;
};

class ECL_SettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        ECL_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<std::vector<unsigned>> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<std::vector<unsigned>> getConnections() const;

    private:
        ECL_Table_UI m_tableUi;
};

class Level0UtilsDialog: public QDialog
{
    Q_OBJECT
    public:
        Level0UtilsDialog(
            const Level0 &l0,
            const QStringList &vmeEventNames,
            QWidget *parent = nullptr);

        Level0 getSettings() const;

    private:
        struct Table_UI_Base
        {
            QTableWidget *table;
        };

        struct TimersTable_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColRange,
                ColPeriod,
                ColDelay,
                ColSoftActivate,
                ColReset,
            };

            static const int FirstUnitIndex = 0;

            QWidget *parentWidget;
            QVector<QComboBox *> combos_range;
            QVector<QCheckBox *> checks_softActivate;
            QVector<QPushButton *> buttons_reset;
        };

        struct TriggerResource_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColType,
                ColIRQIndex,
                ColSlaveTriggerIndex,
                ColDelay,
                ColWidth,
                ColHoldoff,
                ColInvert,
                ColReset,
            };

            static const int FirstUnitIndex = Level0::TriggerResourceOffset;

            QVector<QComboBox *> combos_type;
            QVector<QSpinBox *> spins_irqIndex;
            QVector<QCheckBox *> checks_invert;
            QVector<QSpinBox *> spins_slaveTriggerIndex;
            QVector<QPushButton *> buttons_reset;
        };

        struct StackBusy_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColStackIndex,
                ColReset,
            };

            static const int FirstUnitIndex = Level0::StackBusyOffset;

            QVector<QComboBox *> combos_stack;
            QVector<QPushButton *> buttons_reset;
        };


        mutable Level0 m_l0;
        TimersTable_UI ui_timers;
        StackBusy_UI ui_stackBusy;
        TriggerResource_UI ui_triggerResources;
};

class Level3UtilsDialog: public QDialog
{
    Q_OBJECT
    public:
        // Note: the Level3 structure is copied, then modified and returned in
        // getSettings(). This means the NIM and ECL settings are passed
        // through unmodified.
        Level3UtilsDialog(
            const Level3 &l3,
            const QVector<QVector<QStringList>> &inputChoiceNameLists,
            const QStringList &vmeEventNames,
            QWidget *parent = nullptr);

        Level3 getSettings() const;

    private:
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
                ColStartDelay,
                ColActivate,
            };

            static const int FirstUnitIndex = 0;

            QWidget *parentWidget;
            QVector<QComboBox *> combos_stack;
        };

        struct MasterTriggers_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColConnection,
                ColActivate,
            };

            static const int FirstUnitIndex = 4;
        };

        struct Counters_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColCounterConnection,
                ColLatchConnection,
                ColClearOnLatch,
                ColSoftActivate,
            };

            static const int FirstUnitIndex = 8;

            QWidget *parentWidget;
            QVector<QCheckBox *> checks_clearOnLatch;
            QVector<QCheckBox *> checks_softActivate;
            QVector<QComboBox *> combos_latch_connection;
        };

        mutable Level3 m_l3;
        StackStart_UI ui_stackStart;
        MasterTriggers_UI ui_masterTriggers;
        Counters_UI ui_counters;
};

class LUTOutputEditor: public QWidget
{
    Q_OBJECT
    public:
        LUTOutputEditor(
            const QVector<QStringList> &inputNameLists = {},
            const LUT_DynamicConnections &dynConValues = {},
            QWidget *parent = nullptr);

        // LUT mapping for the output bit being edited
        LUT::Bitmap getOutputMapping() const;
        void setOutputMapping(const LUT::Bitmap &mapping);

    public slots:
        void setInputConnection(unsigned input, unsigned value);

    private slots:
        void onInputUsageChanged();

    private:
        QVector<unsigned> getInputBitMapping() const;

        QTableWidget *m_inputTable;
        QVector<QCheckBox *> m_inputCheckboxes;

        QTableWidget *m_outputTable;
        QVector<QPushButton *> m_outputStateWidgets;
        QPushButton *m_outputFixedValueButton;
        QStackedWidget *m_outputWidgetStack;
        QVector<QStringList> m_inputNameLists;
};

class LUTEditor: public QDialog
{
    Q_OBJECT
    public:
        // LUT without strobe inputs
        LUTEditor(
            const QString &lutName,
            const LUT &lut,
            const QVector<QStringList> &inputNameLists,
            const QStringList &outputNames,
            QWidget *parent = nullptr);

        // LUT with dynamic input choices but no strobe
        LUTEditor(
            const QString &lutName,
            const LUT &lut,
            const QVector<QStringList> &inputNameLists,
            const LUT_DynamicConnections &dynConValues,
            const QStringList &outputNames,
            QWidget *parent = nullptr);

        // LUT with strobe inputs
        LUTEditor(
            const QString &lutName,
            const LUT &lut,
            const QVector<QStringList> &inputNameLists,
            const LUT_DynamicConnections &dynConValues,
            const QStringList &outputNames,
            const QStringList &strobeInputChoiceNames,
            unsigned strobeConValue,
            const trigger_io::IO &strobeSettings,
            const std::bitset<trigger_io::LUT::OutputBits> strobedOutputs,
            QWidget *parent = nullptr);

        LUT::Contents getLUTContents() const;
        QStringList getOutputNames() const;
        LUT_DynamicConnections getDynamicConnectionValues();

        unsigned getStrobeConnectionValue();
        trigger_io::IO getStrobeSettings();
        std::bitset<trigger_io::LUT::OutputBits> getStrobedOutputMask();

    private:
        struct StrobeTable_UI
        {
            enum Columns
            {
                ColConnection,
                ColDelay,
                ColHoldoff,
            };

            QTableWidget *table;
            QComboBox *combo_connection;
        };

        QVector<QComboBox *> m_inputSelectCombos;
        QVector<LUTOutputEditor *> m_outputEditors;
        QVector<QLineEdit *> m_outputNameEdits;
        QVector<QCheckBox *> m_strobeCheckboxes;
        StrobeTable_UI m_strobeTableUi;
};

// How many timer/stack start units are marked as reserved in the UI. This is a
// quick hack to indicate to the user that these units are used internally by
// mvme to setup periodic stacks.
static const int ReservedTimerUnits = 2;
static const int ReservedStackStartUnits = 2;

} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__ */
