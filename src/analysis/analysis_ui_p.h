/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QWidget>

class MVMEContext;
class ModuleConfig;

namespace analysis
{

class AnalysisWidget;
class DataExtractionEditor;
struct EventWidgetPrivate;
class OperatorConfigurationWidget;

class EventWidget: public QWidget
{
    Q_OBJECT
    public:

        using SelectInputCallback = std::function<void ()>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                    AnalysisWidget *analysisWidget, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback);
        void endSelectInput();
        void highlightInputOf(Slot *slot, bool doHighlight);

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
        QToolBar *getToolBar();
        QToolBar *getEventSelectAreaToolBar();

        MVMEContext *getContext() const;
        AnalysisWidget *getAnalysisWidget() const;

        virtual bool eventFilter(QObject *watched, QEvent *event);

        friend class AnalysisWidget;
        friend class AnalysisWidgetPrivate;

    public slots:
        void listFilterExtractorDialogAccepted();
        void listFilterExtractorDialogApplied();
        void listFilterExtractorDialogRejected();

    private:
        EventWidgetPrivate *m_d;
};

class AddEditExtractorWidget: public QDialog
{
    Q_OBJECT
    public:
        AddEditExtractorWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget);
        AddEditExtractorWidget(SourceInterface *src, ModuleConfig *mod, EventWidget *eventWidget);

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

        QVector<std::shared_ptr<Extractor>> m_defaultExtractors;

        void runLoadTemplateDialog();
        void applyTemplate(int index);
};

class ListFilterExtractorDialog: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        ListFilterExtractorDialog(ModuleConfig *mod, analysis::Analysis *analysis,
                                  MVMEContext *context, QWidget *parent = nullptr);
        virtual ~ListFilterExtractorDialog();

        void editSource(const SourcePtr &src);

        QVector<ListFilterExtractorPtr> getExtractors() const;

    public slots:
        virtual void accept() override;
        virtual void reject() override;

        void newFilter();

    private slots:
        void apply();
        void removeFilter();
        void cloneFilter();
        void updateWordCount();

    private:
        void repopulate();
        int addFilterToUi(const ListFilterExtractorPtr &ex);

        struct ListFilterExtractorDialogPrivate;

        std::unique_ptr<ListFilterExtractorDialogPrivate> m_d;
};

QWidget *data_source_widget_factory(SourceInterface *ds);

class AbstractOpConfigWidget;

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
        virtual bool eventFilter(QObject *watched, QEvent *event) override;
        void repopulateSlotGrid();

        OperatorPtr m_opPtr;
        OperatorInterface *m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;
        QVector<QPushButton *> m_selectButtons;
        QDialogButtonBox *m_buttonBox = nullptr;
        bool m_inputSelectActive = false;
        AbstractOpConfigWidget *m_opConfigWidget = nullptr;
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

class AbstractOpConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        AbstractOpConfigWidget(OperatorInterface *op, s32 userLevel, QWidget *parent = nullptr);

        void setNameEdited(bool b) { m_wasNameEdited = b; }
        bool wasNameEdited() const { return m_wasNameEdited; }

        virtual void configureOperator() = 0;
        virtual void inputSelected(s32 slotIndex) = 0;

    protected:
        OperatorInterface *m_op;
        s32 m_userLevel;
        bool m_wasNameEdited;

        QLineEdit *le_name = nullptr;
};

/* One widget to rule them all.
 * This handles all of the older analysis operators. New operators should get
 * their own config widget derived from AbstractOpConfigWidget unless it's
 * simple stuff they need. */
class OperatorConfigurationWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, QWidget *parent = nullptr);
        //bool validateInputs();
        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;


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

        // BinarySumDiff
        QComboBox *combo_equation;
        QDoubleSpinBox *spin_outputLowerLimit;
        QDoubleSpinBox *spin_outputUpperLimit;
        QPushButton *pb_autoLimits;

        void updateOutputLimits(BinarySumDiff *binOp);

        // AggregateOps
        QComboBox *combo_aggOp;

        QCheckBox *cb_useMinThreshold,
                  *cb_useMaxThreshold;

        QDoubleSpinBox *spin_minThreshold,
                       *spin_maxThreshold;
};

class RateMonitorConfigWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        RateMonitorConfigWidget(RateMonitorSink *op, s32 userLevel, QWidget *parent = nullptr);

        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;

    private:
        RateMonitorSink *m_rms;

        QComboBox *combo_type;
        QSpinBox *spin_capacity;
        QLineEdit *le_unit;
        QDoubleSpinBox *spin_factor;
        QDoubleSpinBox *spin_offset;

        // TODO: implement the min/max way of calibrating the input values
        //QDoubleSpinBox *spin_unitMin;
        //QDoubleSpinBox *spin_unitMax;
        //QStackedWidget *stack_calibration;
        //QComboBox *combo_calibrationType;
};

class ExpressionEditor: public QWidget
{
    Q_OBJECT
    public:
        ExpressionEditor(QWidget *parent = nullptr);

        void setExpressionString(const QString &exprString);
        QString getExpressionString() const;

    private:
        QPlainTextEdit *m_textEdit;
        QTableWidget *m_errorTable;
        QPushButton *pb_compile,
                    *pb_run;
};

class ExpressionOperatorConfigurationWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        ExpressionOperatorConfigurationWidget(ExpressionOperator *op,
                                              s32 userLevel,
                                              QWidget *parent = nullptr);

        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;

    private:
        static const size_t ArenaSize = Kilobytes(256);

        void populateEditors();
        void populateInputTable();
        void populateOutputTable();
        void rebuild();
        void reloadFromOperator();

        ExpressionOperator *m_op;
        memory::Arena m_arena;
        a2::PipeVectors m_a2_inPipe;
        a2::Operator m_a2_op;

        ExpressionEditor *m_exprBeginEditor,
                         *m_exprStepEditor;

        QTableWidget *m_tw_input,
                     *m_tw_output;
};

class PipeDisplay: public QWidget
{
    Q_OBJECT
    public:
        PipeDisplay(Analysis *analysis, Pipe *pipe, QWidget *parent = 0);

        void refresh();

        Analysis *m_analysis;
        Pipe *m_pipe;

        QLabel *m_infoLabel;
        QTableWidget *m_parameterTable;
};

class CalibrationItemDelegate: public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        virtual QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
};

/* Specialized tree for the EventWidget.
 *
 * The declaration is here because of MOC, the implementation is in
 * analysis_ui.cc because of locally defined types.
 */
class EventWidgetTree: public QTreeWidget
{
    Q_OBJECT
    public:
        using QTreeWidget::QTreeWidget;

        EventWidget *m_eventWidget = nullptr;
        s32 m_userLevel = 0;

    protected:
        virtual bool dropMimeData(QTreeWidgetItem *parent, int index, const QMimeData *data, Qt::DropAction action) override;
        virtual QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
        virtual QStringList mimeTypes() const override;
        virtual Qt::DropActions supportedDropActions() const override;
        virtual void dropEvent(QDropEvent *event) override;
};

/* Subclass storing pointers to the roots for 1D and 2D histograms. Originally
 * finding those nodes was done via QTreeWidget::topLevelItem() but this would
 * break if anything gets sorted before or in-between the two root nodes. */
class DisplayTree: public EventWidgetTree
{
    Q_OBJECT
    public:
        using EventWidgetTree::EventWidgetTree;

        QTreeWidgetItem *histo1DRoot = nullptr;
        QTreeWidgetItem *histo2DRoot = nullptr;
};

class SessionErrorDialog: public QDialog
{
    Q_OBJECT
    public:
        SessionErrorDialog(const QString &message, const QString &title = QString(), QWidget *parent = nullptr);
};

}

#endif /* __ANALYSIS_UI_P_H__ */
