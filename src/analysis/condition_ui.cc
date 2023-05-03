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
#include "condition_ui.h"

#include <algorithm>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <memory>

#include <qwt_picker_machine.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_shapeitem.h>

#include "analysis/analysis.h"
#include "analysis/analysis_ui_util.h"
#include "analysis/analysis_util.h"
#include "analysis/ui_lib.h"
#include "gui_util.h"
#include "histo1d_widget.h"
#include "histo_ui.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme_qthelp.h"
#include "qt_util.h"
#include "treewidget_utils.h"
#include "ui_interval_condition_dialog.h"
#include "ui_polygon_condition_dialog.h"

namespace analysis
{
namespace ui
{

using ConditionInterface = analysis::ConditionInterface;

namespace
{

enum NodeType
{
    NodeType_Condition = QTreeWidgetItem::UserType,
    NodeType_ConditionBit,
};

enum DataRole
{
    DataRole_AnalysisObject = Global_DataRole_AnalysisObject,
    DataRole_RawPointer,
    DataRole_BitIndex
};

class TreeNode: public BasicTreeNode
{
    public:
        using BasicTreeNode::BasicTreeNode;

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const override
        {
            if (type() == other.type() && treeWidget() && treeWidget()->sortColumn() == 0)
            {
                if (type() == NodeType_ConditionBit)
                {
                    s32 thisIndex  = data(0, DataRole_BitIndex).toInt();
                    s32 otherIndex = other.data(0, DataRole_BitIndex).toInt();
                    return thisIndex < otherIndex;
                }
            }
            return QTreeWidgetItem::operator<(other);
        }
};

template<typename T>
TreeNode *make_node(T *data, int type = QTreeWidgetItem::Type, int dataRole = DataRole_RawPointer)
{
    auto ret = new TreeNode(type);
    ret->setData(0, dataRole, QVariant::fromValue(static_cast<void *>(data)));
    ret->setFlags(ret->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled));
    return ret;
}

} // end anon namespace

using namespace histo_ui;

ConditionDialogBase::ConditionDialogBase(QWidget *parent)
    : QDialog(parent)
{
}

ConditionDialogBase::~ConditionDialogBase()
{
}

struct IntervalConditionDialog::Private
{
    std::unique_ptr<Ui::IntervalConditionDialog> ui;
    QToolBar *toolbar_ = nullptr;
};

IntervalConditionDialog::IntervalConditionDialog(QWidget *parent)
    : ConditionDialogBase(parent)
    , d(std::make_unique<Private>())
{
    d->ui = std::make_unique<Ui::IntervalConditionDialog>();
    d->ui->setupUi(this);
    d->toolbar_ = make_toolbar();
    auto tb_frameLayout = make_hbox<0, 0>(d->ui->tb_frame);
    tb_frameLayout->addWidget(d->toolbar_);
    auto actionNew = d->toolbar_->addAction(QIcon(":/document-new.png"), "New");
    auto actionSave = d->toolbar_->addAction(QIcon(":/document-save.png"), "Save Changes");
    d->toolbar_->addAction(QIcon(":/help.png"), QSL("Help"),
                           this, mesytec::mvme::make_help_keyword_handler("Condition System"));

    d->ui->tw_intervals->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(actionNew, &QAction::triggered,
            this, &IntervalConditionDialog::newConditionButtonClicked);

    connect(actionSave, &QAction::triggered,
            this, &IntervalConditionDialog::applied);

    connect(d->ui->combo_cond, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int /*index*/) {
                emit conditionSelected(d->ui->combo_cond->currentData().toUuid());
            });

    connect(d->ui->combo_cond, &QComboBox::editTextChanged,
            this, [this] (const QString &text) {
                emit conditionNameChanged(
                    d->ui->combo_cond->currentData().toUuid(),
                    text);
            });

    connect(d->ui->tw_intervals, &QTableWidget::itemChanged,
            this, [this] (QTableWidgetItem *item) {
                auto row = item->row();
                auto col = item->column();
                auto intervals = getIntervals();

                if (shouldEditAllIntervals())
                    intervals.fill(intervals[row]);

                setIntervals(intervals);
                emit intervalsEdited(intervals);

                if (auto newItem = d->ui->tw_intervals->item(row, col))
                    d->ui->tw_intervals->setCurrentItem(newItem, QItemSelectionModel::Select);
            });

    connect(d->ui->tw_intervals, &QTableWidget::currentCellChanged,
            this, [this] (int curRow, int curCol, int prevRow, int prevCol) {

                if (const auto &intervals = getIntervals();
                    0 <= curRow && curRow < intervals.size())
                {
                    emit intervalSelected(curRow);
                }
            });

    auto ignore_all = [this]
    {
        for (int row = 0; row < d->ui->tw_intervals->rowCount(); ++row)
        {
            if (auto cb_ignore = qobject_cast<QCheckBox *>(d->ui->tw_intervals->cellWidget(row, 2)))
                cb_ignore->setChecked(true);
        }
    };

    auto ignore_none = [this]
    {
        for (int row = 0; row < d->ui->tw_intervals->rowCount(); ++row)
        {
            if (auto cb_ignore = qobject_cast<QCheckBox *>(d->ui->tw_intervals->cellWidget(row, 2)))
                cb_ignore->setChecked(false);
        }
    };

    connect(d->ui->pb_ignoreAll, &QPushButton::clicked, this, ignore_all);
    connect(d->ui->pb_ignoreNone, &QPushButton::clicked, this, ignore_none);
}

