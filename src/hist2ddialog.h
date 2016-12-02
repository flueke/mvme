#ifndef HIST2DDIALOG_H
#define HIST2DDIALOG_H

#include "hist2d.h"
#include "mvme_context.h"
#include <QDialog>
#include <QValidator>

class MVMEContext;
class QTreeWidget;
class QTreeWidgetItem;
class QDialogButtonBox;

namespace Ui {
class Hist2DDialog;
}

class Hist2DDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode
    {
        Create, // create a new histogram
        Edit,   // edit an existign histo
        Sub     // create a sub histogram
    };

    // to create a new histogram
    Hist2DDialog(MVMEContext *context, QWidget *parent = nullptr);

    // edit an existing histogram
    Hist2DDialog(MVMEContext *context, Hist2D *histo, QWidget *parent = nullptr);

    // create a sub histogram
    Hist2DDialog(MVMEContext *context, Hist2D *histo,
                 QwtInterval xBinRange,
                 QwtInterval yBinRange,
                 QWidget *parent = nullptr);

    ~Hist2DDialog();

    /* Get the resulting histo and config. In case of editing an existing
     * histogram this call will update both the histo and config with the new
     * values. */
    QPair<Hist2D *, Hist2DConfig *> getHistoAndConfig();

private slots:
    void on_pb_xSource_clicked();
    void on_pb_ySource_clicked();
    void on_pb_xClear_clicked();
    void on_pb_yClear_clicked();
    void updateAndValidate();

private:
    Hist2DDialog(Mode mode, MVMEContext *context, Hist2D *histo,
                 QwtInterval xBinRange, QwtInterval yBinRange,
                 QWidget *parent);


    Ui::Hist2DDialog *ui;
    Mode m_mode;
    MVMEContext *m_context;
    Hist2D *m_histo;
    Hist2DConfig *m_histoConfig;
    QPair<DataFilterConfig *, int> m_xSource;
    QPair<DataFilterConfig *, int> m_ySource;
    QwtInterval m_xBinRange;
    QwtInterval m_yBinRange;
    QPair<Hist2D *, Hist2DConfig *> m_result;
};

class SelectAxisSourceDialog: public QDialog
{
    Q_OBJECT
public:
        SelectAxisSourceDialog(MVMEContext *context, int selectedEventIndex = -1, QWidget *parent = 0);

        virtual void accept() override;
        QPair<DataFilterConfig *, int> getAxisSource() const;

private:
        void onTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);

        QTreeWidget *m_tree;
        QDialogButtonBox *m_buttonBox;
};

class NameValidator: public QValidator
{
    Q_OBJECT
    public:
        NameValidator(MVMEContext *context, Hist2D *histo = 0, QObject *parent = 0)
            : QValidator(parent)
            , m_context(context)
            , m_histo(histo)
        {}

        virtual State validate(QString &name, int &pos) const
        {
            if (name.isEmpty())
            {
                return QValidator::Intermediate;
            }

            auto hist2ds = m_context->getObjects<Hist2D *>();
            for (auto hist2d: hist2ds)
            {
                if (hist2d->objectName() == name && hist2d != m_histo)
                {
                    return QValidator::Intermediate;
                }
            }
            return QValidator::Acceptable;
        }

    private:
        MVMEContext *m_context;
        Hist2D *m_histo;
};

#endif // HIST2DDIALOG_H
