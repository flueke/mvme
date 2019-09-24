#ifndef __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__
#define __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__

#include <QGraphicsView>
#include <QTableWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>

#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/mvlc_trigger_io_2.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
{

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

class TriggerIOGraphicsScene: public QGraphicsScene
{
    Q_OBJECT
    signals:
        void editLUT(int level, int unit);
        void editNIM_Inputs();
        void editNIM_Outputs();
        void editECL_Outputs();
        void editL0Utils();
        void editL3Utils();

    public:
        TriggerIOGraphicsScene(QObject *parent = nullptr);

    protected:
        virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev) override;

    private:
        struct Level0Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            QGraphicsRectItem *nimItem;

        };

        struct Level0UtilItems
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            QGraphicsRectItem *utilsItem;
        };

        struct Level1Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            std::array<QGraphicsItem *, trigger_io::Level1::LUTCount> luts;
        };

        struct Level2Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            std::array<QGraphicsItem *, trigger_io::Level2::LUTCount> luts;
        };

        struct Level3Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            QGraphicsRectItem *nimItem;
            QGraphicsRectItem *eclItem;
            QGraphicsRectItem *utilsItem;

        };

        Level0Items m_level0Items;
        Level1Items m_level1Items;
        Level2Items m_level2Items;
        Level3Items m_level3Items;

        Level0UtilItems m_level0UtilItems;
};

class IOSettingsWidget: public QWidget
{
    Q_OBJECT
    public:
        IOSettingsWidget(QWidget *parent = nullptr);
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
        ColConnection
    };

    QTableWidget *table;
    QVector<QComboBox *> combos_direction;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
    QVector<QComboBox *> combos_connection;
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
        ColConnection
    };

    QTableWidget *table;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
    QVector<QComboBox *> combos_connection;
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
            const QVector<trigger_io::IO> &settings,
            QWidget *parent = nullptr);

        // Use this when editing NIMs on Level3 (to be used as outputs)
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<QStringList> &inputChoiceNameLists,
            const QVector<unsigned> &connections,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<unsigned> getConnections() const;

    private:
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const trigger_io::IO::Direction &dir,
            QWidget *parent = nullptr);

        NIM_IO_Table_UI m_tableUi;
};

class ECL_SettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        ECL_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<unsigned> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<unsigned> getConnections() const;

    private:
        ECL_Table_UI m_tableUi;
};

class Level0UtilsDialog: public QDialog
{
    Q_OBJECT
    public:
        Level0UtilsDialog(
            const Level0 &l0,
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
            };

            static const int FirstUnitIndex = 0;

            QVector<QComboBox *> combos_range;
        };

        struct IRQUnits_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColIRQIndex,
            };

            static const int FirstUnitIndex = Level0::IRQ_UnitOffset;

            QVector<QSpinBox *> spins_irqIndex;
        };

        struct SoftTriggers_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
            };

            static const int FirstUnitIndex = Level0::SoftTriggerOffset;
        };

        struct SlaveTriggers_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColDelay,
                ColWidth,
                ColHoldoff,
                ColInvert,
            };

            static const int FirstUnitIndex = Level0::SlaveTriggerOffset;

            QVector<QCheckBox *> checks_invert;
        };

        struct StackBusy_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColStackIndex,
            };

            static const int FirstUnitIndex = Level0::StackBusyOffset;

            QVector<QSpinBox *> spins_stackIndex;
        };

        mutable Level0 m_l0;
        TimersTable_UI ui_timers;
        IRQUnits_UI ui_irqUnits;
        SoftTriggers_UI ui_softTriggers;
        SlaveTriggers_UI ui_slaveTriggers;
        StackBusy_UI ui_stackBusy;
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
            const QVector<QStringList> &inputChoiceNameLists,
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
                ColActivate,
            };

            static const int FirstUnitIndex = 0;

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

            static const int FirstUnitIndex = 4;
        };

        struct Counters_UI: public Table_UI_Base
        {
            enum Columns
            {
                ColName,
                ColConnection,
            };

            static const int FirstUnitIndex = 8;
        };

        mutable Level3 m_l3;
        StackStart_UI ui_stackStart;
        MasterTriggers_UI ui_masterTriggers;
        Counters_UI ui_counters;
};

class LUTOutputEditor: public QWidget
{
    Q_OBJECT
    signals:
        void inputConnectionChanged(unsigned input, unsigned value);

    public:
        LUTOutputEditor(
            int outputNumber,
            const QVector<QStringList> &inputNameLists = {},
            const LUT_DynConValues &dynConValues = {},
            QWidget *parent = nullptr);

        // LUT mapping for the output bit being edited
        OutputMapping getOutputMapping() const;
        void setOutputMapping(const OutputMapping &mapping);

        LUT_DynConValues getDynamicConnectionValues() const;

    public slots:
        void setInputConnection(unsigned input, unsigned value);

    private slots:
        void onInputUsageChanged();

    private:
        QVector<unsigned> getInputBitMapping() const;

        QVector<QCheckBox *> m_inputCheckboxes;
        QVector<QComboBox *> m_inputConnectionCombos;
        QTableWidget *m_outputTable;
        QVector<QPushButton *> m_outputStateWidgets;
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

        // LUT with strobe inputs
        LUTEditor(
            const QString &lutName,
            const LUT &lut,
            const QVector<QStringList> &inputNameLists,
            const LUT_DynConValues &dynConValues,
            const QStringList &outputNames,
            const QStringList &strobeInputChoiceNames,
            unsigned strobeConValue,
            const trigger_io::IO &strobeSettings,
            const std::bitset<trigger_io::LUT::OutputBits> strobedOutputs,
            QWidget *parent = nullptr);

        LUT::Contents getLUTContents() const;
        QStringList getOutputNames() const;
        LUT_DynConValues getDynamicConnectionValues();

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
                ColWidth,
                ColHoldoff,
            };

            QTableWidget *table;
            QComboBox *combo_connection;
        };

        QVector<LUTOutputEditor *> m_outputEditors;
        QVector<QLineEdit *> m_outputNameEdits;
        QVector<QCheckBox *> m_strobeCheckboxes;
        StrobeTable_UI m_strobeTableUi;
};

QWidget *make_centered(QWidget *widget);

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_MVLC_TRIGGER_IO_EDITOR_P_H__ */