IntervalConditionDialog::~IntervalConditionDialog()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void IntervalConditionDialog::setConditionList(const QVector<ConditionInfo> &condInfos)
{
    d->ui->combo_cond->clear();
    for (const auto &info: condInfos)
    {
        d->ui->combo_cond->addItem(info.second, info.first);
    }

    d->ui->combo_cond->setEditable(d->ui->combo_cond->count() > 0);
}

void IntervalConditionDialog::setIntervals(const QVector<IntervalData> &intervals)
{
    QSignalBlocker sb(d->ui->tw_intervals); // block itemChanged() from the table widget
    int curRow = d->ui->tw_intervals->currentRow();
    d->ui->tw_intervals->clearContents();
    d->ui->tw_intervals->setRowCount(intervals.size());
    int row = 0;
    for (const auto &intervalData: intervals)
    {
        const auto &interval = intervalData.interval;
        auto x1Item = new QTableWidgetItem;
        auto x2Item = new QTableWidgetItem;

        x1Item->setText(QString::number(interval.minValue()));
        x2Item->setText(QString::number(interval.maxValue()));

        d->ui->tw_intervals->setItem(row, 0, x1Item);
        d->ui->tw_intervals->setItem(row, 1, x2Item);

        auto cb_ignore = new QCheckBox;
        cb_ignore->setChecked(intervalData.ignored);
        d->ui->tw_intervals->setCellWidget(row, 2, cb_ignore);

        auto headerItem = new QTableWidgetItem(QString::number(row));
        d->ui->tw_intervals->setVerticalHeaderItem(row, headerItem);

        ++row;
    }
    d->ui->tw_intervals->resizeRowsToContents();
    d->ui->tw_intervals->setCurrentCell(curRow, 0);
}


QVector<IntervalConditionDialog::IntervalData> IntervalConditionDialog::getIntervals() const
{
    QVector<IntervalData> ret;

    for (int row=0; row<d->ui->tw_intervals->rowCount(); ++row)
    {
        auto x1Item = d->ui->tw_intervals->item(row, 0);
        auto x2Item = d->ui->tw_intervals->item(row, 1);
        auto cb_ignore = qobject_cast<QCheckBox *>(d->ui->tw_intervals->cellWidget(row, 2));

        double x1 = x1Item->data(Qt::EditRole).toDouble();
        double x2 = x2Item->data(Qt::EditRole).toDouble();
        bool ignored = cb_ignore && cb_ignore->isChecked();

        IntervalData intervalData
        {
            QwtInterval{x1, x2},
            ignored
        };

        ret.push_back(intervalData);
    }

    return ret;
}

QString IntervalConditionDialog::getConditionName() const
{
    return d->ui->combo_cond->currentText();
}

bool IntervalConditionDialog::shouldEditAllIntervals() const
{
    return d->ui->cb_editAll->isChecked();
}

void IntervalConditionDialog::setInfoText(const QString &txt)
{
    d->ui->label_info->setText(txt);
}

void IntervalConditionDialog::selectCondition(const QUuid &objectId)
{
    auto idx = d->ui->combo_cond->findData(objectId);

    if (idx >= 0)
        d->ui->combo_cond->setCurrentIndex(idx);
}

void IntervalConditionDialog::selectInterval(int index)
{
    auto col = d->ui->tw_intervals->currentColumn();

    if (auto item = d->ui->tw_intervals->item(index, col))
    {
        d->ui->tw_intervals->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    }
}

void IntervalConditionDialog::reject()
{
    d->ui->tw_intervals->clearContents();
    d->ui->combo_cond->clear();
    d->ui->combo_cond->setEditable(false);
    QDialog::reject();
}

struct IntervalConditionEditorController::Private
{
    enum class State
    {
        Inactive,
        NewInterval,
        EditInterval
    };

    Histo1DSinkPtr sink_;
    IPlotWidget *histoWidget_;
    IntervalConditionDialog *dialog_;
    AnalysisServiceProvider *asp_;

    // Currently selected condition id. Updated when the dialoge emits conditionSelected()
    QUuid currentConditionId_;

    // Set if a new condition is to be created. Cleared once the condition has
    // been added to the analysis via 'apply'.
    std::shared_ptr<IntervalCondition> newCond_;

    // Intervals currently being edited.
    QVector<IntervalConditionDialog::IntervalData> intervals_;

    // Interval edit state
    State state_ = State::Inactive;

    NewIntervalPicker *newPicker_ = nullptr;
    IntervalEditorPicker *editPicker_ = nullptr;
    bool hasUnsavedChanges_ = false;

    void repopulateDialogFromAnalysis()
    {
        auto conditions = getEditableConditions();
        auto condInfos = getConditionInfos(conditions);
        dialog_->setConditionList(condInfos);
        hasUnsavedChanges_ = false;
    }

    void transitionState(State newState)
    {
        switch (newState)
        {
            case State::Inactive:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(true);

                    dialog_->setInfoText("Click the \"New\" button to create a new interval condition working on the histograms input.");
                } break;

            case State::NewInterval:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(true);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    dialog_->setInfoText("Select two points in the histogram to create the initial condition intervals.");
                } break;

            case State::EditInterval:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(true);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    dialog_->setInfoText("Drag interval borders or edit table cells to modify. "
                                         "When evaluating the condition at least one interval test "
                                         "must succeed for the condition to become true "
                                         "(OR over all interval checks).\n"
                                         "Checking \"ignore\" excludes the entry from affecting "
                                         "the result of the condition evaluation."
                                         );

