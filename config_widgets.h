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
    class ModuleConfigDialog;
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

        Ui::EventConfigDialog *ui;
        MVMEContext *m_context;
        EventConfig *m_config;
};

class ModuleConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        ModuleConfigDialog(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);
        ~ModuleConfigDialog();
        ModuleConfig *getConfig() const { return m_config; }

    protected:
        virtual void closeEvent(QCloseEvent *event);

    private slots:
        void on_buttonBox_clicked(QAbstractButton *button);

    private:
        void loadFromConfig();
        void saveToConfig();


        void handleListTypeIndexChanged(int);
        void onNameEditFinished();
        void onAddressEditFinished();

        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void execList();
        void initModule();

        Ui::ModuleConfigDialog *ui;
        QAction *actLoadTemplate, *actLoadFile;
        MVMEContext *m_context;
        ModuleConfig *m_config;
        int m_lastListTypeIndex = 0;
        QMap<int, QString> m_configStrings;
};

#endif /* __CONFIG_WIDGETS_H__ */
