#ifndef __MVME_DEV_MVLC_TRIGGER_GUI_H__
#define __MVME_DEV_MVLC_TRIGGER_GUI_H__

#include <QGraphicsView>
#include <QDialog>
#include <QtWidgets>
#include <bitset>

#include "mvlc/mvlc_trigger_io.h"

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

class LUTOutputEditor: public QWidget
{
    Q_OBJECT
    public:
        using OutputMapping = std::bitset<mesytec::mvlc::trigger_io::LUT::InputCombinations>;

        LUTOutputEditor(QWidget *parent = nullptr);

        OutputMapping getOutputMapping() const;

    private slots:
        void onInputSelectionChanged();

    private:
        QVector<unsigned> getInputBitMapping() const;

        QVector<QCheckBox *> m_inputCheckboxes;
        QTableWidget *m_outputTable;
        QVector<QCheckBox *> m_outputCheckboxes;
};

class LUTEditor: public QDialog
{
    Q_OBJECT
    public:
        LUTEditor(QWidget *parent = nullptr);
};

#endif /* __MVME_DEV_MVLC_TRIGGER_GUI_H__ */
