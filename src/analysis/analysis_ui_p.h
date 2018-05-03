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

        using SelectInputCallback = std::function<void (Slot *destSlot, Pipe *sourcePipe, s32 sourceParamIndex)>;

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

        void removeOperator(OperatorInterface *op);

        void uniqueWidgetCloses();
        void addUserLevel();
        void removeUserLevel();
        void toggleSinkEnabled(SinkInterface *sink);
        void repopulate();
        QToolBar *getToolBar();
        QToolBar *getEventSelectAreaToolBar();

        MVMEContext *getContext() const;
        AnalysisWidget *getAnalysisWidget() const;

        virtual bool eventFilter(QObject *watched, QEvent *event);

        friend class AnalysisWidget;
        friend class AnalysisWidgetPrivate;

        QUuid getEventId() const;

    public slots:
        // Note: these slots are only public because of being called by a
        // lambda which otherwise cannot invoke them.

        void listFilterExtractorDialogAccepted();
        void listFilterExtractorDialogApplied();
        void listFilterExtractorDialogRejected();

        void addExtractorDialogAccepted();
        void editExtractorDialogAccepted();
        void addEditExtractorDialogRejected();

        void addOperatorDialogAccepted();
        void editOperatorDialogAccepted();
        void addEditOperatorDialogRejected();

    private:
        EventWidgetPrivate *m_d;
};

class AddEditExtractorDialog: public QDialog
{
    Q_OBJECT
    public:
        enum Mode
        {
            AddExtractor,
            EditExtractor
        };

        AddEditExtractorDialog(std::shared_ptr<Extractor> ex, ModuleConfig *mod, Mode mode, EventWidget *eventWidget = nullptr);
        virtual ~AddEditExtractorDialog();

        virtual void accept() override;
        virtual void reject() override;

    private:
        std::shared_ptr<Extractor> m_ex;
        ModuleConfig *m_module;
        EventWidget *m_eventWidget;
        Mode m_mode;

        QLineEdit *le_name;
        QDialogButtonBox *m_buttonBox;
        DataExtractionEditor *m_filterEditor;
        QFormLayout *m_optionsLayout;
        QSpinBox *m_spinCompletionCount;
        QGroupBox *m_gbGenHistograms = nullptr;
        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_unitMin = nullptr;
        QDoubleSpinBox *spin_unitMax = nullptr;

        QVector<std::shared_ptr<Extractor>> m_defaultExtractors;

        void runLoadTemplateDialog();
        void applyTemplate(int index);
};


QWidget *data_source_widget_factory(SourceInterface *ds);

class AbstractOpConfigWidget;

/* Provides the input selection grid ("SlotGrid") and instantiates a specific
 * child widget depending on the operator type. */
class AddEditOperatorDialog: public QDialog
{
    Q_OBJECT
    signals:
        void selectInputForSlot(Slot *slot);

    public:
        enum Mode
        {
            AddOperator,
            EditOperator
        };

        AddEditOperatorDialog(OperatorPtr opPtr, s32 userLevel, Mode mode, EventWidget *eventWidget);

        virtual void resizeEvent(QResizeEvent *event) override;

        virtual void accept() override;
        virtual void reject() override;
        virtual bool eventFilter(QObject *watched, QEvent *event) override;
        void repopulateSlotGrid();

        OperatorPtr m_op;
        s32 m_userLevel;
        Mode m_mode;
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

    private:
        void onOperatorValidityChanged();

        void inputSelectedForSlot(
            Slot *destSlot,
            Pipe *selectedPipe,
            s32 selectedParamIndex);

        void endInputSelect();
};

class AbstractOpConfigWidget: public QWidget
{
    Q_OBJECT
    signals:
        void validityMayHaveChanged();

    public:
        AbstractOpConfigWidget(OperatorInterface *op, s32 userLevel, MVMEContext *context, QWidget *parent = nullptr);

        void setNameEdited(bool b) { m_wasNameEdited = b; }
        bool wasNameEdited() const { return m_wasNameEdited; }

        virtual void configureOperator() = 0;
        virtual void inputSelected(s32 slotIndex) = 0;
        virtual bool isValid() const = 0;

    protected:
        OperatorInterface *m_op;
        s32 m_userLevel;
        bool m_wasNameEdited;
        MVMEContext *m_context;

        QLineEdit *le_name = nullptr;
};

/* One widget to rule them all.
 * This handles most of the analysis operators. New/complex operators should
 * get their own config widget derived from AbstractOpConfigWidget unless it's
 * simple stuff they need. */
class OperatorConfigurationWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        OperatorConfigurationWidget(OperatorInterface *op,
                                    s32 userLevel,
                                    MVMEContext *context,
                                    QWidget *parent = nullptr);

        //bool validateInputs();
        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;
        bool isValid() const override;


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

        // ConditionFilter
        QCheckBox *cb_invertCondition;

        // ExportSink
        QLineEdit *le_exportPrefixPath;

        bool m_prefixPathWasManuallyEdited = false;

        QPushButton *pb_selectOutputDirectory,
                    *pb_generateCode;

        QComboBox *combo_exportFormat;
        QComboBox *combo_exportCompression;
};

class RateMonitorConfigWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        RateMonitorConfigWidget(RateMonitorSink *op,
                                s32 userLevel,
                                MVMEContext *context,
                                QWidget *parent = nullptr);

        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;
        bool isValid() const override;

    private:
        RateMonitorSink *m_rms;

        QComboBox *combo_type;
        QSpinBox *spin_capacity;
        QLineEdit *le_unit;
        QDoubleSpinBox *spin_factor;
        QDoubleSpinBox *spin_offset;
        QDoubleSpinBox *spin_interval;

        // TODO (maybe): implement the min/max way of calibrating the input values
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
                                              MVMEContext *context,
                                              QWidget *parent = nullptr);

        void configureOperator() override;
        void inputSelected(s32 slotIndex) override;
        bool isValid() const override;

    private:
        static const size_t ArenaSize = Kilobytes(256);

        void populateEditors();
        void populateInputTable();
        void populateOutputTable();
        void rebuild();
        void reloadFromOperator();

        ExpressionOperator *m_a1_op;
        memory::Arena m_arena;
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
        QTreeWidgetItem *rateRoot    = nullptr;
        QTreeWidgetItem *exportRoot  = nullptr;
};

class SessionErrorDialog: public QDialog
{
    Q_OBJECT
    public:
        SessionErrorDialog(const QString &message, const QString &title = QString(), QWidget *parent = nullptr);
};

class ExportSinkStatusMonitor: public QWidget
{
    Q_OBJECT
    public:
        ExportSinkStatusMonitor(const std::shared_ptr<ExportSink> &sink,
                                MVMEContext *context,
                                QWidget *parent = nullptr);

    private slots:
        void update();

    private:
        std::shared_ptr<ExportSink> m_sink;
        MVMEContext *m_context;

        QLabel *label_outputDirectory,
               *label_fileName,
               *label_fileSize,
               *label_eventsWritten,
               *label_bytesWritten,
               *label_status;

        QPushButton *pb_openDirectory;
};

}

#endif /* __ANALYSIS_UI_P_H__ */
