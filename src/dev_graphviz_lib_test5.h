#ifndef __DEV_GRAPHVIZ_LIB_TEST5_H__
#define __DEV_GRAPHVIZ_LIB_TEST5_H__

#include <QMainWindow>
#include <QUuid>

namespace Ui { class MainWindow; }

class MainWindow: public QMainWindow
{
    Q_OBJECT
    public:
        MainWindow();
        ~MainWindow() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
        Ui::MainWindow *ui_;
};

#endif /* __DEV_GRAPHVIZ_LIB_TEST5_H__ */
