#include "listfile_recovery_wizard.h"
#include "listfile_recovery_wizard_p.h"

#include <mesytec-mvlc/util/fmt.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QtConcurrent>
#include <sstream>

#include "util/qt_str.h"
#include "listfile_recovery.h"

namespace mesytec::mvme::listfile_recovery
{

// FIXME: move this into mvme_workspace.h and fix the awful function in there
QSettings make_workspace_settings()
{
    return QSettings("mvmeworkspace.ini", QSettings::IniFormat);
}

IntroPage::IntroPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Intro");
}

//bool IntroPage::isComplete() const
//{
//    return true;
//}

InputsPage::InputsPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Input Selection");

    auto le_inputFile = new QLineEdit;
    auto le_analysisFile = new QLineEdit;
    auto le_outputFile = new QLineEdit;

    le_inputFile->setReadOnly(true);
    le_analysisFile->setReadOnly(true);
    le_outputFile->setReadOnly(true);

    auto pb_selInputFile = new QPushButton(QIcon(":/document-open.png"), "Select");
    auto pb_selAnalysis = new QPushButton(QIcon(":/document-open.png"), "Select");
    auto pb_selOutputFile = new QPushButton(QIcon(":/document-open.png"), "Select");

    auto l = new QGridLayout(this);
    int row = 0;
    l->addWidget(new QLabel("Input Listfile"), row, 0);
    l->addWidget(le_inputFile, row, 1);
    l->addWidget(pb_selInputFile, row, 2);
    ++row;

    l->addWidget(new QLabel("Optional Analysis"), row, 0);
    l->addWidget(le_analysisFile, row, 1);
    l->addWidget(pb_selAnalysis, row, 2);
    ++row;

    l->addWidget(new QLabel("Output Listfile"), row, 0);
    l->addWidget(le_outputFile, row, 1);
    l->addWidget(pb_selOutputFile, row, 2);
    ++row;

    registerField("inputFile*", le_inputFile);
    registerField("analysisFile", le_analysisFile);
    registerField("outputFile*", le_outputFile);

    connect(pb_selInputFile, &QPushButton::clicked,
            this, [this]
            {
                auto fn = QFileDialog::getOpenFileName(this, "Select input listfile",
                    make_workspace_settings().value("ListFileDirectory").toString(),
                    "mvme listfiles (*.zip)");

                if (!fn.isEmpty())
                    setField("inputFile", fn);
            });

    connect(pb_selAnalysis, &QPushButton::clicked,
            this, [this]
            {
                auto fn = QFileDialog::getOpenFileName(this, "Select analysis file",
                    {},
                    "mvme analysis file (*.analysis)");

                if (!fn.isEmpty())
                    setField("analysisFile", fn);
            });

    connect(pb_selOutputFile, &QPushButton::clicked,
            this, [this]
            {
                QString suggestedFilePath;
                if (auto inputFilePath = field("inputFile").toString();
                    !inputFilePath.isEmpty())
                {
                    QFileInfo fi(inputFilePath);
                    suggestedFilePath = QSL("%1/%2-recovered.zip")
                        .arg(fi.path())
                        .arg(fi.completeBaseName());
                }
                else
                {
                    suggestedFilePath = make_workspace_settings().value("ListFileDirectory").toString();
                }

                QFileDialog fd(this, "Select output listfile",
                    suggestedFilePath,
                    "mvme listfiles (*.zip)");
                fd.setDefaultSuffix(".zip");
                fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

                if (fd.exec() == QDialog::Accepted && !fd.selectedFiles().isEmpty())
                    setField("outputFile", fd.selectedFiles().front());
            });
}

InputInfoPage::InputInfoPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Input Info");
    infoLabel_ = new QLabel;
    auto l = new QHBoxLayout(this);
    l->addWidget(infoLabel_);
}

bool InputInfoPage::isComplete() const
{
    return entryInfo_.dataStartOffset > 0 && !entryInfo_.entryName.empty();
}

void InputInfoPage::initializePage()
{
    try
    {
        entryInfo_ = {};
        auto foundEntry = find_first_entry(field("inputFile").toString().toStdString());

        if (!foundEntry.entryName.empty())
        {
            std::stringstream ss;
            ss << "Found possible listfile entry:\n\n";
            ss << fmt::format("  Filename: {}\n", foundEntry.entryName);
            ss << fmt::format("  Header Offset: {} bytes\n", foundEntry.headerOffset);
            ss << fmt::format("  Data Offset: {} bytes\n", foundEntry.dataStartOffset);
            ss << "\nIf the above looks sensible press the 'Next' button to start the recovery process.\n";

            infoLabel_->setText(QString::fromStdString(ss.str()));
            entryInfo_ = foundEntry;
        }
        else
        {
            infoLabel_->setText("Error: could not find a listfile entry inside the input archive.");
        }
    }
    catch (const std::exception &e)
    {
        std::stringstream ss;
        ss << "Listfile detection failed:\n\n";
        ss << e.what() << "\n";
        infoLabel_->setText(QString::fromStdString(ss.str()));
    }

    emit completeChanged();
}

RunPage::RunPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Working");
    progressBar_ = new QProgressBar;
    auto l = new QHBoxLayout(this);
    l->addWidget(progressBar_);

    connect(&watcher_, &QFutureWatcher<RecoveryProgress>::finished,
            this, &RunPage::onRecoveryFinished);

    connect(&updateTimer_, &QTimer::timeout,
            this, [this]
            {
                auto p = progress_.copy();
                progressBar_->setRange(0, p.inputFileSize);
                progressBar_->setValue(p.inputBytesRead);
            });

    updateTimer_.setInterval(500);
    updateTimer_.start();
}

bool RunPage::isComplete() const
{
}

void RunPage::initializePage()
{
    // get the entryinfo from the inputinfopage. will require a fugly dynamic cast
    // future = qtconcurrent run
    // setup future watcher, once the future is done emit completeChanged()
    // setup a timer callback to peridocally update the progressbar with the info from recover_listfile()

    auto inputFilename = field("inputFile").toString().toStdString();
    auto outputFilename = field("outputFile").toString().toStdString();
    auto analysisFilename = field("analysisFile").toString().toStdString();
    auto entryInfo = qobject_cast<InputInfoPage *>(wizard()->page(InputInfoPageId))->entryInfo();

    auto perform_recovery = [=] () -> RecoveryProgress
    {
        // TODO: try catch block and return an object containing possible exception info
        auto result = recover_listfile(
            inputFilename,
            outputFilename,
            entryInfo,
            progress_);

        // TODO: reopen the output zip archive and add the analysis file to it.

        return result;
    };

    // XXX: leftoff here somewhere

    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    future_ = QtConcurrent::run(perform_recovery);
    watcher_.setFuture(future_);
}

ResultPage::ResultPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Recovery Results");
}

bool ResultPage::isComplete() const
{
}

struct ListfileRecoveryWizard::Private
{
};

enum Pages
{
    IntroPageId,
    InputsPageId,
    InputInfoPageId,
    RunPageId,
    ResultPageId,
};

ListfileRecoveryWizard::ListfileRecoveryWizard(QWidget *parent)
    : QWizard(parent)
    , d(std::make_unique<Private>())
{
    setPage(Pages::IntroPageId, new IntroPage);
    setPage(Pages::InputsPageId, new InputsPage);
    setPage(Pages::InputInfoPageId, new InputInfoPage);
    setPage(Pages::RunPageId, new RunPage);
    setPage(Pages::ResultPageId, new ResultPage);
}

ListfileRecoveryWizard::~ListfileRecoveryWizard()
{
}

}