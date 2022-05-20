#include <QApplication>
#include <QFileDialog>
#include <spdlog/spdlog.h>

#include "mvme_session.h"
#include "vme_config_model_view.h"
#include "vme_config_tree.h"

using namespace mesytec::mvme;

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    mvme_init("dev_vme_config_model_view");

    std::unique_ptr<VMEConfig> vmeConfig;

    auto vmeConfigFilename = QSettings().value("LastVMEConfig").toString();

    if (!vmeConfigFilename.isEmpty())
    {
        QString errString;
        std::tie(vmeConfig, errString) = read_vme_config_from_file(vmeConfigFilename, logger);
        if (!vmeConfig)
            spdlog::error("Error loading vme config from {}: {}",
                          vmeConfigFilename.toStdString(), errString.toStdString());
    }

    if (!vmeConfig)
    {
        vmeConfigFilename = QFileDialog::getOpenFileName(
            nullptr, "Load VME config from file", {}, "VME Configs (*.vme)");
        QString errString;
        std::tie(vmeConfig, errString) = read_vme_config_from_file(vmeConfigFilename, logger);
        if (!vmeConfig)
            spdlog::error("Error loading vme config from {}: {}",
                          vmeConfigFilename.toStdString(), errString.toStdString());
    }

    QSettings().setValue("LastVMEConfig", vmeConfigFilename);

    // tree widget
    VMEConfigTreeWidget treeWidget;
    treeWidget.setConfig(vmeConfig.get());
    treeWidget.show();
    treeWidget.resize(500, 700);

    // custom model/view
    QTreeView treeView1;
    treeView1.setWindowTitle("custom model");
    EventModel eventModel1;
    treeView1.setModel(&eventModel1);
    if (vmeConfig->getEventConfigs().size())
        eventModel1.setEventConfig(vmeConfig->getEventConfigs()[0]);
    treeView1.show();
    treeView1.resize(500, 700);

    // QStandardItem model/view
    QTreeView treeView2;
    treeView2.setWindowTitle("QStandardItem model");
    EventModel2 eventModel2;
    treeView2.setModel(&eventModel2);
    if (vmeConfig->getEventConfigs().size())
        eventModel2.setEventConfig(vmeConfig->getEventConfigs()[0]);
    treeView2.show();
    treeView2.resize(500, 700);


    return app.exec();
}
