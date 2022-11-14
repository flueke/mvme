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
#ifndef __MVME_ANALYSIS_SERIALIZATION_H__
#define __MVME_ANALYSIS_SERIALIZATION_H__

#include "analysis_fwd.h"
#include "object_visitor.h"
#include "typedefs.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QVariantMap>

class VMEConfig;

namespace analysis
{

QJsonObject LIBMVME_EXPORT serialize(SourceInterface *source);
QJsonObject LIBMVME_EXPORT serialize(OperatorInterface *op);
QJsonObject LIBMVME_EXPORT serialize(SinkInterface *sink);
QJsonObject LIBMVME_EXPORT serialize(Directory *dir);

struct LIBMVME_EXPORT ObjectSerializerVisitor: public ObjectVisitor
{
    void visit(SourceInterface *source) override;
    void visit(OperatorInterface *op) override;
    void visit(SinkInterface *sink) override;
    void visit(ConditionInterface *cond) override;
    void visit(Directory *dir) override;
    void visit(PlotGridView *view) override;

    QJsonArray serializeConnections() const;
    QJsonObject serializeConditionLinks(const ConditionLinks &links) const;
    QJsonObject finalize(const Analysis *analysis) const;
    int objectCount() const { return visitedObjects.size(); }

    QJsonArray sourcesArray;
    QJsonArray operatorsArray;
    QJsonArray directoriesArray;
    QJsonArray genericObjectsArray;
    AnalysisObjectVector visitedObjects;
};

/* Connection from the output of a PipeSourceInterface object to the input of
 * an OperatorInterface slot. */
struct LIBMVME_EXPORT Connection
{
    PipeSourcePtr srcObject;    // The source object where the output pipe resides
    s32 srcIndex;               // The index of the output pipe in the source object

    OperatorPtr dstObject;      // Destination object containing the destination slot
    s32 dstIndex;               // Destination slot index
    s32 dstParamIndex;          // The parameter index used in the connection.

    bool operator==(const Connection &other) const
    {
        return srcObject == other.srcObject
            && srcIndex == other.srcIndex
            && dstObject == other.dstObject
            && dstIndex == other.dstIndex
            && dstParamIndex == other.dstParamIndex;
    }
};

uint LIBMVME_EXPORT qHash(const Connection &con, uint seed = 0);

QJsonObject LIBMVME_EXPORT serialze_connection(const Connection &con);

QSet<Connection> LIBMVME_EXPORT collect_internal_connections(const AnalysisObjectVector &objects);
QSet<Connection> LIBMVME_EXPORT collect_incoming_connections(const AnalysisObjectVector &objects);

QJsonArray LIBMVME_EXPORT serialize_internal_connections(const AnalysisObjectVector &objects);

struct LIBMVME_EXPORT AnalysisObjectStore
{
    SourceVector sources;
    OperatorVector operators;
    DirectoryVector directories;
    AnalysisObjectVector generics;
    QSet<Connection> connections;
    QHash<QUuid, AnalysisObjectPtr> objectsById;
    QHash<QUuid, QVariantMap> objectSettingsById;
    QVariantMap dynamicQObjectProperties;
    ConditionLinks conditionLinks;

    AnalysisObjectVector allObjects() const;
};

class ObjectFactory;

QJsonObject convert_to_current_version(QJsonObject json, const VMEConfig *vmeConfig);

AnalysisObjectStore LIBMVME_EXPORT deserialize_objects(
    QJsonObject data,
    const ObjectFactory &objectFactory);

void LIBMVME_EXPORT establish_connections(const QSet<Connection> &connections);
void LIBMVME_EXPORT establish_connections(const AnalysisObjectStore &objectStore);

} // end namespace analysis

#endif /* __MVME_ANALYSIS_SERIALIZATION_H__ */
