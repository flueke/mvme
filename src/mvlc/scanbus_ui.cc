#include "mvlc/scanbus_ui.hpp"

#include <QApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QtConcurrent>

#include "mvlc_vme_controller.h"
#include "qt_util.h"
#include "util/qt_threading.h"

namespace mesytec::mvme
{

struct MvlcScanbusWidget::Private
{
    explicit Private(MvlcScanbusWidget *q_)
        : q(q_)
    {
    }

    MvlcScanbusWidget *q;
    mvme_mvlc::MVLC_VMEController *mvlc_ = nullptr;

    QLineEdit *mvlcAddress_ = nullptr;
    QPushButton *scanButton_ = nullptr;
    QTableWidget *resultsTable_ = nullptr;
    QFutureWatcher<std::vector<mvlc::scanbus::VMEModuleInfo>> watcher_;
    QProgressDialog pd;

    void onControllerStateChanged(ControllerState state)
    {
        switch (state)
        {
        case ControllerState::Disconnected:
        case ControllerState::Connecting:
            q->setEnabled(false);
            break;
        case ControllerState::Connected:
            q->setEnabled(true);
            break;
        }

        auto mvlcObj = mvlc_->getMVLCObject();

        mvlcAddress_->setText(mvlcObj->connectionInfo().c_str());
    }

    void startScanbus()
    {
        auto f = QtConcurrent::run(this, &MvlcScanbusWidget::Private::doScanbus);
        watcher_.setFuture(f);
        pd.show();
    }

    std::vector<mvlc::scanbus::VMEModuleInfo> doScanbus()
    {
        try
        {
            auto mvlc = mvlc_->getMVLC();
            auto candidates = mvlc::scanbus::scan_vme_bus_for_candidates(mvlc);
            std::vector<mvlc::scanbus::VMEModuleInfo> result;

            for (auto &addr: candidates)
            {
                mvlc::scanbus::VMEModuleInfo moduleInfo{};

                if (mvlc::scanbus::read_module_info(mvlc, addr, moduleInfo))
                    continue; // ignore errors

                result.emplace_back(std::move(moduleInfo));
            }

            return result;
        }
        catch (...)
        {
            throw QtExceptionPtr(std::current_exception());
        }
    }

    void onScanbusDone()
    {
        resultsTable_->clearContents();
        resultsTable_->setRowCount(0);

        try
        {
            auto result = watcher_.result();

            resultsTable_->setRowCount(result.size());
            resultsTable_->setColumnCount(5);
            resultsTable_->setHorizontalHeaderLabels(
                {"Address", "Hardware ID", "Firmware Version", "Module Type", "Firmware Type"});

            unsigned currentRow = 0;

            for (const auto &moduleInfo: result)
            {
                mvlc::u32 vmeAddress = moduleInfo.address;
                auto item0 = new QTableWidgetItem(tr("0x%1").arg(vmeAddress, 8, 16, QLatin1Char('0')));
                auto item1 =
                    new QTableWidgetItem(tr("0x%1").arg(moduleInfo.hwId, 4, 16, QLatin1Char('0')));
                auto item2 =
                    new QTableWidgetItem(tr("0x%1").arg(moduleInfo.fwId, 4, 16, QLatin1Char('0')));
                auto item3 = new QTableWidgetItem(moduleInfo.moduleTypeName().c_str());
                auto item4 = new QTableWidgetItem(moduleInfo.mdppFirmwareTypeName().c_str());

                auto items = {item0, item1, item2, item3, item4};
                unsigned col = 0;

                for (auto item: items)
                {
                    item->setData(Qt::UserRole, vmeAddress);
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                    resultsTable_->setItem(currentRow, col++, item);
                }

                ++currentRow;
            }

            resultsTable_->resizeColumnsToContents();
            resultsTable_->resizeRowsToContents();
        }
        catch (const std::exception &e)
        {
            QMessageBox::critical(q, "VME Bus Scan Error",
                                  QString("An error occurred during VME bus scan:\n%1").arg(e.what()));
        }
    }

    void resultsTableContextMenu(const QPoint &pos)
    {
        QMenu menu(resultsTable_);
        auto actionCopy = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy");
        actionCopy->setShortcut(QKeySequence::Copy);
        QObject::connect(actionCopy, &QAction::triggered, resultsTable_,
                         [this]
                         {
                             if (auto item = resultsTable_->currentItem())
                                 QApplication::clipboard()->setText(item->text());
                         });
        menu.exec(resultsTable_->mapToGlobal(pos));
    }
};

MvlcScanbusWidget::MvlcScanbusWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>(this))
{
    setWindowTitle("Scan VME Bus");
    resize(600, 400);

    d->mvlcAddress_ = new QLineEdit;
    d->mvlcAddress_->setReadOnly(true);
    d->scanButton_ = new QPushButton("Start VME bus scan");
    d->resultsTable_ = new QTableWidget;
    d->resultsTable_->verticalHeader()->setVisible(false);
    d->resultsTable_->horizontalHeader()->setStretchLastSection(true);
    d->resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    d->resultsTable_->setSelectionBehavior(QAbstractItemView::SelectItems);
    d->resultsTable_->setContextMenuPolicy(Qt::CustomContextMenu);

    d->pd.setLabelText("Scanning VME bus...");
    d->pd.setRange(0, 0);
    d->pd.setCancelButton(nullptr);
    d->pd.setWindowTitle("Scan VME Bus");

    connect(&d->watcher_, &QFutureWatcher<std::vector<mvlc::scanbus::VMEModuleInfo>>::finished,
            this, [this]
            {
                d->pd.close();
                d->onScanbusDone();
            });

    auto gb = new QGroupBox("Bus scan results");
    auto gbLayout = make_vbox(gb);
    gbLayout->addWidget(d->resultsTable_);

    auto layout = new QFormLayout(this);
    layout->addRow("MVLC Address", d->mvlcAddress_);
    layout->addRow(d->scanButton_);
    layout->addRow(gb);

    connect(d->scanButton_, &QPushButton::clicked, this, [this] { d->startScanbus(); });

    connect(d->resultsTable_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { d->resultsTableContextMenu(pos); });
}

MvlcScanbusWidget::~MvlcScanbusWidget()
{
    if (d->watcher_.isRunning())
        d->watcher_.waitForFinished();
}

void MvlcScanbusWidget::setMvlc(mvme_mvlc::MVLC_VMEController *mvlc)
{
    disconnect(d->mvlc_, nullptr, this, nullptr);
    d->mvlc_ = mvlc;

    if (d->mvlc_)
    {
        connect(d->mvlc_, &mvme_mvlc::MVLC_VMEController::controllerStateChanged, this,
                [this](ControllerState state) { d->onControllerStateChanged(state); });

        connect(d->mvlc_, &QObject::destroyed, this, [this] { d->mvlc_ = nullptr; });

        d->onControllerStateChanged(d->mvlc_->getState());
    }

    d->resultsTable_->clearContents();
    d->resultsTable_->setRowCount(0);
}

} // namespace mesytec::mvme
