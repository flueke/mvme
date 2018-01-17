#ifndef __MVME_LISTFILE_BROWSER_H__
#define __MVME_LISTFILE_BROWSER_H__

#include <QComboBox>
#include <QFileSystemModel>
#include <QTableView>

class MVMEContext;
class MVMEMainWindow;

class ListfileBrowser: public QWidget
{
    Q_OBJECT
    public:
        ListfileBrowser(MVMEContext *context, MVMEMainWindow *mainWindow, QWidget *parent = nullptr);

    private:
        void onWorkspacePathChanged();
        void onGlobalStateChanged();
        void periodicUpdate();

        void onItemDoubleClicked(const QModelIndex &mi);

        MVMEContext *m_context;
        MVMEMainWindow *m_mainWindow;
        QFileSystemModel *m_fsModel;
        QTableView *m_fsView;
        QComboBox *m_analysisLoadActionCombo;
};

#endif /* __LISTFILE_BROWSER_H__ */
