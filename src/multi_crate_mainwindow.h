#ifndef __MVME_MULTI_CRATE_MAINWINDOW_H__
#define __MVME_MULTI_CRATE_MAINWINDOW_H__

#include <QMainWindow>
#include <memory>
#include "libmvme_export.h"
#include "vme_config.h"

namespace mesytec::mvme
{

class LIBMVME_EXPORT MultiCrateMainWindow: public QMainWindow
{
    Q_OBJECT
    signals:
        void newVmeConfig();
        void openVmeConfig();
        void saveVmeConfig();
        void saveVmeConfigAs();

        void startDaq();
        void stopDaq();
        void pauseDaq();
        void resumeDaq();

        void editVmeScript(VMEScriptConfig *config);

    public:
        MultiCrateMainWindow(QWidget *parent = nullptr);
        ~MultiCrateMainWindow();

        void closeEvent(QCloseEvent *event) override;
        void setConfig(const std::shared_ptr<ConfigObject> &config, const QString &filename);
        std::shared_ptr<ConfigObject> getConfig();
        void setConfigFilename(const QString &filename);
        QString getConfigFilename() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* _MVME__MULTI_CRATE_MAINWINDOW_H__ */
