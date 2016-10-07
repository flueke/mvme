#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

#include "util.h"
#include <QDialog>
#include <QMap>

class EventConfig;
class ModuleConfig;
class QCloseEvent;
class QAction;
class MVMEContext;
class QAbstractButton;

namespace Ui
{
    class EventConfigDialog;
    class ModuleConfigWidget;
    class VHS4030pWidget;
}

class EventConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        EventConfigDialog(MVMEContext *context, EventConfig *config, QWidget *parent = 0);

        EventConfig *getConfig() const { return m_config; }

        virtual void accept();
        
    private:
        void loadFromConfig();
        void saveToConfig();
        void setReadOnly(bool readOnly);

        Ui::EventConfigDialog *ui;
        MVMEContext *m_context;
        EventConfig *m_config;
};

class QComboBox;
class QLineEdit;

class ModuleConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        ModuleConfigDialog(MVMEContext *context, ModuleConfig *module,
                           bool isNewModule = false, QWidget *parent = 0);

        ModuleConfig *getModule() const { return m_module; }

        virtual void accept();

        QComboBox *typeCombo;
        QLineEdit *nameEdit;
        QLineEdit *addressEdit;

        MVMEContext *m_context;
        ModuleConfig *m_module;
};

#endif /* __CONFIG_WIDGETS_H__ */
