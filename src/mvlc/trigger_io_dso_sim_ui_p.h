#ifndef __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__

#include <QStandardItemModel>

#include "mvlc/trigger_io_dso_sim_ui.h"
#include "mvlc/trigger_io_dso.h"
#include "util/qt_model_view_util.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

//
// Trace and Trigger Selection
//

static const int PinRole = Qt::UserRole + 1;
static const int ColUnit = 0; // unit name/path
static const int ColName = 1; // user defined pin name

class BaseModel: public QStandardItemModel
{
    Q_OBJECT
    public:
        using QStandardItemModel::QStandardItemModel;

        void setTriggerIO(const TriggerIO &trigIO)
        {
            beginResetModel();
            m_trigIO = trigIO;
            endResetModel();
        }

        const TriggerIO &triggerIO() const
        {
            return m_trigIO;
        }

        QStringList pinPathList(const PinAddress &pa) const;
        QString pinPath(const PinAddress &pa) const;
        QString pinName(const PinAddress &pa) const;
        QString pinUserName(const PinAddress &pa) const;

    private:
        TriggerIO m_trigIO; // not efficient at all as both models keep a full copy of the trigger io setup...
};

class TraceTreeModel: public BaseModel
{
    Q_OBJECT
    public:
        using BaseModel::BaseModel;

    QStandardItem *samplesRoot = nullptr;
};

class TraceTableModel: public BaseModel
{
    Q_OBJECT
    public:
        using BaseModel::BaseModel;

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override
        {
            qDebug() << __PRETTY_FUNCTION__;
            // Pretend to always move column 0 to circumvent QStandardItemModel
            // from adding columns.
            return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
        }
};

class LIBMVME_EXPORT TraceSelectWidget: public QWidget
{
    Q_OBJECT

    signals:
        void selectionChanged(const QVector<PinAddress> &selection);
        void triggersChanged(const CombinedTriggers &triggers);
        void debugTraceClicked(const PinAddress &pa);

    public:
        TraceSelectWidget(QWidget *parent = nullptr);
        ~TraceSelectWidget() override;

        void setTriggerIO(const TriggerIO &trigIO);
        void setSelection(const QVector<PinAddress> &selection);
        QVector<PinAddress> getSelection() const;

        // Trigger bits in DSO trace order.
        void setTriggers(const CombinedTriggers &triggers);
        CombinedTriggers getTriggers() const;

        mvme::TreeViewExpansionState getTreeExpansionState() const;
        void setTreeExpansionState(const mvme::TreeViewExpansionState &expansionState);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// GUI controls to specify a DSOSetup and a DSO poll interval.
class LIBMVME_EXPORT DSOControlWidget: public QWidget
{
    Q_OBJECT
    signals:
        // Emitted on pressing the start button.
        // Use getPre/PostTriggerTime() and getInterval() to query for the DSO
        // parameters. If the interval is 0 only one snapshot should be
        // acquired from the DSO. Otherwise the DSO is restarted using the same
        // setup after the interval has elapsed.
        void startDSO();

        // Emitted on pressing the stop button.
        void stopDSO();

        // Emitted when the user changes the pre or post trigger times.
        void restartDSO();

    public:
        DSOControlWidget(QWidget *parent = nullptr);
        ~DSOControlWidget() override;

        unsigned getPreTriggerTime();
        unsigned getPostTriggerTime();
        std::chrono::milliseconds getInterval() const;

    public slots:
        // Load the pre- and postTriggerTimes and the interval into the GUI.
        void setDSOSettings(
            unsigned preTriggerTime,
            unsigned postTriggerTime,
            const std::chrono::milliseconds &interval = {});

        // Notify the widget about the current state of the DSO sampler.
        void setDSOActive(bool active);
        // Use this to indicate that the DSO was triggered and data arrived.
        void dsoTriggered();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__ */
