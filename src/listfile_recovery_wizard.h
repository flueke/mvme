#ifndef SRC_LISTFILE_RECOVERY_WIZARD_H_
#define SRC_LISTFILE_RECOVERY_WIZARD_H_

#include <memory>
#include <QWizard>
#include "libmvme_export.h"

namespace mesytec::mvme::listfile_recovery
{

class LIBMVME_EXPORT ListfileRecoveryWizard: public QWizard
{
    Q_OBJECT
    public:
        ListfileRecoveryWizard(QWidget *parent = nullptr);
        ~ListfileRecoveryWizard() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // SRC_LISTFILE_RECOVERY_WIZARD_H_