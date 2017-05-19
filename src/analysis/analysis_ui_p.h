#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"
#include "../histo_util.h"

#include <functional>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolBar>
#include <QWidget>

class MVMEContext;
class ModuleConfig;

namespace analysis
{

class AnalysisWidget;
class DataExtractionEditor;
class EventWidgetPrivate;
class OperatorConfigurationWidget;

class EventWidget: public QWidget
{
    Q_OBJECT
    public:

        using SelectInputCallback = std::function<void ()>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, AnalysisWidget *analysisWidget, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback);
        void endSelectInput();

        void addSource(SourcePtr src, ModuleConfig *module, bool addHistogramsAndCalibration,
                       const QString &unit = QString(), double unitMin = 0.0, double unitMax = 0.0);
        void sourceEdited(SourceInterface *src);
        void removeSource(SourceInterface *src);

        void addOperator(OperatorPtr op, s32 userLevel);
        void operatorEdited(OperatorInterface *op);
        void removeOperator(OperatorInterface *op);

        void uniqueWidgetCloses();
        void addUserLevel();
        void removeUserLevel();
        void repopulate();
        QToolBar *makeToolBar();

        MVMEContext *getContext() const;

        virtual bool eventFilter(QObject *watched, QEvent *event);

        friend class AnalysisWidget;
        friend class AnalysisWidgetPrivate;

    private:
        // Note: the EventWidgetPrivate part is not neccessary anymore as this
        // now already is inside a private header. I started EventWidget as the
        // main AnalysisUI class...
        EventWidgetPrivate *m_d;
};

class AddEditSourceWidget: public QDialog
{
    Q_OBJECT
    public:
        AddEditSourceWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget);
        AddEditSourceWidget(SourceInterface *src, ModuleConfig *mod, EventWidget *eventWidget);

        virtual void accept() override;
        virtual void reject() override;

        SourcePtr m_srcPtr;
        SourceInterface *m_src;
        ModuleConfig *m_module;
        EventWidget *m_eventWidget;

        QLineEdit *le_name;
        QDialogButtonBox *m_buttonBox;
        DataExtractionEditor *m_filterEditor;
        QFormLayout *m_optionsLayout;
        QSpinBox *m_spinCompletionCount;
        QGroupBox *m_gbGenHistograms;
        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_unitMin = nullptr;
        QDoubleSpinBox *spin_unitMax = nullptr;
        QComboBox *m_templateCombo = nullptr;
        QVector<std::shared_ptr<Extractor>> m_extractorTemplates;

        void applyTemplate(int index);
};

class AddEditOperatorWidget: public QDialog
{
    Q_OBJECT
    public:
        AddEditOperatorWidget(OperatorPtr opPtr, s32 userLevel, EventWidget *eventWidget);
        AddEditOperatorWidget(OperatorInterface *op, s32 userLevel, EventWidget *eventWidget);

        virtual void resizeEvent(QResizeEvent *event) override;

        void inputSelected(s32 slotIndex);
        virtual void accept() override;
        virtual void reject() override;
        void repopulateSlotGrid();

        OperatorPtr m_opPtr;
        OperatorInterface *m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;
        QVector<QPushButton *> m_selectButtons;
        QDialogButtonBox *m_buttonBox = nullptr;
        bool m_inputSelectActive = false;
        OperatorConfigurationWidget *m_opConfigWidget = nullptr;
        QGridLayout *m_slotGrid = nullptr;
        QPushButton *m_addSlotButton = nullptr;
        QPushButton *m_removeSlotButton = nullptr;

        struct SlotConnection
        {
            Pipe *inputPipe;
            s32 paramIndex;
        };

        QVector<SlotConnection> m_slotBackups;
        bool m_resizeEventSeen = false;
        bool m_wasAcceptedOrRejected = false;

        static const s32 WidgetMinWidth  = 325;
        static const s32 WidgetMinHeight = 175;
};

class OperatorConfigurationWidget: public QWidget
{
    Q_OBJECT
    public:
        OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, AddEditOperatorWidget *parent);
        //bool validateInputs();
        void configureOperator();
        void inputSelected(s32 slotIndex);

        AddEditOperatorWidget *m_parent;
        OperatorInterface *m_op;
        s32 m_userLevel;

        QLineEdit *le_name = nullptr;
        bool wasNameEdited = false;

        // Histo1DSink and Histo2DSink
        QComboBox *combo_xBins = nullptr;
        QComboBox *combo_yBins = nullptr;
        QLineEdit *le_xAxisTitle = nullptr;
        QLineEdit *le_yAxisTitle = nullptr;
        HistoAxisLimitsUI limits_x;
        HistoAxisLimitsUI limits_y;

        // CalibrationMinMax
        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_unitMin = nullptr;
        QDoubleSpinBox *spin_unitMax = nullptr;
        QTableWidget *m_calibrationTable = nullptr;
        QFrame *m_applyGlobalCalibFrame = nullptr;
        QPushButton *m_pb_applyGlobalCalib = nullptr;
        void fillCalibrationTable(CalibrationMinMax *calib, double proposedMin, double proposedMax);

        // IndexSelector
        QSpinBox *spin_index = nullptr;

        // PreviousValue
        QCheckBox *cb_keepValid = nullptr;

        // Sum
        QCheckBox *cb_isMean = nullptr;

        // ArrayMap
        QVector<ArrayMap::IndexPair> m_arrayMappings;
        QTableWidget *tw_input = nullptr;
        QTableWidget *tw_output = nullptr;

        // RangeFilter1D
        QDoubleSpinBox *spin_minValue;
        QDoubleSpinBox *spin_maxValue;
        QRadioButton *rb_keepInside;
        QRadioButton *rb_keepOutside;

        // RectFilter2D
        QDoubleSpinBox *spin_xMin,
                       *spin_xMax,
                       *spin_yMin,
                       *spin_yMax;
        QRadioButton *rb_opAnd;
        QRadioButton *rb_opOr;
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

class CalibrationItemDelegate: public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        virtual QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
};

}

#endif /* __ANALYSIS_UI_P_H__ */
