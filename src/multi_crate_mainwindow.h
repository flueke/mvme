#ifndef __MVME_MULTI_CRATE_MAINWINDOW_H__
#define __MVME_MULTI_CRATE_MAINWINDOW_H__

#include <QMainWindow>
#include "libmvme_export.h"

class LIBMVME_EXPORT MultiCrateMainWindow: public QMainWindow
{
    Q_OBJECT

    public:
        MultiCrateMainWindow(QWidget *parent = nullptr);
        ~MultiCrateMainWindow();

        void closeEvent(QCloseEvent *event) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* _MVME__MULTI_CRATE_MAINWINDOW_H__ */

