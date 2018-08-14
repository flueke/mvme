#ifndef __MVME_ANALYIS_OBJECT_EDITOR_DIALOG_H__
#define __MVME_ANALYIS_OBJECT_EDITOR_DIALOG_H__

#include <QDialog>

namespace analysis
{

enum class ObjectEditorMode
{
    New,
    Edit
};

class ObjectEditorDialog: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        ObjectEditorDialog(QWidget *parent = nullptr);
        virtual ~ObjectEditorDialog();
};

} // end namespace analysis

#endif /* __MVME_ANALYIS_OBJECT_EDITOR_DIALOG_H__ */
