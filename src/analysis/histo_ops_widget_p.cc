#include "histo_ops_widget.h"
#include "histo_ops_widget_p.h"

#include <QHeaderView>

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

    connect(histoOpsWidget, &QObject::destroyed, this, [this]
    {
        histoOpsWidget_ = nullptr;
        deleteLater();
    });
}

bool HistoOpsEditDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == histoOpsWidget_
        && (event->type() == QEvent::Move
            || event->type() == QEvent::Resize))
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
    resize(width(), histoOpsWidget_->height());
}

void HistoOpsEditDialog::refresh()
{
    if (auto histoOp = histoOpsWidget_->getHistoOp())
    {
        combo_operationType_->setCurrentIndex(combo_operationType_->findData(
            static_cast<int>(histoOp->getOperationType())));
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
                if (auto obj = histoOp->getAnalysis()->getObject(entry.sinkId))
                    title = obj->objectName();
            }
            auto item = new QTableWidgetItem(title);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            tw_entries_->setItem(row++, 0, item);
        }
    }
}