                    hasUnsavedChanges_ = true;
                } break;
        }

        state_ = newState;
    }

    void onDialogApplied()
    {
        std::shared_ptr<IntervalCondition> cond;

        if (newCond_ && newCond_->getId() == currentConditionId_)
            cond = newCond_;
        else
            cond = getCondition(currentConditionId_);

        if (!cond)
            return;

        AnalysisPauser pauser(asp_);

        intervals_ = dialog_->getIntervals();

        cond->setObjectName(dialog_->getConditionName());
        cond->setIntervals(intervals_);

        if (!cond->getAnalysis())
        {
            // It's a new condition. Connect its input and add it to the analysis.
            cond->connectArrayToInputSlot(0, sink_->getSlot(0)->inputPipe);
            cond->setEventId(sink_->getEventId());
            cond->setUserLevel(sink_->getUserLevel());
            add_condition_to_analysis(asp_->getAnalysis(), cond);
        }
        else
        {
            assert(asp_->getAnalysis() == cond->getAnalysis().get());
            asp_->setAnalysisOperatorEdited(cond);
        }

        newCond_ = {};
        repopulateDialogFromAnalysis();
        dialog_->selectCondition(cond->getId());
        hasUnsavedChanges_ = false;
    }

    void onDialogAccepted()
    {
        qDebug() << __PRETTY_FUNCTION__ << this;
        onDialogApplied();
    }

    void onDialogRejected()
    {
        qDebug() << __PRETTY_FUNCTION__ << this;
        transitionState(State::Inactive);
        newCond_ = {};
        intervals_ = {};
        currentConditionId_ = QUuid();
        hasUnsavedChanges_ = false;
    }

    // Keeps the dialog_ at the top right of the histo widget.
    void updateDialogPosition()
    {
        int x = histoWidget_->x() + histoWidget_->frameGeometry().width() + 2;
        int y = histoWidget_->y();
        dialog_->move({x, y});
        dialog_->resize(dialog_->width(), histoWidget_->height());
    }

    void onNewConditionRequested()
    {
        if (newCond_ || hasUnsavedChanges_)
        {
            auto choice = QMessageBox::warning(dialog_, "Discarding condition",
                "Creating a new condition will discard unsaved changes! Continue?",
                QMessageBox::Cancel | QMessageBox::Ok,
                QMessageBox::Cancel);

            if (choice == QMessageBox::Cancel)
                return;
        }

        auto analysis = asp_->getAnalysis();
        newCond_ = std::make_shared<IntervalCondition>();
        newCond_->setObjectName(make_unique_operator_name(analysis, "interval", ""));
        intervals_ = {};

        auto conditions = getEditableConditions();
        conditions.push_back(newCond_);
        auto condInfos = getConditionInfos(conditions);

        dialog_->setConditionList(condInfos);
        dialog_->selectCondition(newCond_->getId());
        dialog_->setIntervals(intervals_);

        transitionState(State::NewInterval);
    }

    void onNewIntervalSelected(const QwtInterval &interval)
    {
        if (state_ == State::NewInterval)
        {
            // Set all intervals to the same, newly selected value.
            intervals_.resize(sink_->getNumberOfHistos());
            intervals_.fill({interval, false});

            dialog_->setIntervals(intervals_);

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
                dialog_->selectInterval(w->currentHistoIndex());

            // Transition to EditInterval state
            transitionState(State::EditInterval);
            editPicker_->setInterval(interval);
        }
        else
            assert(false);
    }

    void onIntervalModified(const QwtInterval &interval)
    {
        if (state_ == State::EditInterval)
        {
            if (dialog_->shouldEditAllIntervals())
            {
                intervals_.fill({interval, false});
            }
            else
            {
                int intervalIndex = 0;

                if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
                    intervalIndex = w->currentHistoIndex();

                if (intervalIndex < intervals_.size())
                    intervals_[intervalIndex] = { interval, false };
            }

            dialog_->setIntervals(intervals_);
            hasUnsavedChanges_ = true;
        }
        else
            assert(false);
    }

    void handleMouseWouldGrabBorder(bool wouldGrab)
    {
        if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
            zoomAction->setChecked(!wouldGrab);
    }

    void onIntervalsEditedInDialog(const QVector<IntervalConditionDialog::IntervalData> &intervals)
    {
        if (state_ == State::EditInterval)
        {
            int intervalIndex = 0;

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
                intervalIndex = w->currentHistoIndex();

            intervals_ = intervals;

            if (intervalIndex < intervals_.size())
                editPicker_->setInterval(intervals_[intervalIndex].interval.normalized());

            hasUnsavedChanges_ = true;
        }
        else
            assert(false);
    }

    void onIntervalSelectedInEditorTable(int index)
    {
        if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
        {
            w->selectHistogram(index);
        }
    }

    void onHistogramSelected(int histoIndex)
    {
        if (state_ == State::EditInterval && histoIndex < intervals_.size())
        {
            editPicker_->setInterval(intervals_[histoIndex].interval.normalized());
            dialog_->selectInterval(histoIndex);
        }
    }

    ConditionVector getEditableConditions()
    {
        if (auto analysis = sink_->getAnalysis())
            return find_conditions_for_sink(sink_, analysis->getConditions());
        return {};
    };

    std::shared_ptr<IntervalCondition> getCondition(const QUuid &objectId)
    {
        for (const auto &condPtr: getEditableConditions())
            if (condPtr->getId() == objectId)
                return std::dynamic_pointer_cast<IntervalCondition>(condPtr);
        return {};
    }

    QVector<IntervalConditionDialog::ConditionInfo> getConditionInfos(
        const ConditionVector &conditions)
    {
        QVector<IntervalConditionDialog::ConditionInfo> condInfos;

        for (const auto &cond: conditions)
            condInfos.push_back(std::make_pair(cond->getId(), cond->objectName()));

        // sort by condition name
        std::sort(std::begin(condInfos), std::end(condInfos),
                  [] (const auto &a, const auto &b)
                  {
                      return a.second < b.second;
                  });

        return condInfos;
    }

    QVector<IntervalCondition::IntervalData> getIntervals(const QUuid &id)
    {
        if (auto intervalCond = getCondition(id))
            return intervalCond->getIntervals();
        return {};
    }

    void onConditionSelected(const QUuid &id)
    {
        currentConditionId_ = id;
        intervals_ = getIntervals(id);
        dialog_->setIntervals(intervals_);

        if (newCond_ && newCond_->getId() == id)
        {
            transitionState(State::NewInterval);
        }
        else
        {
            transitionState(State::EditInterval);

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
            {
                int intervalIndex = w->currentHistoIndex();

                if (intervalIndex < intervals_.size())
                {
                    editPicker_->setInterval(intervals_[intervalIndex].interval);
                    dialog_->selectInterval(intervalIndex);
                }
            }
        }
    }
};

