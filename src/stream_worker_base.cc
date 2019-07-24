#include "stream_worker_base.h"

#include "util/perf.h"

StreamWorkerBase::StreamWorkerBase(QObject *parent)
    : QObject(parent)
    , m_logThrottle(MaxLogMessagesPerSecond, std::chrono::seconds(1))
{}

StreamWorkerBase::~StreamWorkerBase()
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->objectName() << this->parent();
}

bool StreamWorkerBase::logMessage(const MessageSeverity &sev,
                                  const QString &msg,
                                  bool useThrottle)
{
    if (!useThrottle)
    {
        qDebug().noquote() << msg;
        emit sigLogMessage(msg);
        return true;
    }

    // have to store this before the call to eventOverflows()
    size_t suppressedMessages = m_logThrottle.overflow();

    if (!m_logThrottle.eventOverflows())
    {
        if (unlikely(suppressedMessages))
        {
            auto finalMsg(QString("%1 (suppressed %2 earlier messages)")
                          .arg(msg)
                          .arg(suppressedMessages)
                         );
            qDebug().noquote() << finalMsg;
            emit sigLogMessage(msg);
        }
        else
        {
            qDebug().noquote() << msg;
            emit sigLogMessage(msg);
        }
        return true;
    }

    return false;
}
