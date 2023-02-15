#ifndef SRC_LISTFILE_RECOVERY_WIZARD_P_H_
#define SRC_LISTFILE_RECOVERY_WIZARD_P_H_

#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
#include <QWizardPage>

#include "listfile_recovery.h"

class QLabel;
class QProgressBar;

namespace mesytec::mvme::listfile_recovery
{

class IntroPage: public QWizardPage
{
    Q_OBJECT
    public:
        IntroPage(QWidget *parent = nullptr);
        //bool isComplete() const override;
};

class InputsPage: public QWizardPage
{
    Q_OBJECT
    public:
        InputsPage(QWidget *parent = nullptr);

        void cleanupPage() override;
};

class InputInfoPage: public QWizardPage
{
    Q_OBJECT
    public:
        InputInfoPage(QWidget *parent = nullptr);
        bool isComplete() const override;
        void initializePage() override;

        EntryFindResult entryInfo() const { return entryInfo_; }

    private:
        QLabel *infoLabel_ = {};
        EntryFindResult entryInfo_ = {};
};

class RunPage: public QWizardPage
{
    Q_OBJECT
    public:
        RunPage(QWidget *parent = nullptr);
        bool isComplete() const override;
        void initializePage() override;

    private:
        friend class ResultPage;
        void onRecoveryFinished();
        QProgressBar *progressBar_ = {};
        mesytec::mvlc::Protected<RecoveryProgress> progress_;
        QFutureWatcher<RecoveryProgress> watcher_;
        QFuture<RecoveryProgress> future_;
        QTimer updateTimer_;

};

class ResultPage: public QWizardPage
{
    Q_OBJECT
    public:
        ResultPage(QWidget *parent = nullptr);
        bool isComplete() const override;
        void initializePage() override;

    private:
        QLabel *infoLabel_ = {};
        RecoveryProgress recoveryResult_;
};

}

#endif // SRC_LISTFILE_RECOVERY_WIZARD_P_H_