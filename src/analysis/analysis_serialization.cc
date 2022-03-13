/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "analysis_serialization.h"
#include <QHash>

#include "analysis.h"
#include "object_factory.h"
#include "util/qt_metaobject.h"

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

QJsonObject serialize_conditionlink(const OperatorPtr &op, const ConditionLink &cl)
{
    QJsonObject result;
    result["operatorId"] = op->getId().toString();
    result["conditionId"] = cl.condition->getId().toString();
    result["subIndex"] = cl.subIndex;
    return result;
}

QSet<Connection> collect_internal_connections(const AnalysisObjectVector &objects)
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
                    OperatorPtr dstOp;

                    assert(dstSlot->parentOperator);

                    if (dstSlot->parentOperator)
                    {
                        dstOp = std::dynamic_pointer_cast<OperatorInterface>(
                            dstSlot->parentOperator->shared_from_this());
                    }

                    if (dstOp)
                    {
                        // slow. let the caller pass an object set instead?
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

QSet<Connection> collect_incoming_connections(const AnalysisObjectVector &objects)
{
    QSet<Connection> connections;

    for (const auto &obj: objects)
    {
        auto op = std::dynamic_pointer_cast<OperatorInterface>(obj);

        // Only OperatorInterface subclasses have input slots
        if (!op) continue;

        auto inputSlots = op->getSlots();

        for (Slot *slot: inputSlots)
        {
            if (!slot->isConnected()) continue;

            Connection con;

            con.srcObject = std::dynamic_pointer_cast<PipeSourceInterface>(
                slot->inputPipe->getSource()->shared_from_this());

            // Skip internal connections
            if (!objects.contains(con.srcObject))
            {
                con.srcIndex = slot->inputPipe->sourceOutputIndex;

                con.dstObject = op;
                con.dstIndex = slot->parentSlotIndex;
                con.dstParamIndex = slot->paramIndex;

                connections.insert(con);
            }
        }
    }

    return connections;
}

QJsonArray serialize_internal_connections(const AnalysisObjectVector &objects)
{
    QSet<Connection> connections = collect_internal_connections(objects);

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

QJsonArray ObjectSerializerVisitor::serializeConditionLinks(const ConditionLinks &links) const
{
    QJsonArray result;

    for (auto it = links.begin(); it != links.end(); it++)
    {
        const auto &op(it.key());
        const auto &link(it.value());

        if (op && link)
        {
            result.append(serialize_conditionlink(op, link));
        }
    }

    return result;
}

QJsonObject ObjectSerializerVisitor::finalize(const Analysis *analysis) const
{
    QJsonObject json;
    json["MVMEAnalysisVersion"] = Analysis::getCurrentAnalysisVersion();
    json["sources"] = sourcesArray;
    json["operators"] = operatorsArray;
    json["directories"] = directoriesArray;
    json["connections"] = serializeConnections();
    json["conditionLinks"] = serializeConditionLinks(analysis->getConditionLinks());
    return json;
}

namespace
{

int get_version(const QJsonObject &json)
{
    return json[QSL("MVMEAnalysisVersion")].toInt(1);
};

AnalysisObjectStore deserialize_objects(
    QJsonObject data,
    const ObjectFactory &objectFactory,
    bool ignoreVersionTooOld)
{
    int version = get_version(data);

    if (!ignoreVersionTooOld && version < Analysis::getCurrentAnalysisVersion())
    {
        throw std::system_error(make_error_code(AnalysisReadResult::VersionTooOld));
    }

    if (version > Analysis::getCurrentAnalysisVersion())
        throw std::system_error(make_error_code(AnalysisReadResult::VersionTooNew));

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
                //qDebug() << __PRETTY_FUNCTION__
                //    << "src =" << srcObject.get() << ", dst =" << dstObject.get();

                Slot *dstSlot = dstObject->getSlot(dstIndex);

                if (!dstSlot)
                {
                    qDebug() << __PRETTY_FUNCTION__
                        << "null dstSlot encountered!, dstObject =" << dstObject.get()
                        << ", dstSlotIndex =" << dstIndex;
                }

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

    // Condition Links
    {
        QJsonArray array = data["conditionLinks"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();

            QUuid opId(objectJson["operatorId"].toString());
            QUuid condId(objectJson["conditionId"].toString());
            s32 subIndex = objectJson["subIndex"].toInt();

            auto op = std::dynamic_pointer_cast<OperatorInterface>(
                result.objectsById.value(opId));

            auto cond = std::dynamic_pointer_cast<ConditionInterface>(
                result.objectsById.value(condId));

            if (op && cond)
            {
                result.conditionLinks.insert(op, { cond, subIndex });
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


QJsonObject noop_converter(QJsonObject json, const VMEConfig *)
{
    return json;
}

/* This function converts from analysis config versions prior to V2, which
 * stored eventIndex and moduleIndex instead of eventId and moduleId.
 */
QJsonObject v1_to_v2(QJsonObject json, const VMEConfig *vmeConfig)
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

// Special casing in the UI for raw sinks (bottom-left tree) was removed.
// Previously raw histograms for module extraction filters where anchored below
// special module nodes like data extractors. Newer analysis versions instead
// create a directory for each module to hold the histograms.
//
// The converter collects sinks (histograms, rate monitors) from the first
// level, groups them by event and module, creates a directory with the name of
// the module and moves the histograms into the directory.
QJsonObject v3_to_v4(QJsonObject json, const VMEConfig *)
{
    auto objectStore = deserialize_objects(json, Analysis().getObjectFactory(), true);
    establish_connections(objectStore);

    // EventId -> ModuleId -> Sinks
    using GroupedSinks = std::map<QUuid, std::map<QUuid, std::vector<AnalysisObjectPtr>>>;

    auto operators = objectStore.operators;

    // Keep only sinks on level 0.
    operators.erase(std::remove_if(std::begin(operators), std::end(operators),
                                   [] (const auto &op) {
                                       return !(op->getUserLevel() == 0
                                                && qobject_cast<SinkInterface *>(op.get()));
                                   }),
                    operators.end());

    // Group the sinks by event and module ids.
    GroupedSinks gs;

    for (const auto &sink: operators)
    {
        // It's enough to check the first slot. Histos and Rate Monitors only
        // have one slot.
        auto firstSlot = sink->getSlot(0);

        if (firstSlot && firstSlot->isConnected())
        {
            if (auto sourceObject = qobject_cast<SourceInterface *>(firstSlot->inputPipe->getSource()))
            {
                auto eventId = sourceObject->getEventId();
                auto moduleId = sourceObject->getModuleId();
                assert(eventId == sink->getEventId());

                gs[eventId][moduleId].emplace_back(sink);
            }
        }
    }

    // Get module names directly from the json data. Note that the VMEConfig
    // cannot be used to get this information because module id auto assignment
    // hasn't been done yet which means the ids of the vmeconfig and the
    // analysis can differ.
    QMap<QUuid, QString> moduleNames;

    for (const auto &modProps: json["properties"].toObject()["ModuleProperties"].toArray())
    {
        auto moduleId = QUuid::fromString(modProps.toObject()["moduleId"].toString());
        auto moduleName = modProps.toObject()["moduleName"].toString();
        moduleNames[moduleId] = moduleName;
    }

    for (const auto &ev: gs)
    {
        const auto &eventId = ev.first;

        for (const auto &mv: ev.second)
        {
            const auto &moduleId = mv.first;

            if (moduleNames.contains(moduleId))
            {
                auto dir = std::make_shared<Directory>();
                dir->setEventId(eventId);
                dir->setDisplayLocation(DisplayLocation::Sink);
                dir->setObjectName("Raw Histos " + moduleNames[moduleId]);

                for (const auto &sink: mv.second)
                    dir->push_back(sink);

                objectStore.directories.push_back(dir);
                objectStore.objectsById[dir->getId()] = dir;
            }
        }
    }

    auto allObjects = objectStore.allObjects();
    ObjectSerializerVisitor sv;
    visit_objects(allObjects.begin(), allObjects.end(), sv);

    json["directories"] = sv.directoriesArray;

    return json;
}

using VersionConverter = std::function<QJsonObject (QJsonObject, const VMEConfig *)>;

QVector<VersionConverter> get_version_converters()
{
    static QVector<VersionConverter> VersionConverters =
    {
        nullptr,        // 0 -> 1
        v1_to_v2,       // 1 -> 2
        noop_converter, // 2 -> 3 (addition of Directory objects)
        v3_to_v4,       // 3 -> 4 (dirs for raw histograms, removed raw sink special casing from the UI)
    };

    return VersionConverters;
}

} // end anon namespace

QJsonObject convert_to_current_version(QJsonObject json, const VMEConfig *vmeConfig)
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

    assert(json[QSL("MVMEAnalysisVersion")].toInt() == Analysis::getCurrentAnalysisVersion());

    return json;
}

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

AnalysisObjectStore deserialize_objects(
    QJsonObject data,
    const ObjectFactory &objectFactory)
{
    return deserialize_objects(data, objectFactory, false);
}

void establish_connections(const QSet<Connection> &connections)
{
    qDebug() << __PRETTY_FUNCTION__ << "#connetions=" << connections.size();

    for (const auto &con: connections)
    {
        if (con.srcObject && con.dstObject)
        {
            if (Pipe *srcPipe = con.srcObject->getOutput(con.srcIndex))
            {
                con.dstObject->connectInputSlot(con.dstIndex, srcPipe, con.dstParamIndex);
            }
            else
            {
                qDebug() << "establish_connections: no srcPipe for" << con.srcObject->objectName();
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