IntervalConditionEditorController::IntervalConditionEditorController(
            const Histo1DSinkPtr &sinkPtr,
            IPlotWidget *histoWidget,
            AnalysisServiceProvider *asp,
            QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->sink_ = sinkPtr;
    d->histoWidget_ = histoWidget;
    d->dialog_ = new IntervalConditionDialog(histoWidget);
    d->asp_ = asp;
    d->newPicker_ = new NewIntervalPicker(histoWidget->getPlot());
    d->newPicker_->setEnabled(false);
    d->editPicker_ = new IntervalEditorPicker(histoWidget->getPlot());
    d->editPicker_->setEnabled(false);

    connect(d->dialog_, &IntervalConditionDialog::applied,
            this, [this] () { d->onDialogApplied(); });

    connect(d->dialog_, &QDialog::accepted,
            this, [this] () { d->onDialogAccepted(); });

    connect(d->dialog_, &QDialog::rejected,
            this, [this] () { d->onDialogRejected(); });

    connect(d->dialog_, &IntervalConditionDialog::newConditionButtonClicked,
            this, [this] () { d->onNewConditionRequested(); });

    connect(d->dialog_, &IntervalConditionDialog::conditionSelected,
            this, [this] (const QUuid &id) { d->onConditionSelected(id); });

    connect(d->dialog_, &IntervalConditionDialog::intervalsEdited,
            this, [this] (const QVector<IntervalConditionDialog::IntervalData> &intervals)
            { d->onIntervalsEditedInDialog(intervals); });

    connect(d->dialog_, &IntervalConditionDialog::intervalSelected,
            this, [this] (int index) { d->onIntervalSelectedInEditorTable(index); });

    connect(d->newPicker_, &NewIntervalPicker::intervalSelected,
            this, [this] (const QwtInterval &interval) { d->onNewIntervalSelected(interval); });

    connect(d->editPicker_, &IntervalEditorPicker::intervalModified,
            this, [this] (const QwtInterval &interval) { d->onIntervalModified(interval); });

    connect(d->editPicker_, &IntervalEditorPicker::mouseWouldGrabIntervalBorder,
            this, [this] (bool wouldGrab) { d->handleMouseWouldGrabBorder(wouldGrab); });


    if (auto histo1DWidget = qobject_cast<Histo1DWidget *>(histoWidget))
    {
        connect(histo1DWidget, &Histo1DWidget::histogramSelected,
               this, [this] (int histoIndex) { d->onHistogramSelected(histoIndex); });
    }

    d->histoWidget_->installEventFilter(this);
    d->dialog_->installEventFilter(this);
    d->dialog_->setInfoText("Use the \"Condition Name\" controls to edit an existing or create a new condition.");

    d->updateDialogPosition();
    d->dialog_->show();

    QTimer::singleShot(250, this, [this] { d->updateDialogPosition(); });
}

IntervalConditionEditorController::~IntervalConditionEditorController()
{
    qDebug() << __PRETTY_FUNCTION__;
    delete d->dialog_;

    if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);
}

bool IntervalConditionEditorController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->histoWidget_
        && (event->type() == QEvent::Move
            || event->type() == QEvent::Resize))
    {
        d->updateDialogPosition();
    }

    return QObject::eventFilter(watched, event);
}

void IntervalConditionEditorController::setEnabled(bool on)
{
    if (on)
    {
        d->repopulateDialogFromAnalysis();
    }
    else
    {
        d->dialog_->reject();
        if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
            zoomAction->setChecked(true);
    }

    d->dialog_->setVisible(on);
}

IntervalConditionDialog *IntervalConditionEditorController::getDialog() const
{
    return d->dialog_;
}

bool IntervalConditionEditorController::hasUnsavedChanges() const
{
    return d->hasUnsavedChanges_;
}

//
// PolygonConditionDialog
//
struct PolygonConditionDialog::Private
{
    std::unique_ptr<Ui::PolygonConditionDialog> ui;
    QToolBar *toolbar_;
};

