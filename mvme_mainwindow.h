#ifndef UUID_0bf3a86c_9a9e_422c_9b9b_6f77a8aec378
#define UUID_0bf3a86c_9a9e_422c_9b9b_6f77a8aec378

#include <QMainWindow>

namespace Ui
{
class MainWindow;
}

class MVMEContext;

class MVMEMainWindow: public QMainWindow
{
    Q_OBJECT
    public:
        MVMEMainWindow();
        ~MVMEMainWindow();

    private slots:
        void on_actionAdd_VME_Module_triggered();
        void on_actionAdd_Mesytec_Chain_triggered();
        void on_actionAdd_VMUSB_Stack_triggered();

    private:
        Ui::MainWindow *ui;
        MVMEContext *m_context;
};

#endif
