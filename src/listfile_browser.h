#ifndef __MVME_LISTFILE_BROWSER_H__
#define __MVME_LISTFILE_BROWSER_H__

#include <QFileSystemModel>
#include <QTableView>

class MVMEContext;

class ListfileBrowser: public QWidget
{
    Q_OBJECT
    public:
        ListfileBrowser(MVMEContext *context, QWidget *parent = nullptr);

    private:
        void updateWidget();

        MVMEContext *m_context;

        QFileSystemModel *m_fsModel;
        QTableView *m_fsView;
};

#endif /* __LISTFILE_BROWSER_H__ */
