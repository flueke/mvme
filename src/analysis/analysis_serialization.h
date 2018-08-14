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
    virtual void visit(SourceInterface *source) override;
    virtual void visit(OperatorInterface *op) override;
    virtual void visit(SinkInterface *sink) override;
    virtual void visit(Directory *dir) override;

    QJsonArray serializeConnections() const;
    QJsonObject finalize() const;
    int objectCount() const { return visitedObjects.size(); }

    QJsonArray sourcesArray;
    QJsonArray operatorsArray;
    QJsonArray directoriesArray;
    AnalysisObjectVector visitedObjects;
};

struct LIBMVME_EXPORT Connection
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

uint LIBMVME_EXPORT qHash(const Connection &con, uint seed = 0);

QJsonObject LIBMVME_EXPORT serialze_connection(const Connection &con);

QSet<Connection> LIBMVME_EXPORT collect_internal_collections(const AnalysisObjectVector &objects);

QJsonArray LIBMVME_EXPORT serialize_internal_connections(const AnalysisObjectVector &objects);

struct LIBMVME_EXPORT AnalysisObjectStore
{
    SourceVector sources;
    OperatorVector operators;
    DirectoryVector directories;
    QSet<Connection> connections;
    QHash<QUuid, AnalysisObjectPtr> objectsById;
    QHash<QUuid, QVariantMap> objectSettingsById;
    QVariantMap dynamicQObjectProperties;

    AnalysisObjectVector allObjects() const;
};

class ObjectFactory;

AnalysisObjectStore LIBMVME_EXPORT deserialize_objects(QJsonObject data,
                                                       VMEConfig *vmeConfig,
                                                       const ObjectFactory &objectFactory);

void LIBMVME_EXPORT establish_connections(const QSet<Connection> &connections);
void LIBMVME_EXPORT establish_connections(const AnalysisObjectStore &objectStore);

} // end namespace analysis

#endif /* __MVME_ANALYSIS_SERIALIZATION_H__ */
