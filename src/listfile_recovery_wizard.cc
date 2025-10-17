#include "listfile_recovery_wizard.h"
#include "listfile_recovery_wizard_p.h"

#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <QFile>
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
#include <quazipfile.h>
#include <quazip.h>
#include <sstream>

#include "listfile_recovery.h"
#include "mvme_workspace.h"
#include "util/qt_str.h"
#include "util/qt_threading.h"

// FIXME: failed recovery will leave the output zip file behind!

namespace mesytec::mvme::listfile_recovery
{

enum Pages
{
    IntroPageId,
    InputsPageId,
    InputInfoPageId,
    RunPageId,
    ResultPageId,
};

IntroPage::IntroPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Intro");
    auto infoLabel = new QLabel;
    auto l = new QHBoxLayout(this);
    l->addWidget(infoLabel);

    infoLabel->setWordWrap(true);
    infoLabel->setText(
        "This dialog runs the mvme listfile recovery procedure, trying to " \
        "recover recorded DAQ data from corrupted listfile ZIP archives.\n\n" \

        "In case of a DAQ crash or unexpected computer shutdown, the corrupted ZIP "
        "will contain a single file: the raw listmode file (.mvmelst or " \
        ".mvlclst). The recovery procedure tries to detect the file name, the " \
        "compression type and the start of the file. The following data is then " \
        "unpacked and written to a new output ZIP archive.\n\n" \

        "Optionally an analysis file can be selected which will be added to the " \
        "newly created output ZIP file." \
        );
}

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

void InputsPage::cleanupPage()
{
    // Do nothing here so that field values are kept on going back one page. The
    // default implementation would reset the values.
}

InputInfoPage::InputInfoPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Input Info");
    setCommitPage(true);
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
    wizard()->setButtonText(QWizard::CommitButton, "Start Recovery");
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
            ss << "\nIf the above looks sensible press the 'Start Recovery' button to start the recovery process.\n";

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
    setSubTitle("Recovering listfile data");
    setCommitPage(true);
    progressBar_ = new QProgressBar;
    auto l = new QHBoxLayout(this);
    l->addWidget(progressBar_);

    connect(&watcher_, &QFutureWatcher<RecoveryProgress>::finished,
            this, &RunPage::onRecoveryFinished);

    connect(&updateTimer_, &QTimer::timeout,
            this, [this]
            {
                auto p = progress_.copy();
                auto donePercent = (p.inputFileSize > 0
                    ? static_cast<double>(p.inputBytesRead) / static_cast<double>(p.inputFileSize) * 100.0
                    : 0.0);
                progressBar_->setRange(0, 100);
                progressBar_->setValue(donePercent);
            });
}

bool RunPage::isComplete() const
{
    return future_.isFinished();
}

