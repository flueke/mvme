/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "analysis/analysis.h"
#include "analysis/code_editor.h"
#include "analysis/object_editor_dialog.h"
#include "analysis/ui_eventwidget.h"
#include "analysis/ui_lib.h"
#include "data_filter_edit.h"
#include "histo_util.h"
#include "mvlc_stream_worker.h"
#include "qt_util.h"

class ModuleConfig;

namespace analysis
{
namespace ui
{

class AnalysisWidget;
class DataExtractionEditor;
struct EventWidgetPrivate;
class OperatorConfigurationWidget;
class ObjectTree;

class FilterNameListDialog: public QDialog
{
    Q_OBJECT
    public:
        FilterNameListDialog(const QString &filterName, const QStringList &names,
                             QWidget *parent = nullptr);

        virtual void accept();
        QStringList getNames() const { return m_names; }

    private:
        CodeEditor *m_editor;
        QDialogButtonBox *m_bb;
        QStringList m_names;
};

class AddEditExtractorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        AddEditExtractorDialog(std::shared_ptr<Extractor> ex, ModuleConfig *moduleConfig,
                               ObjectEditorMode mode, EventWidget *eventWidget = nullptr);
        virtual ~AddEditExtractorDialog();

        virtual void accept() override;
        virtual void reject() override;

    private:
        std::shared_ptr<Extractor> m_ex;
        ModuleConfig *m_module;
        EventWidget *m_eventWidget;
        ObjectEditorMode m_mode;
        QStringList m_parameterNames;

        QLineEdit *le_name;
        QDialogButtonBox *m_buttonBox;
        DataExtractionEditor *m_filterEditor;
        QFormLayout *m_optionsLayout;
        QSpinBox *m_spinCompletionCount;
        QPushButton *pb_editNameList;
        QGroupBox *m_gbGenHistograms = nullptr;
        QLineEdit *le_unit = nullptr;
        QDoubleSpinBox *spin_unitMin = nullptr;
        QDoubleSpinBox *spin_unitMax = nullptr;
        QCheckBox *cb_noAddedRandom = nullptr;
        QCheckBox *cb_isSignedValue = nullptr;

        QVector<std::shared_ptr<Extractor>> m_defaultExtractors;

        void runLoadTemplateDialog();
        void applyTemplate(int index);
        void editNameList();
};

class MultiHitExtractorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        MultiHitExtractorDialog(
            const std::shared_ptr<MultiHitExtractor> &ex,
            ModuleConfig *mod,
            ObjectEditorMode mode,
            EventWidget *eventWidget = nullptr);
        ~MultiHitExtractorDialog() override;

        void accept() override;
        void reject() override;

    private slots:
        void runLoadTemplateDialog();
        void applyTemplate(const std::shared_ptr<Extractor> &tmpl);
        void updateWidget();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};


QComboBox *make_event_selection_combo(
    const QList<EventConfig *> &eventConfigs,
    const OperatorPtr &op,
    const DirectoryPtr &destDir = {},
    QWidget *parent = nullptr);

class AbstractOpConfigWidget;

/* Provides the input selection grid ("SlotGrid") and instantiates a specific
 * child widget depending on the operator type. */
class AddEditOperatorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    signals:
        void selectInputForSlot(Slot *slot);

    public:

        AddEditOperatorDialog(OperatorPtr opPtr,
                              s32 userLevel,
                              ObjectEditorMode mode,
                              const DirectoryPtr &destDir,
                              EventWidget *eventWidget);

        virtual void resizeEvent(QResizeEvent *event) override;

        virtual void accept() override;
        virtual void reject() override;
        virtual bool eventFilter(QObject *watched, QEvent *event) override;
        void repopulateSlotGrid();

        OperatorPtr m_op;
        s32 m_userLevel;
        ObjectEditorMode m_mode;
        DirectoryPtr m_destDir;

        QComboBox *m_eventSelectionCombo = nullptr;
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

    private slots:
        void onOperatorValidityChanged();

    private:
        void inputSelectedForSlot(
            Slot *destSlot,
            Pipe *selectedPipe,
            s32 selectedParamIndex);

        void endInputSelect();
};

/* Base implementation and interface for custom operator configuration UIs.
 * Created and used by AddEditOperatorDialog. */
class AbstractOpConfigWidget: public QWidget
{
    Q_OBJECT
    signals:
        void validityMayHaveChanged();

    public:
        AbstractOpConfigWidget(OperatorInterface *op,
                               s32 userLevel,
                               AnalysisServiceProvider *serviceProvider,
                               QWidget *parent = nullptr);

        void setNameEdited(bool b) { m_wasNameEdited = b; }
        bool wasNameEdited() const { return m_wasNameEdited; }

