#include "qt_eventfilters.h"

#include <QEvent>

namespace mesytec::mvme::util
{

bool WheelEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    bool result = event->type() == QEvent::Wheel;
    return result;
}

}