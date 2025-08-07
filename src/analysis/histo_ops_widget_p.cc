#include "histo_ops_widget.h"
#include "histo_ops_widget_p.h"

#include "analysis_ui_util.h"

#include <QDragEnterEvent>
#include <QHeaderView>
#include <QMimeData>

using namespace analysis::ui;

HistoOpsEditDialog::HistoOpsEditDialog(HistogramOperationsWidget *histoOpsWidget)
    : QDialog(histoOpsWidget)
    , histoOpsWidget_(histoOpsWidget)
    , combo_operationType_(new QComboBox)
    , tw_entries_(new QTableWidget)
{
    combo_operationType_->addItem(QSL("Addition"), analysis::HistogramOperation::Operation::Sum);
    auto box = make_hbox_container("Operation", combo_operationType_);

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    make_hbox<0, 0>(toolBarFrame)->addWidget(box.container.release());

    tw_entries_->setColumnCount(1);
    tw_entries_->horizontalHeader()->setStretchLastSection(true);

    auto layout = make_vbox(this);
    layout->addWidget(toolBarFrame);
    layout->addWidget(tw_entries_);

    histoOpsWidget->installEventFilter(this);

    connect(histoOpsWidget, &QObject::destroyed, this,
            [this]
            {
                histoOpsWidget_ = nullptr;
                deleteLater();
            });

    setAcceptDrops(true);
    setMouseTracking(true);
}

bool HistoOpsEditDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == histoOpsWidget_ &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize))
    {
        updateDialogPosition();
    }

    return QObject::eventFilter(watched, event);
}

// Keeps the dialog_ at the top right of the histo widget.
void HistoOpsEditDialog::updateDialogPosition()
{
    int x = histoOpsWidget_->x() + histoOpsWidget_->frameGeometry().width() + 2;
    int y = histoOpsWidget_->y();
    move({x, y});
    resize(sizeHint().width(), histoOpsWidget_->height());
}

void HistoOpsEditDialog::refresh()
{
    if (auto histoOp = histoOpsWidget_->getHistoOp())
    {
#ifndef NDEBUG
        for (const auto &entry: histoOp->getEntries())
        {
            qDebug() << __PRETTY_FUNCTION__ << entry.sinkId << entry.elementIndex;
        }
#endif

        combo_operationType_->setCurrentIndex(
            combo_operationType_->findData(static_cast<int>(histoOp->getOperationType())));
        auto entries = histoOp->getEntries();
        if (tw_entries_->rowCount() != static_cast<qint64>(entries.size()))
        {
            tw_entries_->clearContents();
            tw_entries_->setRowCount(entries.size());
        }
        tw_entries_->setHorizontalHeaderLabels({QSL("Entry")});

        size_t row = 0;
        for (const auto &entry: entries)
        {
            QString title("unknown");
            if (auto analysis = histoOp->getAnalysis())
            {
                auto obj = histoOp->getAnalysis()->getObject(entry.sinkId);

                if (obj)
                    title = obj->objectName();

                if (entry.elementIndex >= 0)
                    title = QSL("%1[%2]").arg(title, QString::number(entry.elementIndex));
                else if (auto h1dSink = std::dynamic_pointer_cast<analysis::Histo1DSink>(obj))
                    title = QSL("%1<%2>").arg(title, QString::number(h1dSink->getNumberOfHistos()));
            }
            auto item = new QTableWidgetItem(title);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            tw_entries_->setItem(row++, 0, item);
        }

        tw_entries_->horizontalHeader()->setStretchLastSection(true);
        updateDialogPosition();
    }
}

void HistoOpsEditDialog::dragEnterEvent(QDragEnterEvent *ev)
{
    if (auto histoOp = histoOpsWidget_->getHistoOp())
    {
        handle_drag_enter(histoOp, ev);
    }

    if (!ev->isAccepted())
        QWidget::dragEnterEvent(ev);
}

void HistoOpsEditDialog::dropEvent(QDropEvent *ev)
{
    if (auto histoOp = histoOpsWidget_->getHistoOp())
    {
        if (handle_drop_event(histoOp, ev))
        {
            histoOpsWidget_->replot();
        }
    }
    // Note: deliberately not accepting the drop event here. Otherwise it will
    // _move_ nodes from the analysis trees onto this.
}

void handle_drag_enter(std::shared_ptr<analysis::HistogramOperation> &histoOp, QDragEnterEvent *ev)
{
    auto analysis = histoOp->getAnalysis();

    if (!analysis || !ev->mimeData()->hasFormat(SinkObjectRefMimeType))
    {
        return;
    }

    auto curEntryType = histoOp->getEntryType();

    auto objectRefs = decode_object_ref_list(ev->mimeData()->data(SinkObjectRefMimeType));

    for (const auto &ref: objectRefs)
    {
        if (auto sink = analysis->getObject<analysis::Histo1DSink>(ref.id))
        {
            if (curEntryType.has_value() &&
                *curEntryType != analysis::HistogramOperation::EntryType::Histo1D)
            {
                return;
            }
        }
        else if (auto sink = analysis->getObject<analysis::Histo2DSink>(ref.id))
        {
            if (curEntryType.has_value() &&
                *curEntryType != analysis::HistogramOperation::EntryType::Histo2D)
            {
                return;
            }
        }
    }

    ev->acceptProposedAction();
}

bool handle_drop_event(std::shared_ptr<analysis::HistogramOperation> &histoOp, QDropEvent *ev)
{
    bool didAddSomething = false;

    auto analysis = histoOp->getAnalysis();

    if (!analysis || !ev->mimeData()->hasFormat(SinkObjectRefMimeType))
    {
        return false;
    }

    auto objectRefs = decode_object_ref_list(ev->mimeData()->data(SinkObjectRefMimeType));

    for (const auto &ref: objectRefs)
    {
        analysis::HistogramOperation::Entry entry{ref.id, ref.index};
        // just try to add the entry. if it does not match the current type it's
        // not going to be added.
        if (histoOp->addEntry(entry))
            didAddSomething = true;
    }

    return didAddSomething;
}
