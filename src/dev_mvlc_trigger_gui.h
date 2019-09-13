#ifndef __MVME_DEV_MVLC_TRIGGER_GUI_H__
#define __MVME_DEV_MVLC_TRIGGER_GUI_H__

#include <QGraphicsView>
#include <QDialog>
#include <QtWidgets>
#include <bitset>

#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvlc
{
namespace trigger_io_config
{

// Addressing: level, unit [, subunit]
// subunit used to address LUT outputs in levels 1 and 2
using UnitAddress = std::array<unsigned, 3>;

struct UnitConnection
{
    static UnitConnection makeDynamic(bool available = true)
    {
        UnitConnection res{0, 0, 0};
        res.isDynamic = true;
        res.isAvailable = available;
        return res;
    }

    UnitConnection(unsigned level, unsigned unit, unsigned subunit = 0)
        : address({level, unit, subunit})
    {}

    unsigned level() const { return address[0]; }
    unsigned unit() const { return address[1]; }
    unsigned subunit() const { return address[2]; }

    unsigned operator[](size_t index) const { return address[index]; }
    unsigned &operator[](size_t index) { return address[index]; }

    UnitConnection(const UnitConnection &other) = default;
    UnitConnection &operator=(const UnitConnection &other) = default;

    bool isDynamic = false;
    bool isAvailable = true;
    UnitAddress address = {};
};

using OutputMapping = std::bitset<trigger_io::LUT::InputCombinations>;

using LUT_Connections = std::array<UnitConnection, trigger_io::LUT::InputBits>;

static const size_t Level2LUT_VariableInputCount = 3;
using LUT_DynConValues = std::array<unsigned, Level2LUT_VariableInputCount>;

struct LUT
{
    std::array<OutputMapping, trigger_io::LUT::OutputBits> lutContents;
    std::array<QString, trigger_io::LUT::OutputBits> outputNames;

    // Strobe gate generator settings
    trigger_io::IO strobeGG;
    std::bitset<trigger_io::LUT::OutputBits> strobedOutputs;
};

struct Level0: public trigger_io::Level0
{
    static const std::array<QString, trigger_io::Level0::OutputCount> DefaultUnitNames;

    QStringList outputNames;

    Level0();
};

struct Level1
{
    static const std::array<LUT_Connections, trigger_io::Level1::LUTCount> StaticConnections;
    //static const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> OutputPinMapping;

    std::array<LUT, trigger_io::Level1::LUTCount> luts;

    Level1();
};

// TODO: merge with trigger_io::Level2
struct Level2
{
    static const std::array<LUT_Connections, trigger_io::Level2::LUTCount> StaticConnections;
    //static const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> OutputPinMapping;

    std::array<LUT, trigger_io::Level2::LUTCount> luts;

    // The first 3 inputs of each LUT have dynamic connections. The selected
    // value is stored here.
    std::array<LUT_DynConValues, trigger_io::Level2::LUTCount> lutDynConValues;
    std::array<unsigned, trigger_io::Level2::LUTCount> strobeDynConValues;

    Level2();
};

using UnitAddressVector = std::vector<UnitAddress>;

// Input choices for a single lut on level 2
struct Level2LUTDynamicInputChoices
{
    std::vector<UnitAddressVector> inputChoices;
    UnitAddressVector strobeInputChoices;
};

Level2LUTDynamicInputChoices make_level2_input_choices(unsigned unit);

struct Level3: public trigger_io::Level3
{
    static const std::array<QString, trigger_io::Level3::UnitCount> DefaultUnitNames;
    // A list of possible input addresses for each level 3 input pin.
    const std::vector<UnitAddressVector> dynamicInputChoiceLists;

    QStringList unitNames;

    Level3();
};

struct Config
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
    Level3 l3;
};

QString lookup_name(const Config &cfg, const UnitAddress &addr);

class TriggerIOView: public QGraphicsView
{
    Q_OBJECT
    public:

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
        // Use this when editing NIMs on Level0
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            QWidget *parent = nullptr);

        // Use this when editing NIMs on Level3
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            const QVector<QStringList> &inputChoiceNameLists,
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
            const QVector<int> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;
        QVector<unsigned> getConnections() const;

    private:
        ECL_Table_UI m_tableUi;
};

struct TimersTable_UI
{
    enum Columns
    {
        ColName,
        ColRange,
        ColPeriod,
        ColDelay,
    };

    QTableWidget *table;
    QVector<QComboBox *> combos_range;
};

struct IRQUnits_UI
{
    enum Columns
    {
        ColName,
        ColIRQIndex,
    };

    QTableWidget *table;
    QVector<QSpinBox *> spins_irqIndex;
};

struct SoftTriggers_UI
{
    enum Columns
    {
        ColName,
    };

    QTableWidget *table;
};

struct SlaveTriggers_UI
{
    enum Columns
    {
        ColName,
        ColDelay,
        ColWidth,
        ColHoldoff,
        ColInvert,
    };

    QTableWidget *table;
    QVector<QCheckBox *> checks_invert;
};

struct StackBusy_UI
{
    enum Columns
    {
        ColName,
        ColStackIndex,
    };

    QTableWidget *table;
    QVector<QSpinBox *> spins_stackIndex;
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
        TimersTable_UI m_timersUi;
        IRQUnits_UI m_irqUnitsUi;
        SoftTriggers_UI m_softTriggersUi;
        SlaveTriggers_UI m_slaveTriggersUi;
        StackBusy_UI m_stackBusyUi;
};

class Level3UtilsDialog: public QDialog
{
    Q_OBJECT
    public:
        Level3UtilsDialog(
            const QStringList &names,
            const Level3 &l3,
            const QVector<unsigned> &inputConnections,
            const QVector<QStringList> &inputChoiceNameLists,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        Level3 getSettings() const;
        QVector<unsigned> getConnections() const;
};

struct InputSpec
{
    UnitAddress source;
    bool isAvailable;
    bool isStatic;
    QString name;
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
            const QVector<QStringList> &inputNameLists,
            const QStringList &outputNames,
            QWidget *parent = nullptr);

        // LUT with strobe inputs
        LUTEditor(
            const QString &lutName,
            const QVector<QStringList> &inputNameLists,
            const LUT_DynConValues &dynConValues,
            const QStringList &outputNames,
            const QStringList &strobeInputChoiceNames,
            unsigned strobeConValue,
            const trigger_io::IO &strobeSettings,
            const std::bitset<trigger_io::LUT::OutputBits> strobedOutputs,
            QWidget *parent = nullptr);

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
                ColInvert,
            };

            QTableWidget *table;
            QComboBox *combo_connection;
            QCheckBox *check_invert;
        };

        QVector<LUTOutputEditor *> m_outputEditors;
        QVector<QLineEdit *> m_outputNameEdits;
        QVector<QCheckBox *> m_strobeCheckboxes;
        StrobeTable_UI m_strobeTableUi;
};

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_DEV_MVLC_TRIGGER_GUI_H__ */
