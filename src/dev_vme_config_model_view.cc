#include <iostream>
#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QToolBar>
#include <spdlog/spdlog.h>

#include "multi_crate.h"
#include "mvme_session.h"
#include "util/qt_fs.h"
#include "vme_config_model_view.h"
#include "vme_config_tree.h"
#include "vme_config_util.h"
#include "util/qt_model_view_util.h"

using namespace mesytec;
using namespace mesytec::mvme;
using namespace mesytec::multi_crate;

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    mvme_init("dev_vme_config_model_view");

    if (app.arguments().size() < 2)
    {
        std::cerr << "Error: missing config file path argument\n";
        return 1;
    }

    auto doc = read_json_file(app.arguments().at(1));
    auto config = mvme::vme_config::deserialize_object(doc.object());

    if (!config)
    {
        std::cerr << "Error reading config from " << app.arguments().at(1).toLocal8Bit().data() << "\n";
        return 1;
    }

    qDebug() << "read config object: " << config.get();

    #if 0
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
    #endif

#if 0
    auto vmeConfig = qobject_cast<VMEConfig *>(config.get());
    VMEConfigTreeWidget treeWidget;
    if (vmeConfig)
    {
        // old style tree widget
        treeWidget.setConfig(vmeConfig);
        treeWidget.show();
        treeWidget.resize(500, 700);
    }
#endif

#if 1
    auto treeView3 = new QTreeView();
    auto itemModel = std::make_unique<VmeConfigItemModel>();
    auto controller = std::make_unique<VmeConfigItemController>();
    treeView3->setModel(itemModel.get());
    controller->setModel(itemModel.get());
    controller->addView(treeView3);
    itemModel->setRootObject(config.get());
    treeView3->setEditTriggers(QAbstractItemView::EditKeyPressed);
    treeView3->setDefaultDropAction(Qt::MoveAction); // internal DnD
    treeView3->setDragDropMode(QAbstractItemView::DragDrop); // external DnD
    treeView3->setDragDropOverwriteMode(false);
    treeView3->setDragEnabled(true);
    treeView3->setRootIndex(itemModel->invisibleRootItem()->child(0)->index());
    treeView3->show();
    treeView3->resize(500, 700);
    treeView3->resizeColumnToContents(0);

    QObject::connect(treeView3, &QTreeView::clicked, [&] (const QModelIndex &index)
    {
        auto item = itemModel->itemFromIndex(index);
        qDebug() << item << item->text() << item->data(DataRole_Pointer) << item->type()
            << reinterpret_cast<QObject *>(item->data(DataRole_Pointer).value<quintptr>());
    });

    auto toolbar = new QToolBar;
    auto actionReload = toolbar->addAction("reload");

    QObject::connect(actionReload, &QAction::triggered, treeView3, [&]
    {
        itemModel->setRootObject(config.get());
        treeView3->setRootIndex(itemModel->invisibleRootItem()->child(0)->index());
    });

    QWidget main;
    auto mainLayout = new QVBoxLayout(&main);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(treeView3);
    main.show();

#endif
    return app.exec();
}
