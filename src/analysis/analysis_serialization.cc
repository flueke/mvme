#include "analysis_serialization.h"
#include "analysis.h"
#include "object_factory.h"
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

uint qHash(const Connection &con, uint seed)
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

QSet<Connection> collect_internal_collections(const AnalysisObjectVector &objects)
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

    return connections;
}

QJsonArray serialize_internal_connections(const AnalysisObjectVector &objects)
{
    QSet<Connection> connections = collect_internal_collections(objects);

    QJsonArray result;

    for (const auto &con: connections)
    {
        result.append(serialze_connection(con));
    }

    return result;
}

void ObjectSerializerVisitor::visit(SourceInterface *source)
{
    sourcesArray.append(serialize(source));
    visitedObjects.append(source->shared_from_this());
}

void ObjectSerializerVisitor::visit(OperatorInterface *op)
{
    operatorsArray.append(serialize(op));
    visitedObjects.append(op->shared_from_this());
}

void ObjectSerializerVisitor::visit(SinkInterface *sink)
{
    operatorsArray.append(serialize(sink));
    visitedObjects.append(sink->shared_from_this());
}

void ObjectSerializerVisitor::visit(Directory *dir)
{
    directoriesArray.append(serialize(dir));
    visitedObjects.append(dir->shared_from_this());
}

QJsonArray ObjectSerializerVisitor::serializeConnections() const
{
    return serialize_internal_connections(visitedObjects);
}

QJsonObject ObjectSerializerVisitor::finalize() const
{
    QJsonObject json;
    json["MVMEAnalysisVersion"] = Analysis::getCurrentAnalysisVersion();
    json["sources"] = sourcesArray;
    json["operators"] = operatorsArray;
    json["directories"] = directoriesArray;
    json["connections"] = serializeConnections();
    return json;
}