void RunPage::initializePage()
{
    // get the entryinfo from the inputinfopage. will require a fugly dynamic cast
    // future = qtconcurrent run
    // setup future watcher, once the future is done emit completeChanged()
    // setup a timer callback to peridocally update the progressbar with the info from recover_listfile()

    wizard()->button(QWizard::CancelButton)->setEnabled(false);

    auto inputFilename = field("inputFile").toString();
    auto outputFilename = field("outputFile").toString();
    auto analysisFilename = field("analysisFile").toString();
    auto entryInfo = qobject_cast<InputInfoPage *>(wizard()->page(InputInfoPageId))->entryInfo();

    auto perform_recovery = [=] () -> RecoveryProgress
    {
        try
        {
            auto result = recover_listfile(
                inputFilename.toStdString(),
                outputFilename.toStdString(),
                entryInfo,
                progress_);

            if (!analysisFilename.isEmpty())
            {
                // Add the user selected analysis file to the archive. Pretty
                // hacky solution: the output zip is reopened again using the
                // QuaZip API and the analysis file is added. An alternative
                // solution would be to provide a different version of
                // recover_listfile() taking in a ZipCreator as an argument or
                // returning the internally created ZipCreator instance, then
                // using the creator to add the analysis file.
                QFile analysisFile(analysisFilename);
                if (!analysisFile.open(QIODevice::ReadOnly))
                    throw std::runtime_error(fmt::format("Error opening analysis file for reading: {}",
                        analysisFile.errorString().toStdString()));

                auto analysisData = analysisFile.readAll();

                QuaZip archive;
                archive.setZipName(outputFilename);
                archive.setZip64Enabled(true);

                if (!archive.open(QuaZip::mdAdd))
                    throw std::runtime_error(fmt::format("Error opening output archive: error={}", archive.getZipError()));

                QuaZipFile out(&archive);
                QuaZipNewInfo zipFileInfo("analysis.analysis");
                zipFileInfo.setPermissions(static_cast<QFile::Permissions>(0x6644));

                if (!out.open(QIODevice::WriteOnly, zipFileInfo,
                            nullptr, 0,   // password, crc
                            0,            // method (Z_DEFLATED or 0 for no compression)
                            0             // compression level
                            ))
                {
                    throw std::runtime_error(fmt::format("Error adding analysis to output file: {}", archive.getZipError()));
                }

                out.write(analysisData);
            }

            return result;
        } catch (...)
        {
            throw QtExceptionPtr(std::current_exception());
        }
    };

    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    future_ = QtConcurrent::run(perform_recovery);
    watcher_.setFuture(future_);
    updateTimer_.setInterval(500);
    updateTimer_.start();
}

void RunPage::onRecoveryFinished()
{
    updateTimer_.stop();
    emit completeChanged();
    wizard()->next();
}

struct ListfileRecoveryWizard::Private
{
    bool recoveryCompleted_ = false;
};

ResultPage::ResultPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Listfile Recovery");
    setSubTitle("Recovery Results");

    infoLabel_ = new QLabel;
    auto l = new QHBoxLayout(this);
    l->addWidget(infoLabel_);
}

bool ResultPage::isComplete() const
{
    return true;
}

void ResultPage::initializePage()
{
    using mesytec::mvlc::util::Megabytes;

    auto inputFilename = field("inputFile").toString();
    auto outputFilename = field("outputFile").toString();
    auto analysisFilename = field("analysisFile").toString();
    auto entryInfo = qobject_cast<InputInfoPage *>(wizard()->page(InputInfoPageId))->entryInfo();
    auto inputSizeMiB = static_cast<double>(QFileInfo(inputFilename).size()) / Megabytes(1);

    std::stringstream ss;

    ss << "Recovery Info:\n\n";
    ss << fmt::format("  Input archive: {}\n", inputFilename.toStdString());
    ss << fmt::format("  Input archive size: {:.2f} MB\n", inputSizeMiB);
    ss << fmt::format("  Input entry name: {}\n", entryInfo.entryName);
    ss << fmt::format("  Output archive: {}\n", outputFilename.toStdString());

    if (!analysisFilename.isEmpty())
        ss << fmt::format("  Additional analysis file: {}\n", analysisFilename.toStdString());

    ss << "\nRecovery Result:\n\n";

    try
    {
        auto recoveryResult = qobject_cast<RunPage *>(wizard()->page(RunPageId))->future_.result();
        auto recoveredMiB = static_cast<double>(recoveryResult.inputBytesRead) / Megabytes(1);
        ss << fmt::format("  Recovered {:.2f} MB from input archive.\n", recoveredMiB);
        qobject_cast<ListfileRecoveryWizard *>(wizard())->d->recoveryCompleted_ = true;
    }
    catch (const std::exception &e)
    {
        ss << fmt::format("  Error from the recovery procedure: {}\n", e.what());
        qobject_cast<ListfileRecoveryWizard *>(wizard())->d->recoveryCompleted_ = false;

        // Cleanup the output file
        QFile outputFile(outputFilename);
        if (outputFile.exists())
            outputFile.remove();
    }

    infoLabel_->setText(QString::fromStdString(ss.str()));
}

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

bool ListfileRecoveryWizard::recoveryCompleted() const
{
    return d->recoveryCompleted_;
}

}
