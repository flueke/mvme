#include "widget_registry.h"

namespace mesytec::mvme
{

WidgetRegistry::WidgetRegistry(QObject *parent)
    : QObject(parent)
    , geoSaver_(new WidgetGeometrySaver(this))
{
}

void WidgetRegistry::addObjectWidget(QWidget *widget, QObject *object, const QString &geoSaverKey)
{
    connect(widget, &QObject::destroyed, this, [this, object, widget] (QObject *) {
        objectWidgets_[object].removeAll(widget);
    });

    objectWidgets_[object].push_back(widget);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    geoSaver_->addAndRestore(widget, QSL("WindowGeometries/") + geoSaverKey);
    add_widget_close_action(widget);
    widget->show();
}

bool WidgetRegistry::hasObjectWidget(QObject *object) const
{
    return !objectWidgets_[object].isEmpty();
}

QWidget *WidgetRegistry::getObjectWidget(QObject *object) const
{
    const auto &widgetList = objectWidgets_[object];

    return !widgetList.isEmpty() ? widgetList.last() : nullptr;
}

QList<QWidget *> WidgetRegistry::getObjectWidgets(QObject *object) const
{
    return objectWidgets_[object];
}

void WidgetRegistry::activateObjectWidget(QObject *object)
{
    if (auto widget = getObjectWidget(object))
        show_and_activate(widget);
}

QMultiMap<QObject *, QWidget *> WidgetRegistry::getAllObjectWidgets() const
{
    QMultiMap<QObject *, QWidget *> result;

    for (auto obj: objectWidgets_.keys())
    {
        for (auto widget: objectWidgets_.value(obj))
            result.insertMulti(obj, widget);
    }

    return result;
}

QList<QWidget *> WidgetRegistry::getAllWidgets() const
{
    QList<QWidget *> result;

    for (const auto &widgetList: objectWidgets_.values())
        for (const auto widget: widgetList)
            result.push_back(widget);

    return result;
}

void WidgetRegistry::addWidget(QWidget *widget, const QString &stateKey)
{
    connect(widget, &QObject::destroyed, this, [this, stateKey, widget] (QObject *) {
        if (nonObjectWidgets_.contains(stateKey))
            nonObjectWidgets_[stateKey].removeAll(widget);
    });

    nonObjectWidgets_[stateKey].push_back(widget);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    geoSaver_->addAndRestore(widget, QSL("WindowGeometries/") + stateKey);
    add_widget_close_action(widget);
    widget->show();
}

QWidget *WidgetRegistry::getWidget(const QString &stateKey) const
{
    auto theList = nonObjectWidgets_.value(stateKey);
    return theList.isEmpty() ? nullptr : theList.first();
}

QList<QWidget *> WidgetRegistry::getWidgets(const QString &stateKey) const
{
    return nonObjectWidgets_.value(stateKey);
}

}
