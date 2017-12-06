#include "writer_process.h"
//#include "../vme_config.h"
//#include "../analysis/analysis.h"

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QTextStream>

using namespace mvme_root;

#define DEF_MESSAGE_HANDLER(name) u32 name(QDataStream &in, s32 msgType, QTextStream &logger)

typedef DEF_MESSAGE_HANDLER(MessageHandler);

DEF_MESSAGE_HANDLER(begin_run)
{
    qDebug() << __PRETTY_FUNCTION__;

    QString runId;
    in >> runId;

    logger << __PRETTY_FUNCTION__ << "runId = " << runId << endl;

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
    return 0u;
}

DEF_MESSAGE_HANDLER(end_event)
{
    qDebug() << __PRETTY_FUNCTION__;
    return 0u;
}

DEF_MESSAGE_HANDLER(module_data)
{
    qDebug() << __PRETTY_FUNCTION__;
    return 0u;
}

DEF_MESSAGE_HANDLER(timetick)
{
    qDebug() << __PRETTY_FUNCTION__;
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

    logger << "entering read loop" << endl;

    while (true)
    {
        s32 msgType;
        writerIn >> msgType;

        if (0 <= msgType && msgType < WriterMessageType::Count)
        {
            if (MessageHandlerTable[msgType](writerIn, msgType, logger) != 0)
                break;
        }
        else
        {
            ret = 1;
            break;
        }
    }

    logger << "left read loop, return code = " << ret << endl;

    return ret;
}