PolygonConditionDialog::PolygonConditionDialog(QWidget *parent)
    : ConditionDialogBase(parent)
    , d(std::make_unique<Private>())
{
    d->ui = std::make_unique<Ui::PolygonConditionDialog>();
    d->ui->setupUi(this);
    d->toolbar_ = make_toolbar();
    auto tb_frameLayout = make_hbox<0, 0>(d->ui->tb_frame);
    tb_frameLayout->addWidget(d->toolbar_);
    auto actionNew = d->toolbar_->addAction(QIcon(":/document-new.png"), "New");
    auto actionSave = d->toolbar_->addAction(QIcon(":/document-save.png"), "Apply");

    d->ui->tw_coords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(actionNew, &QAction::triggered,
            this, &PolygonConditionDialog::newConditionButtonClicked);

    connect(actionSave, &QAction::triggered,
            this, &PolygonConditionDialog::applied);

    connect(d->ui->combo_cond, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int /*index*/) {
                emit conditionSelected(d->ui->combo_cond->currentData().toUuid());
            });

    connect(d->ui->combo_cond, &QComboBox::editTextChanged,
            this, [this] (const QString &text) {
                emit conditionNameChanged(
                    d->ui->combo_cond->currentData().toUuid(),
                    text);
            });
}

PolygonConditionDialog::~PolygonConditionDialog()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void PolygonConditionDialog::setConditionList(const QVector<ConditionInfo> &condInfos)
{
    d->ui->combo_cond->clear();

    for (const auto &info: condInfos)
        d->ui->combo_cond->addItem(info.second, info.first);

    d->ui->combo_cond->setEditable(d->ui->combo_cond->count() > 0);
}

void PolygonConditionDialog::setPolygon(const QPolygonF &poly)
{
    d->ui->tw_coords->clearContents();
    d->ui->tw_coords->setRowCount(poly.size());
    int row = 0;
    for (const auto &point: poly)
    {
        auto xItem = new QTableWidgetItem(QString::number(point.x()));
        auto yItem = new QTableWidgetItem(QString::number(point.y()));
        d->ui->tw_coords->setItem(row, 0, xItem);
        d->ui->tw_coords->setItem(row, 1, yItem);

        auto headerItem = new QTableWidgetItem(QString::number(row));
        d->ui->tw_coords->setVerticalHeaderItem(row, headerItem);

        ++row;
    }
    d->ui->tw_coords->resizeRowsToContents();
}

QPolygonF PolygonConditionDialog::getPolygon() const
{
    QPolygonF ret;

    for (int row=0; row<d->ui->tw_coords->rowCount(); ++row)
    {
        auto xItem = d->ui->tw_coords->item(row, 0);
        auto yItem = d->ui->tw_coords->item(row, 1);

        double x = xItem->data(Qt::EditRole).toDouble();
        double y = yItem->data(Qt::EditRole).toDouble();

        ret.push_back({x, y});
    }

    return ret;

}

QString PolygonConditionDialog::getConditionName() const
{
    return d->ui->combo_cond->currentText();
}

void PolygonConditionDialog::setConditionName(const QString &newName)
{
    d->ui->combo_cond->setCurrentText(newName);
}

QToolBar *PolygonConditionDialog::getToolBar()
{
    return d->toolbar_;
}

void PolygonConditionDialog::setInfoText(const QString &txt)
{
    d->ui->label_info->setText(txt);
}

void PolygonConditionDialog::selectCondition(const QUuid &objectId)
{
    auto idx = d->ui->combo_cond->findData(objectId);

    if (idx >= 0)
        d->ui->combo_cond->setCurrentIndex(idx);
}

void PolygonConditionDialog::selectPoint(int index)
{
    if (auto item = d->ui->tw_coords->item(index, 0))
    {
        d->ui->tw_coords->setCurrentItem(item, QItemSelectionModel::Rows | QItemSelectionModel::ClearAndSelect);
    }
}

void PolygonConditionDialog::reject()
{
    d->ui->tw_coords->clearContents();
    d->ui->combo_cond->clear();
    d->ui->combo_cond->setEditable(false);
    QDialog::reject();
}

//
// PolygonConditionEditorController
//

class ModifyPolygonCommand: public QUndoCommand
{
    public:
        ModifyPolygonCommand(
            PolygonConditionEditorController::Private *cp,
            const QPolygonF &before, const QPolygonF &after)
        : cp_(cp)
        , before_(before)
        , after_(after)
        { }

    void redo() override;
    void undo() override;

    private:
        PolygonConditionEditorController::Private *cp_;
        QPolygonF before_;
        QPolygonF after_;
};

struct PolygonConditionEditorController::Private
{
    enum class State
    {
        Inactive,
        NewPolygon,
        EditPolygon,
    };

    Histo2DSinkPtr sink_;
    IPlotWidget *histoWidget_;
    PolygonConditionDialog *dialog_;
    AnalysisServiceProvider *asp_;

    // Currently selected condition id. Updated when the dialoge emits conditionSelected()
    QUuid currentConditionId_;

    // Set if a new condition is to be created. Cleared once the condition has
    // been added to the analysis via 'apply'.
    std::shared_ptr<PolygonCondition> newCond_;

    // The polygon currently being edited.
    QPolygonF poly_;

    // Interval edit state
    State state_ = State::Inactive;

    PlotPicker *newPicker_;
    PolygonEditorPicker *editPicker_;

    QwtPlotShapeItem *polyPlotItem_ = nullptr;

    QUndoStack undoStack_;
    QPolygonF polyPreModification_;

    void repopulateDialogFromAnalysis()
    {
        auto conditions = getEditableConditions();
        auto condInfos = getConditionInfos(conditions);
        dialog_->setConditionList(condInfos);
    }

