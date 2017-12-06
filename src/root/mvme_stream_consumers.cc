#include "mvme_stream_consumers.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QJsonDocument>
#include <QThread>

#include "../analysis/analysis.h"
#include "../vme_config.h"
#include "writer_process.h"

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

AnalysisDataWriter::AnalysisDataWriter(QObject *parent)
    : QObject(parent)
{}

AnalysisDataWriter::~AnalysisDataWriter()
{}

void AnalysisDataWriter::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis, Logger logger)
{
    qDebug() << __PRETTY_FUNCTION__ << QThread::currentThread();

    m_logger = logger;

    m_writerProcess = new QProcess(this);
    m_writerProcess->start(QCoreApplication::applicationDirPath() + "/mvme-root-writer");

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

void AnalysisDataWriter::endRun(const std::exception *e)
{
    qDebug() << __PRETTY_FUNCTION__ << QThread::currentThread();

    m_writerOut << WriterMessageType::EndRun;

    qDebug() << __PRETTY_FUNCTION__ << "waitForFinished";

    if (!m_writerProcess->waitForFinished())
    {
        m_logger(QString("ROOT Writer: writer process did not exit cleanly: %1")
               .arg(m_writerProcess->error()));
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "after waitForFinished";

    m_writerOut.setDevice(nullptr);
    m_writerProcess->kill();
    delete m_writerProcess;
    m_writerProcess = nullptr;
}

void AnalysisDataWriter::beginEvent(s32 eventIndex)
{
    m_writerOut << WriterMessageType::BeginEvent
        << eventIndex;
}

void AnalysisDataWriter::endEvent(s32 eventIndex)
{
    m_writerOut << WriterMessageType::EndEvent
        << eventIndex;
}

void AnalysisDataWriter::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    m_writerOut << WriterMessageType::ModuleData
        << eventIndex
        << moduleIndex;

    m_writerOut.writeBytes(reinterpret_cast<const char *>(data), size * sizeof(u32));
}

void AnalysisDataWriter::processTimetick()
{
    m_writerOut << WriterMessageType::Timetick;
}

} // end namespace mvme_root
