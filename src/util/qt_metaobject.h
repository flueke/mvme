#ifndef __MVME_UTIL_QT_METAOBJECT_H__
#define __MVME_UTIL_QT_METAOBJECT_H__

#include <QMetaObject>
#include <QString>

template<typename T>
QString getClassName(T *obj)
{
    return obj->metaObject()->className();
}

#endif /* __MVME_UTIL_QT_METAOBJECT_H__ */
