#include "listfile_browser.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"

#include <QHeaderView>
#include <QBoxLayout>

ListfileBrowser::ListfileBrowser(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_fsModel(new QFileSystemModel(this))
    , m_fsView(new QTableView(this))
    , m_analysisLoadActionCombo(new QComboBox(this))
{
    setWindowTitle(QSL("Listfile Browser"));

    set_widget_font_pointsize(this, 8);

    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

    m_fsView->setModel(m_fsModel);
    m_fsView->verticalHeader()->hide();
    m_fsView->hideColumn(2); // Hides the file type column
    m_fsView->setSortingEnabled(true);

    auto widgetLayout = new QVBoxLayout(this);

    // On listfile load
    {
        auto label = new QLabel(QSL("On listfile load"));
        auto combo = m_analysisLoadActionCombo;
        combo->addItem(QSL("keep current analysis"),        0u);
        combo->addItem(QSL("load analysis from listfile"),  OpenListfileFlags::LoadAnalysis);

        auto layout = new QHBoxLayout;
        layout->addWidget(label);
        layout->addWidget(combo);
        layout->addStretch();

        widgetLayout->addLayout(layout);
    }

    widgetLayout->addWidget(m_fsView);

    connect(m_context, &MVMEContext::workspaceDirectoryChanged,
            this, [this](const QString &) { updateWidget(); });

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        m_fsView->resizeColumnsToContents();
        m_fsView->resizeRowsToContents();
    });

    connect(m_fsView, &QAbstractItemView::doubleClicked,
            this, &ListfileBrowser::onItemDoubleClicked);

    updateWidget();
}

void ListfileBrowser::updateWidget()
{
    auto workspaceDirectory = m_context->getWorkspaceDirectory();
    auto workspaceSettings  = m_context->makeWorkspaceSettings();

    QDir dir(workspaceDirectory);
    QString listfileDirectory = dir.filePath(
        workspaceSettings->value(QSL("ListFileDirectory")).toString());

    m_fsModel->setRootPath(listfileDirectory);
    m_fsView->setRootIndex(m_fsModel->index(listfileDirectory));
}

void ListfileBrowser::onItemDoubleClicked(const QModelIndex &mi)
{
    auto filename = m_fsModel->filePath(mi);
    u16 flags = m_analysisLoadActionCombo->currentData().toUInt(0);

    auto openResult = open_listfile(m_context, filename, flags);
}
