#include "file_autosaver.h"

#include <cassert>
#include <QDebug>
#include <QTemporaryFile>

#include "qt_util.h"

FileAutoSaver::FileAutoSaver(Serializer serializer, const QString &outputFilename, s32 interval_ms,
                             QObject *parent)
    : QObject(parent)
    , m_serializer(serializer)
    , m_outputFilename(outputFilename)
    , m_timer(new QTimer(this))
{
    assert(m_serializer);

    m_timer->setInterval(interval_ms);

    connect(m_timer, &QTimer::timeout, this, &FileAutoSaver::saveNow);
}

void FileAutoSaver::start()
{
    if (isActive())
    {
        qDebug() << "restarting" << objectName();
    }
    else
    {
        qDebug() << "starting" << objectName();
    }

    m_timer->start();
}

void FileAutoSaver::stop()
{
    qDebug() << "stopping" << objectName();

    m_timer->stop();
}

/* Note: nothing is race free here.
 * Also see
 * https://stackoverflow.com/questions/14935919/copy-file-even-when-destination-exists-in-qt
 * for a discussion about QFile and copying.
 */
void FileAutoSaver::saveNow()
{
    QTemporaryFile tempFile(QSL("mvme_autosave"));

    if (!tempFile.open())
    {
        emit writeError(m_outputFilename, QSL("Could not create temporary file"));
        return;
    }

    if (tempFile.write(m_serializer()) == -1)
    {
        emit writeError(m_outputFilename, QSL("Could not write to temporary file"));
        return;
    }

    if (QFile::exists(m_outputFilename))
    {
        if (!QFile::remove(m_outputFilename))
        {
            emit writeError(m_outputFilename, QSL("Could not remove existing output file"));
            return;
        }
    }

    if (!QFile::copy(tempFile.fileName(), m_outputFilename))
    {
        emit writeError(m_outputFilename, QSL("Could not copy temporary file to output file"));
        return;
    }

    qDebug() << objectName() << "wrote output file " << m_outputFilename;

    emit saved(m_outputFilename);
}
