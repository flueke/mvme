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

struct LUT
{
    std::array<OutputMapping, trigger_io::LUT::OutputBits> lutContents;
    std::array<QString, trigger_io::LUT::OutputBits> outputNames;
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

struct Level2
{
    static const std::array<LUT_Connections, trigger_io::Level2::LUTCount> StaticConnections;
    //static const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> OutputPinMapping;

    std::array<LUT, trigger_io::Level2::LUTCount> luts;
    //std::array<LUT_Connections, trigger_io::Level2::LUTCount> connections;

    Level2();
};

using UnitAddressVector = std::vector<UnitAddress>;

struct Level2LUTsDynamicInputChoices
{
    std::vector<UnitAddressVector> inputChoices;
    UnitAddressVector strobeInputChoices;
};

Level2LUTsDynamicInputChoices make_level2_input_choices(unsigned unit);

struct Level3
{
    static const std::array<QString, trigger_io::Level3::UnitCount> UnitNames;

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
        void editNIM_IOs();

    public:
        TriggerIOGraphicsScene(QObject *parent = nullptr);

    protected:
        virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev) override;

    private:
        struct Level0Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            QGraphicsRectItem *nim_io_item;

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
            QGraphicsRectItem *ioItem;

        };

        Level0Items m_level0Items;
        Level1Items m_level1Items;
        Level2Items m_level2Items;
        Level3Items m_level3Items;
};

class IOSettingsWidget: public QWidget
{
    Q_OBJECT
    public:
        IOSettingsWidget(QWidget *parent = nullptr);
};

struct NIM_IO_Table_UI
{
    static const int ColDelay = 2;
    static const int ColWidth = 3;
    static const int ColHoldoff = 4;
    static const int ColName = 6;

    QTableWidget *table;
    QVector<QComboBox *> combos_direction;
    QVector<QCheckBox *> checks_activate;
    QVector<QCheckBox *> checks_invert;
};

NIM_IO_Table_UI make_nim_io_settings_table();

class NIM_IO_SettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        NIM_IO_SettingsDialog(
            const QStringList &names,
            const QVector<trigger_io::IO> &settings,
            QWidget *parent = nullptr);

        QStringList getNames() const;
        QVector<trigger_io::IO> getSettings() const;

    private:
        NIM_IO_Table_UI m_tableUi;
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
            QWidget *parent = nullptr);

        OutputMapping getOutputMapping() const;
        void setOutputMapping(const OutputMapping &mapping);

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
        QString m_outputName;
};

class LUTEditor: public QDialog
{
    Q_OBJECT
    public:
        LUTEditor(
            const QString &lutName = {},
            const QVector<QStringList> &inputNameLists = {},
            const QStringList &outputNames = {},
            QWidget *parent = nullptr);

        QStringList getOutputNames() const;

    private:
        QVector<LUTOutputEditor *> m_outputEditors;
        QVector<QLineEdit *> m_outputNameEdits;
};

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_DEV_MVLC_TRIGGER_GUI_H__ */
