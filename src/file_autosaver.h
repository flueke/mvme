#ifndef __MVME_FILE_AUTOSAVER_H__
#define __MVME_FILE_AUTOSAVER_H__

#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include "typedefs.h"

/* File auto save feature in mvme:
 *
 * Periodically save the vme and analysis configs to disk to avoid losing all
 * changes in case the program should crash.
 *
 * On opening a workspace check to see if an auto save exists and ask the user
 * if it should be loaded or deleted.
 *
 * On closing a workspace the last thing that should happen is removal of
 * existing autosaves. This has to happen after all config files have been
 * saved or the changes have been explicitly discarded by the user.
 *
 * If the user decides to load an autosave clear the filename so that it is
 * treated like a new file and a new name has to be picked
 *
 * or
 *
 * use the autosave name. !!! But this would conflict with the autosave feature
 * itself!
 *
 * Also directly opening an autosave filename could be treated like the
 * autoload (meaning clear the filename) on opening a workspace or it will
 * yield to the open autosave overwriting itself :)
 */

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
        bool isActive() { return m_timer->isActive(); }
        bool removeOutputFile();

    public slots:
        void setInterval(s32 interval_ms) { m_timer->setInterval(interval_ms); }
        void start();
        void stop();
        void saveNow();

    private:
        Serializer m_serializer;
        const QString m_outputFilename;
        QTimer *m_timer;
};

#endif /* __MVME_FILE_AUTOSAVER_H__ */
