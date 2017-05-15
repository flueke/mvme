#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

#include "util.h"
#include <QDialog>

class EventConfig;
class ModuleConfig;
class DataFilterConfig;
class DualWordDataFilterConfig;
class MVMEContext;
class AnalysisConfig;

namespace analysis
{
    class Analysis;
}

namespace Ui
{
    class EventConfigDialog;
    class DataFilterDialog;
    class DualWordDataFilterDialog;
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
        ModuleConfigDialog(MVMEContext *context, ModuleConfig *module, QWidget *parent = 0);

        ModuleConfig *getModule() const { return m_module; }

        virtual void accept() override;

        QComboBox *typeCombo;
        QLineEdit *nameEdit;
        QLineEdit *addressEdit;

        MVMEContext *m_context;
        ModuleConfig *m_module;
};

QPair<bool, QString> saveAnalysisConfig(AnalysisConfig *config,
                                        analysis::Analysis *analysis_ng,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter);

QPair<bool, QString> saveAnalysisConfigAs(AnalysisConfig *config,
                                          analysis::Analysis *analysis_ng,
                                          QString startPath,
                                          QString fileFilter);

#endif /* __CONFIG_WIDGETS_H__ */
