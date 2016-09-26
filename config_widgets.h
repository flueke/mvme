#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

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

class ModuleConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        ModuleConfigWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);
        ~ModuleConfigWidget();
        ModuleConfig *getConfig() const { return m_config; }

        // close the widget discarding any changes
        void forceClose();

    protected:
        virtual void closeEvent(QCloseEvent *event);

    private slots:
        void on_buttonBox_clicked(QAbstractButton *button);

    private:
        void loadFromConfig();
        void saveToConfig();
        void setReadOnly(bool readOnly);

        void handleListTypeIndexChanged(int);
        void onNameEditFinished();
        void onAddressEditFinished();

        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void execList();
        void initModule();
        void setModified(bool modified);

        Ui::ModuleConfigWidget *ui;
        QAction *actLoadTemplate, *actLoadFile;
        MVMEContext *m_context;
        ModuleConfig *m_config;
        int m_lastListTypeIndex = 0;
        QMap<int, QString> m_configStrings;
        bool m_hasModifications = false;
        bool m_readOnly = false;
        bool m_forceClose = false;
};

QWidget *makeModuleConfigWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);

class VHS4030pWidget: public QDialog
{
    Q_OBJECT
    public:
        VHS4030pWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);

    private:
        Ui::VHS4030pWidget *ui;
        MVMEContext *m_context;
        ModuleConfig *m_config;
};

#endif /* __CONFIG_WIDGETS_H__ */
