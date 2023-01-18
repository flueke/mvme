#ifndef SRC_LISTFILE_RECOVERY_WIZARD_H_
#define SRC_LISTFILE_RECOVERY_WIZARD_H_

#include <memory>
#include <QVariant>
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

        // The user selected input listfile archive
        QString inputFilePath() const { return field("inputFile").toString(); };

        // The user selected path to the output archive.
        QString outputFilePath() const { return field("outputFile").toString(); }

        // User selected optional analysis file to add to the recovered output listfile.
        QString analysisFilePath() const { return field("analysisFile").toString(); }

        // Return true if the recovery completed succesfully and produced an
        // output listfile archive.
        bool recoveryCompleted() const;

    private:
        friend class ResultPage;
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // SRC_LISTFILE_RECOVERY_WIZARD_H_