#ifndef __MVME_MULTI_CRATE_MAINWINDOW_H__
#define __MVME_MULTI_CRATE_MAINWINDOW_H__

#include <QMainWindow>
#include <memory>
#include "libmvme_export.h"
#include "vme_config.h"

class QTreeView;

namespace mesytec::mvme
{

class VmeConfigItemModel;
class VmeConfigItemController;

class LIBMVME_EXPORT MultiCrateMainWindow: public QMainWindow
{
    Q_OBJECT
    signals:
        void newVmeConfig();
        void openVmeConfig();
        void saveVmeConfig();
        void saveVmeConfigAs();
        void exploreWorkspace();

        void startDaq();
        void stopDaq();
        void pauseDaq();
        void resumeDaq();

        void editVmeScript(VMEScriptConfig *config);
        void editVmeScriptAsText(VMEScriptConfig *config);

        void vmeTreeContextMenuRequested(const QPoint &pos);

    public:
        MultiCrateMainWindow(QWidget *parent = nullptr);
        ~MultiCrateMainWindow();

        void closeEvent(QCloseEvent *event) override;
        void setConfig(const std::shared_ptr<ConfigObject> &config, const QString &filename);
        std::shared_ptr<ConfigObject> getConfig();
        void setConfigFilename(const QString &filename);
        QString getConfigFilename() const;

        // TODO: think about this API. How should this stuff be exposed?
        QTreeView *getVmeConfigTree();
        VmeConfigItemModel *getVmeConfigModel();
        VmeConfigItemController *getVmeConfigItemController();;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* _MVME__MULTI_CRATE_MAINWINDOW_H__ */
