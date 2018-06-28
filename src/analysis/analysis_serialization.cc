#include "analysis_serialization.h"
#include <QHash>

namespace analysis
{

QJsonObject serialize(SourceInterface *source)
{
    QJsonObject dest;
    dest["id"] = source->getId().toString();
    dest["name"] = source->objectName();
    dest["eventId"]  = source->getEventId().toString();
    dest["moduleId"] = source->getModuleId().toString();
    dest["class"] = getClassName(source);
    QJsonObject dataJson;
    source->write(dataJson);
    dest["data"] = dataJson;
    return dest;
}

QJsonObject serialize(OperatorInterface *op)
{
    QJsonObject dest;
    dest["id"]        = op->getId().toString();
    dest["name"]      = op->objectName();
    dest["eventId"]   = op->getEventId().toString();
    dest["class"]     = getClassName(op);
    dest["userLevel"] = op->getUserLevel();
    QJsonObject dataJson;
    op->write(dataJson);
    dest["data"] = dataJson;
    return dest;
}

QJsonObject serialize(SinkInterface *sink)
{
    QJsonObject dest = serialize(qobject_cast<OperatorInterface *>(sink));
    dest["enabled"] = sink->isEnabled();
    return dest;
}

QJsonObject serialize(Directory *dir)
{
    QJsonObject dest;
    dest["id"]        = dir->getId().toString();
    dest["name"]      = dir->objectName();
    dest["eventId"]   = dir->getEventId().toString();
    dest["userLevel"] = dir->getUserLevel();
    QJsonObject dataJson;
    dir->write(dataJson);
    dest["data"] = dataJson;
    return dest;
}

namespace
{

struct Connection
{
    PipeSourcePtr srcObject;
    s32 srcIndex;

    OperatorPtr dstObject;
    s32 dstIndex;
    s32 dstParamIndex;

    bool operator==(const Connection &other) const
    {
        return srcObject == other.srcObject
            && srcIndex == other.srcIndex
            && dstObject == other.dstObject
            && dstIndex == other.dstIndex
            && dstParamIndex == other.dstParamIndex;
    }
};

uint qHash(const Connection &con, uint seed = 0)
{
    return ::qHash(con.srcObject.get(), seed)
        ^ ::qHash(con.srcIndex, seed)
        ^ ::qHash(con.dstObject.get(), seed)
        ^ ::qHash(con.dstIndex, seed)
        ^ ::qHash(con.dstParamIndex, seed);
}

QJsonObject serialze_connection(const Connection &con)
{
    QJsonObject result;
    result["srcId"] = con.srcObject->getId().toString();
    result["srcIndex"] = con.srcIndex;
    result["dstId"] = con.dstObject->getId().toString();
    result["dstIndex"] = con.dstIndex;
    result["dstParamIndex"] = con.dstParamIndex;
    return result;
}

} // end anon namespace

QJsonArray serialize_internal_connections(const AnalysisObjectVector &objects)
{
    QSet<Connection> connections;

    for (const auto &obj: objects)
    {
        if (auto srcObject = std::dynamic_pointer_cast<PipeSourceInterface>(obj))
        {
            for (s32 outputIndex = 0;
                 outputIndex < srcObject->getNumberOfOutputs();
                 ++outputIndex)
            {
                Pipe *pipe = srcObject->getOutput(outputIndex);

                for (Slot *dstSlot: pipe->getDestinations())
                {
                    if (auto dstOp = std::dynamic_pointer_cast<OperatorInterface>(
                            dstSlot->parentOperator->shared_from_this()))
                    {
                        if (objects.contains(dstOp))
                        {
                            Connection con =
                            {
                                srcObject,
                                outputIndex,
                                dstOp,
                                dstSlot->parentSlotIndex,
                                dstSlot->paramIndex
                            };

                            connections.insert(con);
                        }
                    }
                }
            }
        }
    }

    QJsonArray result;

    for (const auto &con: connections)
    {
        result.append(serialze_connection(con));
    }

    return result;
}

} // end namespace analysis
