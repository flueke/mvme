#include "mvme_root_data_writer.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QJsonDocument>
#include <QThread>

#include "analysis/analysis.h"
#include "vme_config.h"
#include "mvme_root_writer_common.h"

namespace
{

template<typename T>
QByteArray to_json(T *obj)
{
    QJsonObject json;
    obj->write(json);
    return QJsonDocument(json).toBinaryData();
}

} // end anon namespace

namespace mvme_root
{

RootDataWriter::RootDataWriter(QObject *parent)
    : QObject(parent)
{}

RootDataWriter::~RootDataWriter()
{}

void RootDataWriter::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis, Logger logger)
{
    qDebug() << __PRETTY_FUNCTION__ << QThread::currentThread();

    m_logger = logger;

    m_writerProcess = new QProcess(this);
    m_writerProcess->start(QCoreApplication::applicationDirPath() + "/mvme-root-writer");
                           //QIODevice::ReadWrite | QIODevice::Unbuffered);

    logger("ROOT Writer: starting writer process");

    if (!m_writerProcess->waitForStarted())
    {
        logger(QString("ROOT Writer: error starting writer process: %1")
               .arg(m_writerProcess->error()));
        return;
    }

    m_writerOut.setDevice(m_writerProcess);

    m_writerOut << WriterMessageType::BeginRun
        << runInfo.runId
        << to_json(vmeConfig)
        << to_json(analysis);
}

void RootDataWriter::endRun(const std::exception *e)
{
    qDebug() << __PRETTY_FUNCTION__ << QThread::currentThread();

    m_writerOut << WriterMessageType::EndRun;
    m_writerOut.setDevice(nullptr);

    qDebug() << __PRETTY_FUNCTION__ << "closeWriteChannel()";
    m_writerProcess->closeWriteChannel();

    qDebug() << __PRETTY_FUNCTION__ << "waitForFinished()";

    if (!m_writerProcess->waitForFinished(-1))
    {
        m_logger(QString("ROOT Writer: writer process did not exit cleanly: %1")
               .arg(m_writerProcess->error()));
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "after waitForFinished()";

    m_writerProcess->kill();
    delete m_writerProcess;
    m_writerProcess = nullptr;
}

void RootDataWriter::beginEvent(s32 eventIndex)
{
    m_writerOut << WriterMessageType::BeginEvent
        << eventIndex;
}

void RootDataWriter::endEvent(s32 eventIndex)
{
    m_writerOut << WriterMessageType::EndEvent
        << eventIndex;
}

void RootDataWriter::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    m_writerOut << WriterMessageType::ModuleData
        << eventIndex
        << moduleIndex;

    m_writerOut.writeBytes(reinterpret_cast<const char *>(data), size * sizeof(u32));
}

void RootDataWriter::processTimetick()
{
    m_writerOut << WriterMessageType::Timetick;
}

} // end namespace mvme_root
