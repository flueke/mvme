#include "listfile_browser.h"
#include "mvme_context.h"

#include <QHeaderView>
#include <QBoxLayout>

ListfileBrowser::ListfileBrowser(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_fsModel(new QFileSystemModel(this))
    , m_fsView(new QTableView(this))
{
    setWindowTitle(QSL("Listfile Browser"));

    set_widget_font_pointsize(this, 8);

    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

    m_fsView->setModel(m_fsModel);
    m_fsView->verticalHeader()->hide();
    m_fsView->hideColumn(2); // Hides the file type column
    m_fsView->setSortingEnabled(true);

    auto layout = new QVBoxLayout(this);

    layout->addWidget(m_fsView);

    connect(m_context, &MVMEContext::workspaceDirectoryChanged,
            this, [this](const QString &) { updateWidget(); });

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        m_fsView->resizeColumnsToContents();
        m_fsView->resizeRowsToContents();
    });

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
