#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

#include <QWidget>

class EventConfig;
class ModuleConfig;
class QCloseEvent;
class QAction;
class MVMEContext;

namespace Ui
{
    class ModuleConfigWidget;
}

class EventConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        EventConfigWidget(EventConfig *config, QWidget *parent = 0);
        
    private:
        EventConfig *m_config;
};

class ModuleConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        ModuleConfigWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);
        ModuleConfig *getConfig() const { return m_config; }

    protected:
        virtual void closeEvent(QCloseEvent *event);

    private:
        void handleListTypeIndexChanged(int);
        void editorContentsChanged();
        void onNameEditFinished();
        void onAddressEditFinished();

        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void execList();

        Ui::ModuleConfigWidget *ui;
        QAction *actLoadTemplate, *actLoadFile;
        MVMEContext *m_context;
        ModuleConfig *m_config;
        int m_lastListTypeIndex = 0;
        bool m_ignoreEditorContentsChange = false;
};

#endif /* __CONFIG_WIDGETS_H__ */
