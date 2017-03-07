#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"

#include <functional>

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QWidget>

class MVMEContext;
class ModuleConfig;

namespace analysis
{

class AnalysisWidget;
class DataExtractionEditor;
class EventWidgetPrivate;
class OperatorConfigurationWidget;

struct AnalysisPauser
{
    AnalysisPauser(MVMEContext *context);
    ~AnalysisPauser();

    MVMEContext *context;
    bool was_running;
};

class EventWidget: public QWidget
{
    Q_OBJECT
    public:

        using SelectInputCallback = std::function<void ()>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, AnalysisWidget *analysisWidget, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback);
        void endSelectInput();

        void addSource(SourcePtr src, ModuleConfig *module, bool addHistogramsAndCalibration);
        void sourceEdited(SourceInterface *src);
        void removeSource(SourceInterface *src);

        void addOperator(OperatorPtr op, s32 userLevel);
        void operatorEdited(OperatorInterface *op);
        void removeOperator(OperatorInterface *op);

        void uniqueWidgetCloses();
        void addUserLevel(s32 eventIndex);

        MVMEContext *getContext() const;

        virtual bool eventFilter(QObject *watched, QEvent *event);

    private:
        // Note: the EventWidgetPrivate part is not neccessary anymore as this
        // now already is inside a private header. This class started as the
        // main AnalaysisUi class
        EventWidgetPrivate *m_d;
};

class AddEditOperatorWidget: public QWidget
{
    Q_OBJECT
    public:
        AddEditOperatorWidget(OperatorPtr opPtr, s32 userLevel, EventWidget *eventWidget);
        AddEditOperatorWidget(OperatorInterface *op, s32 userLevel, EventWidget *eventWidget);

        virtual void closeEvent(QCloseEvent *event) override;
        void inputSelected(s32 slotIndex);
        void accept();
        void reject();

        OperatorPtr m_opPtr;
        OperatorInterface *m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;
        QVector<QPushButton *> m_selectButtons;
        QDialogButtonBox *m_buttonBox = nullptr;
        bool m_inputSelectActive = false;
        OperatorConfigurationWidget *m_opConfigWidget = nullptr;

        struct SlotConnection
        {
            Pipe *inputPipe;
            s32 paramIndex;
        };

        QVector<SlotConnection> m_slotBackups;
};

class AddEditSourceWidget: public QWidget
{
    Q_OBJECT
    public:
        AddEditSourceWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget);
        AddEditSourceWidget(SourceInterface *src, ModuleConfig *mod, EventWidget *eventWidget);

        virtual void closeEvent(QCloseEvent *event) override;
        void accept();

        SourcePtr m_srcPtr;
        SourceInterface *m_src;
        ModuleConfig *m_module;
        EventWidget *m_eventWidget;

        QLineEdit *le_name;
        QDialogButtonBox *m_buttonBox;
        DataExtractionEditor *m_filterEditor;
        QFormLayout *m_optionsLayout;
        QSpinBox *m_spinCompletionCount;
        QCheckBox *m_cbGenHistograms;
        bool m_editMode;
};

class OperatorConfigurationWidget: public QWidget
{
    Q_OBJECT
    public:
        OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, AddEditOperatorWidget *parent);
        bool validateInputs();
        void configureOperator();
        void inputSelected(s32 slotIndex);

        AddEditOperatorWidget *m_parent;
        OperatorInterface *m_op;
        s32 m_userLevel;

        QLineEdit *le_name = nullptr;
        QSpinBox *spin_xBins = nullptr;
        QSpinBox *spin_yBins = nullptr;
        QDoubleSpinBox *spin_xMin = nullptr;
        QDoubleSpinBox *spin_xMax = nullptr;
        QDoubleSpinBox *spin_yMin = nullptr;
        QDoubleSpinBox *spin_yMax = nullptr;

        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_factor = nullptr;
        QDoubleSpinBox *spin_offset = nullptr;
        QDoubleSpinBox *spin_unitMin = nullptr;
        QDoubleSpinBox *spin_unitMax = nullptr;
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