    std::shared_ptr<PolygonCondition> getCondition(const QUuid &objectId)
    {
        for (const auto &condPtr: getEditableConditions())
            if (condPtr->getId() == objectId)
                return std::dynamic_pointer_cast<PolygonCondition>(condPtr);
        return {};
    }

    void transitionState(State newState)
    {
        switch (newState)
        {
            case State::Inactive:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(true);

                    dialog_->setInfoText("Click the \"New\" button to create a new polygon condition using the histograms inputs.");

                    if (polyPlotItem_)
                        polyPlotItem_->setVisible(false);
                } break;

            case State::NewPolygon:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(true);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    dialog_->setInfoText("Left-click to add polygon points, right-click to select the final point and close the polygon."
                                         " Middle-click to remove the last placed point.");

                    if (polyPlotItem_)
                        polyPlotItem_->setVisible(false);
                } break;

            case State::EditPolygon:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(true);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    auto editText = QSL(
                        "Polygon Editing:\n"
                        "- Drag points, edges or the polygon itself using the left mouse button\n"
                        "- Use the right-click context menu to insert and remove points.\n"
                        "- Use the 'Apply' button to save your changes."
                        );
                    dialog_->setInfoText(editText);

                    if (!polyPlotItem_)
                    {
                        polyPlotItem_ = new QwtPlotShapeItem;
                        polyPlotItem_->attach(histoWidget_->getPlot());
                        QBrush brush(Qt::magenta, Qt::DiagCrossPattern);
                        polyPlotItem_->setBrush(brush);
                    }
                    polyPlotItem_->setPolygon(poly_);
                    polyPlotItem_->setVisible(true);
                    dialog_->setPolygon(poly_);
                    editPicker_->setPolygon(poly_);
                } break;
        }

        histoWidget_->replot();
        state_ = newState;
    }

    void onPointsSelected(const QVector<QPointF> &points)
    {
        if (state_ == State::NewPolygon)
        {
            poly_ = { points };

            if (!poly_.isEmpty() && poly_.first() != poly_.last())
                poly_.push_back(poly_.first());

            transitionState(State::EditPolygon);
        }
    }

    void onPointAppended(const QPointF &p)
    {
        if (state_ == State::NewPolygon)
        {
            poly_.append(p);
            dialog_->setPolygon(poly_);
        }
    }

    void onPointMoved(const QPointF &p)
    {
        if (state_ == State::NewPolygon && !poly_.isEmpty())
        {
            poly_.last() = p;
            dialog_->setPolygon(poly_);
        }
    }

    void onPointRemoved(const QPointF &)
    {
        if (state_ == State::NewPolygon && !poly_.isEmpty())
        {
            poly_.pop_back();
            dialog_->setPolygon(poly_);
        }
    }

    void onBeginModifyCondition()
    {
        polyPreModification_ = poly_;
    }

    void onEndModifyCondition()
    {
        if (polyPreModification_ != poly_)
        {
            auto command = std::make_unique<ModifyPolygonCommand>(this, polyPreModification_, poly_);
            undoStack_.push(command.release());
        }
    }

    void onPolygonModified(const QVector<QPointF> &points)
    {
        if (state_ == State::EditPolygon)
        {
            poly_ = { points };
            polyPlotItem_->setPolygon(poly_);
            dialog_->setPolygon(poly_);
            editPicker_->setPolygon(poly_);
            histoWidget_->replot();
        }
    }

    void onDialogApplied()
    {
        std::shared_ptr<PolygonCondition> cond;

        if (newCond_ && newCond_->getId() == currentConditionId_)
            cond = newCond_;
        else
            cond = getCondition(currentConditionId_);

        if (!cond)
            return;

        AnalysisPauser pauser(asp_);

        cond->setObjectName(dialog_->getConditionName());
        cond->setPolygon(poly_);

        if (!cond->getAnalysis())
        {
            // It's a new condition. Connect its inputs and add it to the analysis.
            auto sinkSlot0 = sink_->getSlot(0);
            auto sinkSlot1 = sink_->getSlot(1);
            cond->connectInputSlot(0, sinkSlot0->inputPipe, sinkSlot0->paramIndex);
            cond->connectInputSlot(1, sinkSlot1->inputPipe, sinkSlot1->paramIndex);
            cond->setEventId(sink_->getEventId());
            cond->setUserLevel(sink_->getUserLevel());
            add_condition_to_analysis(asp_->getAnalysis(), cond);
        }
        else
        {
            assert(asp_->getAnalysis() == cond->getAnalysis().get());
            asp_->setAnalysisOperatorEdited(cond);
        }

        newCond_ = {};
        repopulateDialogFromAnalysis();
        dialog_->selectCondition(cond->getId());
    }

    void onDialogAccepted()
    {
        onDialogApplied();
    }

    void onDialogRejected()
    {
        transitionState(State::Inactive);
        newCond_ = {};
        poly_ = {};
        currentConditionId_ = QUuid();
    }

    // Keeps the dialog_ at the top right of the histo widget.
    void updateDialogPosition()
    {
        int x = histoWidget_->x() + histoWidget_->frameGeometry().width() + 2;
        int y = histoWidget_->y();
        dialog_->move({x, y});
        dialog_->resize(dialog_->width(), histoWidget_->height());
    }

    void onNewConditionRequested()
    {
        auto analysis = asp_->getAnalysis();
        newCond_ = std::make_shared<PolygonCondition>();
        newCond_->setObjectName(make_unique_operator_name(analysis, "poly", ""));
        poly_ = {};

        auto conditions = getEditableConditions();
        conditions.push_back(newCond_);
        auto condInfos = getConditionInfos(conditions);

        dialog_->setConditionList(condInfos);
        dialog_->selectCondition(newCond_->getId());
        dialog_->setPolygon(poly_);

        transitionState(State::NewPolygon);
    }

    QPolygonF getPolygon(const QUuid &id)
    {
        if (auto polyCond = getCondition(id))
            return polyCond->getPolygon();
        return {};
    }

    void onConditionSelected(const QUuid &id)
    {
        currentConditionId_ = id;
        poly_ = getPolygon(id);
        dialog_->setPolygon(poly_);

        if (newCond_ && newCond_->getId() == id)
        {
            transitionState(State::NewPolygon);
        }
        else
        {
            transitionState(State::EditPolygon);
        }

        undoStack_.clear();
    }

    void onActionDeleteCond()
    {
        if (auto cond = getCondition(currentConditionId_))
        {
            AnalysisPauser pauser(asp_);
            asp_->getAnalysis()->removeObjectsRecursively({ cond });
            repopulateDialogFromAnalysis();
        }
    }

    ConditionVector getEditableConditions()
    {
        if (auto analysis = sink_->getAnalysis())
            return find_conditions_for_sink(sink_, analysis->getConditions());
        return {};
    };

    QVector<IntervalConditionDialog::ConditionInfo> getConditionInfos(
        const ConditionVector &conditions)
    {
        QVector<IntervalConditionDialog::ConditionInfo> condInfos;

        for (const auto &cond: conditions)
            condInfos.push_back(std::make_pair(cond->getId(), cond->objectName()));

        // sort by condition name
        std::sort(std::begin(condInfos), std::end(condInfos),
                  [] (const auto &a, const auto &b)
                  {
                      return a.second < b.second;
                  });

        return condInfos;
    }
};

