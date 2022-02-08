#ifndef __MVME_OBJECT_WIDGET_REGISTRY_H__
#define __MVME_OBJECT_WIDGET_REGISTRY_H__

#include <QObject>
#include <QWidget>
#include <QString>
#include "qt_util.h"

namespace mesytec
{
namespace mvme
{

class WidgetRegistry: public QObject
{
    Q_OBJECT
    signals:

    public:
        WidgetRegistry(QObject *parent = nullptr);

        // Adds a widget for the given object to the registry. Multiple widgets can be added for
        // the same object.
        void addObjectWidget(QWidget *widget, QObject *object, const QString &geoSaverKey);

        // Returns true if there is at least one widget registered for the given object.
        bool hasObjectWidget(QObject *object) const;

        // Returns the last widget registered for the given object or nullptr if no widget was
        // registered.
        QWidget *getObjectWidget(QObject *object) const;

        // Returns the full list of widgets registered for the specified object.
        QList<QWidget *> getObjectWidgets(QObject *object) const;

        // Activates, shows and raises the widget for the given object.
        void activateObjectWidget(QObject *object);

        QMultiMap<QObject *, QWidget *> getAllObjectWidgets() const;
        QList<QWidget *> getAllWidgets() const;

    private:
        WidgetGeometrySaver *geoSaver_;
        QMap<QObject *, QList<QWidget *>> objectWidgets_;
};

}
}

#endif /* __MVME_OBJECT_WIDGET_REGISTRY_H__ */
