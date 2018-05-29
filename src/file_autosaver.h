#ifndef __MVME_FILE_AUTOSAVER_H__
#define __MVME_FILE_AUTOSAVER_H__

#include <QObject>
#include <QTimer>
#include <memory>
#include "typedefs.h"

class FileAutoSaver: public QObject
{
    Q_OBJECT

    using Serializer = std::function<QByteArray ()>;

    signals:
        void saved(const QString &filename);
        void writeError(const QString &filename, const QString &errorMessage);

    public:
        FileAutoSaver(Serializer serializer, const QString &outputFilename, s32 interval_ms,
                      QObject *parent = nullptr);

        s32 getInterval() const { return m_timer->interval(); }
        QString getOutputFilename() const { return m_outputFilename; }

    public slots:
        void setInterval(s32 interval_ms) { m_timer->setInterval(interval_ms); }
        void start() { m_timer->start(); }
        void stop() { m_timer->stop(); }
        bool isActive() { return m_timer->isActive(); }
        void saveNow();

    private:
        Serializer m_serializer;
        const QString m_outputFilename;
        QTimer *m_timer;
};

#endif /* __MVME_FILE_AUTOSAVER_H__ */
