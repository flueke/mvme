#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"

#include <functional>

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QWidget>

class MVMEContext;

namespace analysis
{

class EventWidgetPrivate;

class EventWidget: public QWidget
{
    Q_OBJECT
    public:

        using SelectInputCallback = std::function<void ()>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback);
        void endSelectInput();
        void addOperator(OperatorPtr op, s32 userLevel);
        void addAnalysisElementWidgetCloses(); // FIXME: better name

    private:
        // Note: the EventWidgetPrivate part is not neccessary anymore as this
        // now already is inside a private header. This class started as the
        // main AnalaysisUi class
        EventWidgetPrivate *m_d;
};

class OperatorConfigurationWidget;

class AddOperatorWidget: public QWidget
{
    Q_OBJECT

    public:
        AddOperatorWidget(OperatorPtr op, s32 userLevel, EventWidget *eventWidget);

        virtual void closeEvent(QCloseEvent *event) override;
        void inputSelected(s32 slotIndex);
        void accept();

        OperatorPtr m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;
        QVector<QPushButton *> m_selectButtons;
        QDialogButtonBox *m_buttonBox = nullptr;
        bool m_inputSelectActive = false;
        OperatorConfigurationWidget *m_opConfigWidget = nullptr;
};

class OperatorConfigurationWidget: public QWidget
{
    Q_OBJECT
    public:
        OperatorConfigurationWidget(OperatorPtr op, s32 userLevel, AddOperatorWidget *parent);
        bool validateInputs();
        void configureOperator();

        AddOperatorWidget *m_parent;
        OperatorPtr m_op;
        s32 m_userLevel;

        QSpinBox *spin_bins = nullptr;
        QSpinBox *spin_xBins = nullptr;
        QSpinBox *spin_yBins = nullptr;
        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_factor = nullptr;
        QDoubleSpinBox *spin_offset = nullptr;
        QSpinBox *spin_index = nullptr;
};

class PipeDisplay: public QWidget
{
    Q_OBJECT
    public:
        PipeDisplay(Pipe *pipe, QWidget *parent = 0);

        void refresh();

        Pipe *m_pipe;

        QTableWidget *m_parameterTable;
};

}

#endif /* __ANALYSIS_UI_P_H__ */