        virtual void configureOperator() = 0;
        virtual void inputSelected(s32 slotIndex) = 0;
        virtual bool isValid() const = 0;

    protected:
        OperatorInterface *m_op;
        s32 m_userLevel;
        bool m_wasNameEdited;
        AnalysisServiceProvider *m_serviceProvider;

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
                                    AnalysisServiceProvider *serviceProvider,
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
                    *pb_generateCode,
                    *pb_openOutputDir;

        QComboBox *combo_exportFormat;
        QComboBox *combo_exportCompression;
        QGroupBox *gb_codeGen;
};

class RateMonitorConfigWidget: public AbstractOpConfigWidget
{
    Q_OBJECT
    public:
        RateMonitorConfigWidget(RateMonitorSink *op,
                                s32 userLevel,
                                AnalysisServiceProvider *asp,
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
        QDoubleSpinBox *spin_dtSample;
        QComboBox *combo_xScaleType;
};

class SelectConditionsDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        SelectConditionsDialog(const OperatorPtr &op, EventWidget *eventWidget);

        void accept() override;
        void reject() override;

        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        void addSelectButtons(const ConditionPtr &cond = {});

        EventWidget *m_eventWidget;
        OperatorPtr m_op;
        QGridLayout *m_buttonsGrid;
        QVector<QPushButton *> m_selectButtons;
        QVector<ConditionPtr> m_selectedConditions;
        bool m_inputSelectActive = false;
};

class PipeDisplay: public QWidget
{
    Q_OBJECT
    public:
        PipeDisplay(Analysis *analysis, Pipe *pipe, bool showDecimals = true,
                    QWidget *parent = nullptr);

        void setShowDecimals(bool showDecimals) { m_showDecimals = showDecimals; }
        bool doesShowDecimals() const { return m_showDecimals; }

    public slots:
        void refresh();

    private:
        Analysis *m_analysis;
        Pipe *m_pipe;
        bool m_showDecimals;
        QTableWidget *m_parameterTable;
};

class CalibrationItemDelegate: public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        virtual QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const;
};

class SessionErrorDialog: public QDialog
{
    Q_OBJECT
    public:
        SessionErrorDialog(const QString &message, const QString &title = QString(),
                           QWidget *parent = nullptr);
};

class ExportSinkStatusMonitor: public QWidget
{
    Q_OBJECT
    public:
        ExportSinkStatusMonitor(const std::shared_ptr<ExportSink> &sink,
                                AnalysisServiceProvider *serviceProvider,
                                QWidget *parent = nullptr);

    private slots:
        void update();

    private:
        std::shared_ptr<ExportSink> m_sink;
        AnalysisServiceProvider *m_serviceProvider;

        QLabel *label_outputDirectory,
               *label_fileName,
               *label_fileSize,
               *label_eventsWritten,
               *label_bytesWritten,
               *label_status;

        QPushButton *pb_openDirectory;
};

class EventSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        EventSettingsDialog(
            const VMEConfig *vmeConfig,
            const Analysis::VMEObjectSettings &settings,
            QWidget *parent = nullptr);

        ~EventSettingsDialog();

        Analysis::VMEObjectSettings getSettings() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class ModuleSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        ModuleSettingsDialog(const ModuleConfig *moduleConfig,
                             const QVariantMap &settings,
                             QWidget *parent = nullptr);

        QVariantMap getSettings() const { return m_settings; }

        virtual void accept() override;

    private:
        QVariantMap m_settings;
        DataFilterEdit *m_filterEdit;
};

QString make_input_source_text(Slot *slot);
QString make_input_source_text(Pipe *inputPipe, s32 paramIndex = Slot::NoParamIndex);

class MVLCParserDebugHandler: public QObject
{
    Q_OBJECT
    public:
        explicit MVLCParserDebugHandler(QObject *parent = nullptr);

    public slots:
        void handleDebugInfo(
            const DataBuffer &buffer,
            mesytec::mvlc::readout_parser::ReadoutParserState parserState,
            const mesytec::mvlc::readout_parser::ReadoutParserCounters &parserCounters,
            const VMEConfig *vmeConfig,
            const analysis::Analysis *analysis);

    private:
        WidgetGeometrySaver *m_geometrySaver;
};

class MVLCSingleStepHandler: public QObject
{
    Q_OBJECT
    public:
        using Logger = std::function<void (const QString &)>;
        MVLCSingleStepHandler(Logger logger, QObject *parent = nullptr);

    public slots:
        void handleSingleStepResult(const EventRecord &eventRecord);

    private:
        Logger m_logger;
};

} // ns ui
} // ns analysis

#endif /* __ANALYSIS_UI_P_H__ */
