#include "mvme_mainwindow.h"
#include "ui_mvme_mainwindow.h"
#include "mvme_context.h"
#include "vme_module.h"

#include <QDockWidget>
#include <QToolBar>
#include <QDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>

struct AddVMEModuleDialog: public QDialog
{
    AddVMEModuleDialog(MVMEContext *context, QWidget *parent = 0)
        : QDialog(parent)
    {
        QStringList moduleTypes = {
            "Unknown",
            "MADC",
            "MQDC",
            "MTDC",
            "MDPP16",
            "MDPP32",
            "MDI12"
        };

        typeCombo = new QComboBox;
        typeCombo->addItems(moduleTypes);

        nameEdit = new QLineEdit;
        addressEdit = new QLineEdit;

        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto layout = new QFormLayout(this);
        layout->addRow("Type", typeCombo);
        layout->addRow("Name", nameEdit);
        layout->addRow("Address", addressEdit); 
        layout->addRow(bb);
    }

    QComboBox *typeCombo;
    QLineEdit *nameEdit;
    QLineEdit *addressEdit;
};

MVMEMainWindow::MVMEMainWindow()
    : ui(new Ui::MainWindow)
    , m_context(new MVMEContext)
{
    ui->setupUi(this);

    auto contextWidget = new MVMEContextWidget(m_context);
    auto contextDock = new QDockWidget("Configuration");
    contextDock->setWidget(contextWidget);
    addDockWidget(Qt::LeftDockWidgetArea, contextDock);
}

MVMEMainWindow::~MVMEMainWindow()
{
    delete m_context;
}

void MVMEMainWindow::on_actionAdd_VME_Module_triggered()
{
    AddVMEModuleDialog dialog(m_context, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        bool ok;
        VMEModuleType moduleType = static_cast<VMEModuleType>(dialog.typeCombo->currentIndex());
        QString name = dialog.nameEdit->text();
        uint32_t baseAddress = dialog.addressEdit->text().toUInt(&ok);
        HardwareModule *module = 0;

        switch (moduleType)
        {
            case VMEModuleType::Unknown:
                module = new GenericModule(baseAddress, name);
                break;
            case VMEModuleType::MADC32:
                module = new MADC32(baseAddress, name);
                break;
            case VMEModuleType::MQDC32:
                module = new MQDC32(baseAddress, name);
                break;
            case VMEModuleType::MTDC32:
                module = new MTDC32(baseAddress, name);
                break;
            case VMEModuleType::MDPP16:
                module = new MDPP16(baseAddress, name);
                break;
            case VMEModuleType::MDPP32:
                module = new MDPP32(baseAddress, name);
                break;
            case VMEModuleType::MDI2:
                module = new MDI2(baseAddress, name);
                break;
        }

        m_context->addModule(module);
    }
}

void MVMEMainWindow::on_actionAdd_Mesytec_Chain_triggered()
{
}

void MVMEMainWindow::on_actionAdd_VMUSB_Stack_triggered()
{
}
