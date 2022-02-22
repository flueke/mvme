#ifndef __MVME_VME_CONFIG_TREE_P_H__
#define __MVME_VME_CONFIG_TREE_P_H__

#include <QTreeWidget>

#include "libmvme_export.h"

class VMEConfigTreeWidget;

class LIBMVME_EXPORT VMEConfigTree: public QTreeWidget
{
    Q_OBJECT
    public:
        explicit VMEConfigTree(VMEConfigTreeWidget *parent = nullptr);

    protected:
        virtual QStringList mimeTypes() const override;

        virtual QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;

        virtual bool dropMimeData(QTreeWidgetItem *parent, int index,
                                  const QMimeData *data, Qt::DropAction action) override;

        bool dropMimeDataOnModulesInit(QTreeWidgetItem *parent, int index,
                                       const QMimeData *data, Qt::DropAction action);

        virtual Qt::DropActions supportedDropActions() const override
        {
            return /*Qt::CopyAction |*/ Qt::MoveAction;
        }

        virtual void dropEvent(QDropEvent *event) override
        {
            /* Avoid calling the QTreeWidget reimplementation which handles
             * internal moves specially. Instead pass through to the
             * QAbstractItemView base. */
            QAbstractItemView::dropEvent(event);
        }

    private:
        VMEConfigTreeWidget *m_configWidget;
};

#endif /* __MVME_VME_CONFIG_TREE_P_H__ */
