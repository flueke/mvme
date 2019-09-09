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

using OutputMapping = std::bitset<trigger_io::LUT::InputCombinations>;

using LUT_Connections = std::array<UnitAddress, trigger_io::LUT::InputBits>;

struct LUT
{
    std::array<OutputMapping, trigger_io::LUT::OutputBits> lutContents;
    std::array<QString, trigger_io::LUT::OutputBits> outputNames;
};

struct Level0
{
    static const std::array<QString, trigger_io::Level0::OutputCount> DefaultOutputNames;
    static const std::array<UnitAddress, trigger_io::NIM_IO_Count> OutputPinMapping;

    QStringList outputNames;

    Level0();
};

struct Level1
{
    static const std::array<LUT_Connections, trigger_io::Level1::LUTCount> StaticConnections;
    static const std::array<UnitAddress, 2 * trigger_io::LUT::OutputBits> OutputPinMapping;

    std::array<LUT, trigger_io::Level1::LUTCount> luts;
};

struct Level2
{
    std::array<LUT, trigger_io::Level2::LUTCount> luts;
};

struct Config
{
    Level0 l0;
    Level1 l1;
    Level2 l2;
};

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

    public:
        TriggerIOGraphicsScene(QObject *parent = nullptr);

    protected:
        virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev) override;

    private:
        struct Level1Items
        {
            QGraphicsRectItem *parent;
            QGraphicsSimpleTextItem *label;
            std::array<QGraphicsItem *, 5> luts;
        };

        Level1Items m_level1Items;
};

class IOSettingsWidget: public QWidget
{
    Q_OBJECT
    public:
        IOSettingsWidget(QWidget *parent = nullptr);
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
    public:
        LUTOutputEditor(
            int outputNumber,
            const QStringList &inputNames = {},
            QWidget *parent = nullptr);

        OutputMapping getOutputMapping() const;
        void setOutputMapping(const OutputMapping &mapping);

    private slots:
        void onInputSelectionChanged();

    private:
        QVector<unsigned> getInputBitMapping() const;

        QVector<QCheckBox *> m_inputCheckboxes;
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
            const QStringList &inputNames = {},
            const QStringList &outputNames = {},
            QWidget *parent = nullptr);

    private:
        QVector<LUTOutputEditor *> m_outputEditors;
        QVector<QLineEdit *> m_outputNameEdits;
};

} // end namespace mvlc
} // end namespace mesytec
} // end namespace trigger_io_config

#endif /* __MVME_DEV_MVLC_TRIGGER_GUI_H__ */
