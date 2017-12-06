#include "mvme_root_writer_common.h"

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

#include "vme_config.h"
#include "analysis/analysis.h"

template<typename T>
std::unique_ptr<T> from_json(const QByteArray &data)
{
    QJsonDocument doc(QJsonDocument::fromJson(data));
    auto result = std::make_unique<T>();
    result->read(doc.object());
    return result;
}

using namespace mvme_root;

struct RootWriterContext
{
    QString runId;
    std::unique_ptr<VMEConfig> vmeConfig;
    std::unique_ptr<analysis::Analysis> analysis;
};

#define DEF_MESSAGE_HANDLER(name) u32 name(QDataStream &in,\
                                           s32 msgType,\
                                           QTextStream &logger,\
                                           RootWriterContext &context)

typedef DEF_MESSAGE_HANDLER(MessageHandler);

DEF_MESSAGE_HANDLER(begin_run)
{
    qDebug() << __PRETTY_FUNCTION__;

    QByteArray vmeData, analysisData;

    in >> context.runId >> vmeData >> analysisData;

    context.vmeConfig = from_json<VMEConfig>(vmeData);
    context.analysis  = from_json<analysis::Analysis>(analysisData);

    logger << __PRETTY_FUNCTION__
        << "runId = " << context.runId
        << ", vmeConfig = " << context.vmeConfig.get()
        << ", analysis = " << context.analysis.get()
        << endl;

    return 0u;
}

DEF_MESSAGE_HANDLER(end_run)
{
    qDebug() << __PRETTY_FUNCTION__;
    return 1u;
}

DEF_MESSAGE_HANDLER(begin_event)
{
    qDebug() << __PRETTY_FUNCTION__;

    s32 eventIndex;
    in >> eventIndex;

    logger << __PRETTY_FUNCTION__ << "eventIndex = " << eventIndex << endl;

    return 0u;
}

DEF_MESSAGE_HANDLER(end_event)
{
    qDebug() << __PRETTY_FUNCTION__;

    s32 eventIndex;
    in >> eventIndex;

    logger << __PRETTY_FUNCTION__ << "eventIndex = " << eventIndex << endl;
    return 0u;
}

DEF_MESSAGE_HANDLER(module_data)
{
    qDebug() << __PRETTY_FUNCTION__;

    s32 eventIndex, moduleIndex;
    char *rawData;
    uint dataSize;

    in >> eventIndex >> moduleIndex;
    in.readBytes(rawData, dataSize);

    logger << __PRETTY_FUNCTION__
        << "eventIndex = " << eventIndex
        << ", moduleIndex = " << moduleIndex
        << ", received " << dataSize << " bytes of module data"
        << endl;

    delete[] rawData;

    return 0u;
}

DEF_MESSAGE_HANDLER(timetick)
{
    qDebug() << __PRETTY_FUNCTION__;
    logger << __PRETTY_FUNCTION__ << endl;
    return 0u;
}

static const MessageHandler *
MessageHandlerTable[WriterMessageType::Count] =
{
    begin_run,
    end_run,
    begin_event,
    end_event,
    module_data,
    timetick,
};

int main(int argc, char *argv[])
{
    QFile qstdin;
    qstdin.open(stdin, QIODevice::ReadOnly);
    QDataStream writerIn(&qstdin);
    int ret = 0;

    QFile logFile("mvme-root-writer.log");
    logFile.open(QIODevice::WriteOnly);
    QTextStream logger(&logFile);

    RootWriterContext context;

    logger << "entering read loop" << endl;

    while (true)
    {
        s32 msgType;
        writerIn >> msgType;

        if (writerIn.status() != QDataStream::Ok
            || !(0 <= msgType && msgType < WriterMessageType::Count))
        {
            ret = 1;
            break;
        }

        if (MessageHandlerTable[msgType](writerIn, msgType, logger, context) != 0)
        {
            break;
        }
    }

    logger << "left read loop, return code = " << ret << endl;

    return ret;
}
