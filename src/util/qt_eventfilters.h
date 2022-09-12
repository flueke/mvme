#ifndef __MVME2_SRC_UTIL_QT_EVENTFILTERS_H_
#define __MVME2_SRC_UTIL_QT_EVENTFILTERS_H_

#include <QObject>

namespace mesytec::mvme::util
{

// Filters out all mouse wheel events sent to watched objects.
class WheelEventFilter: public QObject
{
    Q_OBJECT

    public:
       using QObject::QObject;

       bool eventFilter(QObject *watched, QEvent *event) override;
};

}

#endif // __MVME2_SRC_UTIL_QT_EVENTFILTERS_H_