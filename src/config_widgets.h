#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

#include "util.h"
#include <QDialog>

class EventConfig;
class ModuleConfig;
class DataFilterConfig;
class MVMEContext;

namespace Ui
{
    class EventConfigDialog;
    class DataFilterDialog;
}

class EventConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        EventConfigDialog(MVMEContext *context, EventConfig *config, QWidget *parent = 0);
        ~EventConfigDialog();

        EventConfig *getConfig() const { return m_config; }

        virtual void accept() override;
        
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

        virtual void accept() override;

        QComboBox *typeCombo;
        QLineEdit *nameEdit;
        QLineEdit *addressEdit;

        MVMEContext *m_context;
        ModuleConfig *m_module;
};

class DataFilterDialog: public QDialog
{
    Q_OBJECT
    public:
        DataFilterDialog(DataFilterConfig *config, const QString &defaultFilter, QWidget *parent = 0);

        ~DataFilterDialog();

        DataFilterConfig *getConfig() const { return m_config; }

        virtual void accept() override;

    private:
        void loadFromConfig();
        void saveToConfig();
        void validate();

        Ui::DataFilterDialog *ui;
        DataFilterConfig *m_config;
};

#endif /* __CONFIG_WIDGETS_H__ */
