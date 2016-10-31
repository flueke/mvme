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
    explicit Hist2DDialog(MVMEContext *context, Hist2D *histo = 0, QWidget *parent = 0);
    ~Hist2DDialog();

    QPair<Hist2D *, Hist2DConfig *> getHistoAndConfig();

private slots:
    void on_pb_xSource_clicked();
    void on_pb_ySource_clicked();
    void on_pb_xClear_clicked();
    void on_pb_yClear_clicked();
    void updateAndValidate();

private:
    Ui::Hist2DDialog *ui;
    MVMEContext *m_context;
    Hist2D *m_histo;
    QPair<DataFilterConfig *, int> m_xSource;
    QPair<DataFilterConfig *, int> m_ySource;
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