namespace
{

/* This function converts from analysis config versions prior to V2, which
 * stored eventIndex and moduleIndex instead of eventId and moduleId.
 */
QJsonObject v1_to_v2(QJsonObject json, VMEConfig *vmeConfig)
{
    bool couldConvert = true;

    if (!vmeConfig)
    {
        // TODO: report error here
        return json;
    }

    // sources
    auto array = json["sources"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();
        int moduleIndex = objectJson["moduleIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);
        auto moduleConfig = vmeConfig->getModuleConfig(eventIndex, moduleIndex);

        if (eventConfig && moduleConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            objectJson["moduleId"] = moduleConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["sources"] = array;

    // operators
    array = json["operators"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);

        if (eventConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["operators"] = array;

    if (!couldConvert)
    {
        // TODO: report this
        qDebug() << "Error converting to v2!!!================================================";
    }

    return json;
}

QJsonObject noop_converter(QJsonObject json, VMEConfig *)
{
    return json;
}

using VersionConverter = std::function<QJsonObject (QJsonObject, VMEConfig *)>;

QVector<VersionConverter> get_version_converters()
{
    static QVector<VersionConverter> VersionConverters =
    {
        nullptr,
        v1_to_v2,
        noop_converter
    };

    return VersionConverters;
}

int get_version(const QJsonObject &json)
{
    return json[QSL("MVMEAnalysisVersion")].toInt(1);
};

QJsonObject convert_to_current_version(QJsonObject json, VMEConfig *vmeConfig)
{
    int version;

    while ((version = get_version(json)) < Analysis::getCurrentAnalysisVersion())
    {
        auto converter = get_version_converters().value(version);

        if (!converter)
            break;

        json = converter(json, vmeConfig);
        json[QSL("MVMEAnalysisVersion")] = version + 1;

        qDebug() << __PRETTY_FUNCTION__
            << "converted Analysis from version" << version
            << "to version" << version+1;
    }

    return json;
}

} // end anon namespace

AnalysisObjectVector AnalysisObjectStore::allObjects() const
{
    AnalysisObjectVector result;

    for (auto it = objectsById.begin();
         it != objectsById.end();
         it++)
    {
        result.push_back(*it);
    }

    return result;
}

AnalysisObjectStore deserialize_objects(QJsonObject data,
                                        VMEConfig *vmeConfig,
                                        const ObjectFactory &objectFactory)
{
    int version = get_version(data);

    if (version > Analysis::getCurrentAnalysisVersion())
    {
        throw std::runtime_error("The analysis data was generated by a newer version of mvme."
                                 " Please upgrade.");
    }

    data = convert_to_current_version(data, vmeConfig);

    AnalysisObjectStore result;

    // Sources
    {
        QJsonArray array = data["sources"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();
            auto className = objectJson["class"].toString();
            SourcePtr source(objectFactory.makeSource(className));

            if (source)
            {
                source->setId(QUuid(objectJson["id"].toString()));
                source->setObjectName(objectJson["name"].toString());
                source->read(objectJson["data"].toObject());

                auto eventId  = QUuid(objectJson["eventId"].toString());
                auto moduleId = QUuid(objectJson["moduleId"].toString());

                source->setEventId(eventId);
                source->setModuleId(moduleId);
                source->setObjectFlags(ObjectFlags::NeedsRebuild);

                result.sources.push_back(source);

                result.objectsById.insert(source->getId(), source);
            }
        }
    }

    // Operators
    {
        QJsonArray array = data["operators"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();
            auto className = objectJson["class"].toString();

            OperatorPtr op(objectFactory.makeOperator(className));

            // No operator with the given name exists, try a sink instead.
            if (!op)
            {
                op.reset(objectFactory.makeSink(className));
            }

            if (op)
            {
                op->setId(QUuid(objectJson["id"].toString()));
                op->setObjectName(objectJson["name"].toString());
                op->read(objectJson["data"].toObject());

                if (auto sink = qobject_cast<SinkInterface *>(op.get()))
                {
                    // FIXME: move into SinkInterface::read and the counterpart into
                    // SinkInterface::write
                    sink->setEnabled(objectJson["enabled"].toBool(true));
                }

                auto eventId = QUuid(objectJson["eventId"].toString());
                auto userLevel = objectJson["userLevel"].toInt();

                op->setEventId(eventId);
                op->setUserLevel(userLevel);
                op->setObjectFlags(ObjectFlags::NeedsRebuild);

                result.operators.push_back(op);
                result.objectsById.insert(op->getId(), op);

                assert(op->getUserLevel() >= 0);
            }
        }
    }

    // Directories
    {
        QJsonArray array = data["directories"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();
            auto dir = std::make_shared<Directory>();
            dir->setId(QUuid(objectJson["id"].toString()));
            dir->setObjectName(objectJson["name"].toString());
            dir->setEventId(QUuid(objectJson["eventId"].toString()));
            dir->setUserLevel(objectJson["userLevel"].toInt());
            dir->read(objectJson["data"].toObject());
            dir->setObjectFlags(ObjectFlags::NeedsRebuild);

            result.directories.push_back(dir);
            result.objectsById.insert(dir->getId(), dir);

            assert(dir->getUserLevel() >= 0);
        }
    }

    // Connections
    {
        QJsonArray array = data["connections"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();

            // PipeSourceInterface data
            QUuid srcId(objectJson["srcId"].toString());
            s32 srcIndex = objectJson["srcIndex"].toInt();

            // OperatorInterface data
            QUuid dstId(objectJson["dstId"].toString());
            s32 dstIndex = objectJson["dstIndex"].toInt();

            // Slot data
            s32 paramIndex = objectJson["dstParamIndex"].toInt();

            auto srcObject = std::dynamic_pointer_cast<PipeSourceInterface>(
                result.objectsById.value(srcId));

            auto dstObject = std::dynamic_pointer_cast<OperatorInterface>(
                result.objectsById.value(dstId));

            if (srcObject && dstObject)
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "src =" << srcObject.get() << ", dst =" << dstObject.get();

                Slot *dstSlot = dstObject->getSlot(dstIndex);

                assert(dstSlot);

                Connection con =
                {
                    srcObject,
                    srcIndex,
                    dstObject,
                    dstSlot->parentSlotIndex,
                    paramIndex
                };

                result.connections.insert(con);
            }
        }
    }

    // VME Object Settings
    {
        auto container = data["VMEObjectSettings"].toObject();

        for (auto it = container.begin(); it != container.end(); it++)
        {
            QUuid objectId(QUuid(it.key()));
            QVariantMap settings(it.value().toObject().toVariantMap());

            result.objectSettingsById.insert(objectId, settings);
        }
    }

    // Dynamic QObject properties
    result.dynamicQObjectProperties = data["properties"].toObject().toVariantMap();

    return result;
}

void establish_connections(const QSet<Connection> &connections)
{
    for (const auto &con: connections)
    {
        if (con.srcObject && con.dstObject)
        {
            if (Pipe *srcPipe = con.srcObject->getOutput(con.srcIndex))
            {
                con.dstObject->connectInputSlot(con.dstIndex, srcPipe, con.dstParamIndex);
            }
        }
    }
}

void establish_connections(const AnalysisObjectStore &objectStore)
{
    establish_connections(objectStore.connections);
}

AnalysisObjectSet to_set(const AnalysisObjectVector &objects)
{
    AnalysisObjectSet result;

    for (const auto &obj: objects)
    {
        result.insert(obj);
    }

    return result;
}

} // end namespace analysis
