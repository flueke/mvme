#ifndef HIST2DDIALOG_H
#define HIST2DDIALOG_H

#include "hist2d.h"
#include "mvme_context.h"
#include <QDialog>
#include <QValidator>

class MVMEContext;

namespace Ui {
class Hist2DDialog;
}

class Hist2DDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Hist2DDialog(MVMEContext *context, Hist2D *histo = 0, QWidget *parent = 0);
    ~Hist2DDialog();

    Hist2D *getHist2D();

private:
    void onEventXChanged(int index);
    void onModuleXChanged(int index);
    void onChannelXChanged(int index);

    void onEventYChanged(int index);
    void onModuleYChanged(int index);
    void onChannelYChanged(int index);

    Ui::Hist2DDialog *ui;
    MVMEContext *m_context;
    Hist2D *m_histo;
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

            auto hist2ds = m_context->get2DHistograms();
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