void ModifyPolygonCommand::redo()
{
    const auto &poly = after_;
    cp_->poly_ = poly;
    cp_->polyPlotItem_->setPolygon(poly);
    cp_->editPicker_->setPolygon(poly);
    cp_->dialog_->setPolygon(poly);
    cp_->histoWidget_->replot();
}

void ModifyPolygonCommand::undo()
{
    const auto &poly = before_;
    cp_->poly_ = poly;
    cp_->polyPlotItem_->setPolygon(poly);
    cp_->editPicker_->setPolygon(poly);
    cp_->dialog_->setPolygon(poly);
    cp_->histoWidget_->replot();
}

PolygonConditionEditorController::PolygonConditionEditorController(
        const Histo2DSinkPtr &sinkPtr,
        histo_ui::IPlotWidget *histoWidget,
        AnalysisServiceProvider *asp,
        QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->sink_ = sinkPtr;
    d->histoWidget_ = histoWidget;
    d->dialog_ = new PolygonConditionDialog(histoWidget);
    d->asp_ = asp;

    auto actionUndo = d->undoStack_.createUndoAction(this);
    actionUndo->setIcon(QIcon(":/arrow_left.png"));

    auto actionRedo = d->undoStack_.createRedoAction(this);
    actionRedo->setIcon(QIcon(":/arrow_right.png"));

    auto toolbar = d->dialog_->getToolBar();
    auto actionDelete = toolbar->addAction(QIcon(":/list_remove.png"), "Delete");
    toolbar->addAction(actionUndo);
    toolbar->addAction(actionRedo);
    toolbar->addAction(QIcon(":/help.png"), QSL("Help"),
                       this, mesytec::mvme::make_help_keyword_handler("Condition System"));

    d->newPicker_ = new PlotPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::PolygonRubberBand,
        QwtPicker::ActiveOnly,
        histoWidget->getPlot()->canvas());

    d->newPicker_->setStateMachine(new ImprovedPickerPolygonMachine);
    d->newPicker_->setEnabled(false);

    d->editPicker_ = new PolygonEditorPicker(histoWidget->getPlot());
    d->editPicker_->setEnabled(false);

    connect(d->dialog_, &IntervalConditionDialog::applied,
            this, [this] () { d->onDialogApplied(); });

    connect(d->dialog_, &QDialog::accepted,
            this, [this] () { d->onDialogAccepted(); });

    connect(d->dialog_, &QDialog::rejected,
            this, [this] () { d->onDialogRejected(); });

    connect(d->dialog_, &IntervalConditionDialog::newConditionButtonClicked,
            this, [this] () { d->onNewConditionRequested(); });

    connect(d->dialog_, &IntervalConditionDialog::conditionSelected,
           this, [this] (const QUuid &id) { d->onConditionSelected(id); });

    connect(d->editPicker_, &PolygonEditorPicker::beginModification,
            this, [this] { d->onBeginModifyCondition(); });

    connect(d->editPicker_, &PolygonEditorPicker::endModification,
            this, [this] { d->onEndModifyCondition(); });

    connect(actionDelete, &QAction::triggered,
            this, [this] () { d->onActionDeleteCond(); });

    [[maybe_unused]] bool b = false;

#ifdef Q_OS_WIN
    // Working around an issue where connecting QwtPicker/QwtPlotPicker signals
    // with the new method pointer syntax does not work. A warning appears on
    // the console: 'QObject::connect: signal not found in histo_ui::PlotPicker'.
    // Somehow the combination of windows, qt+moc and qwt leads to this error.
    b = connect(d->newPicker_, SIGNAL(selected(const QVector<QPointF> &)),
                this, SLOT(onPointsSelected(const QVector<QPointF> &)));
    assert(b);

    b = connect(d->newPicker_, SIGNAL(appended(const QPointF &)),
                this, SLOT(onPointAppended(const QPointF &)));
    assert(b);

    b = connect(d->newPicker_, SIGNAL(moved(const QPointF &)),
                this, SLOT(onPointMoved(const QPointF &)));
    assert(b);

    b = connect(d->newPicker_, SIGNAL(removed(const QPointF &)),
                this, SLOT(onPointRemoved(const QPointF &)));
    assert(b);
