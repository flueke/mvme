#include <QApplication>
#include "mvme_session.h"
#include "listfile_recovery_wizard.h"

using namespace mesytec::mvme::listfile_recovery;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/window_icon.png"));
    mvme_init("mvme_listfile_recovery");

    ListfileRecoveryWizard wizard;
    wizard.show();

    return app.exec();
}