#else
    b = connect(d->newPicker_, qOverload<const QVector<QPointF> &>(&QwtPlotPicker::selected),
                this, &PolygonConditionEditorController::onPointsSelected);
    assert(b);

    b = connect(d->newPicker_, qOverload<const QPointF &>(&QwtPlotPicker::appended),
                this, &PolygonConditionEditorController::onPointAppended);
    assert(b);

    b = connect(d->newPicker_, qOverload<const QPointF &>(&QwtPlotPicker::moved),
                this, &PolygonConditionEditorController::onPointMoved);
    assert(b);

    b = connect(d->newPicker_, qOverload<const QPointF &>(&PlotPicker::removed),
                this, &PolygonConditionEditorController::onPointRemoved);
    assert(b);
#endif

    // Not a qwt signal -> always use the new style connect()
    connect(d->editPicker_, &PolygonEditorPicker::polygonModified,
            this, [this] (const QPolygonF &poly) { d->onPolygonModified(poly); });

    d->histoWidget_->installEventFilter(this);
    d->dialog_->installEventFilter(this);
    d->dialog_->setInfoText("Use the \"Condition Name\" controls to edit an existing or create a new condition.");

    d->updateDialogPosition();
    d->dialog_->show();

    QTimer::singleShot(250, this, [this] { d->updateDialogPosition(); });
}

PolygonConditionEditorController::~PolygonConditionEditorController()
{
    qDebug() << __PRETTY_FUNCTION__;
    delete d->dialog_;

    if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);
}

bool PolygonConditionEditorController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->histoWidget_
        && (event->type() == QEvent::Move
            || event->type() == QEvent::Resize))
    {
        d->updateDialogPosition();
    }

    return QObject::eventFilter(watched, event);
}

void PolygonConditionEditorController::setEnabled(bool on)
{
    if (on)
    {
        d->repopulateDialogFromAnalysis();
    }
    else
    {
        d->dialog_->reject();
        if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
            zoomAction->setChecked(true);
    }

    d->dialog_->setVisible(on);
}

PolygonConditionDialog *PolygonConditionEditorController::getDialog() const
{
    return d->dialog_;
}

// Note: these forwarding methods have only been added because of the QwtPicker
// signal problems described above. If not for these issues lambdas would have
// been used instead.

void PolygonConditionEditorController::onPointsSelected(const QVector<QPointF> &points)
{
    d->onPointsSelected(points);
}

void PolygonConditionEditorController::onPointAppended(const QPointF &p)
{
    d->onPointAppended(p);
}

void PolygonConditionEditorController::onPointMoved(const QPointF &p)
{
    d->onPointMoved(p);
}

void PolygonConditionEditorController::onPointRemoved(const QPointF &p)
{
    d->onPointRemoved(p);
}

bool edit_condition_in_sink(AnalysisServiceProvider *asp, const ConditionPtr &cond, const SinkPtr &sink)
{
    assert(cond);
    assert(sink);
    assert(asp);

    auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink);
    auto h2dSink = std::dynamic_pointer_cast<Histo2DSink>(sink);

    if (!h1dSink && !h2dSink)
        return false;

    auto widget = show_sink_widget(asp, sink);

    if (h1dSink)
    {
        auto action = widget->findChild<QAction *>("intervalConditions");
        assert(action);
        action->setChecked(true);
        auto editController = widget->findChild<IntervalConditionEditorController *>(
            "intervalConditionEditorController");
        assert(editController);
        auto editDialog = editController->getDialog();
        editDialog->selectCondition(cond->getId());
        return true;
    }
    else if (h2dSink)
    {
        auto action = widget->findChild<QAction *>("polygonConditions");
        assert(action);
        action->setChecked(true);
        auto editController = widget->findChild<PolygonConditionEditorController *>(
            "polygonConditionEditorController");
        assert(editController);
        auto editDialog = editController->getDialog();
        editDialog->selectCondition(cond->getId());
        return true;
    }

    return false;
}

bool edit_condition_in_first_available_sink(AnalysisServiceProvider *asp, const ConditionPtr &cond)
{
    assert(cond);

    const auto allSinks = cond->getAnalysis()->getSinkOperators<std::shared_ptr<SinkInterface>>();
    auto sinks = find_sinks_for_condition(cond, allSinks);

    // Sort the sinks: sinks without an active condition get priority => choses
    // the first sinks without any active condition for editing.
    std::sort(std::begin(sinks), std::end(sinks),
              [&cond](const auto &a, const auto &b)
              {
                  bool aHasConditions = !cond->getAnalysis()->getActiveConditions(a).isEmpty();
                  bool bHasConditions = !cond->getAnalysis()->getActiveConditions(b).isEmpty();

                  if (!aHasConditions && bHasConditions)
                      return true;

                  if (aHasConditions && !bHasConditions)
                      return false;

                  return a->objectName() < b->objectName();
              });

    for (auto &sink: sinks)
    {
        if (edit_condition_in_sink(asp, cond, sink))
            return true;
    }

    return false;
}


} // ns ui
} // ns analysis
