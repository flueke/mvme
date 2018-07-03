#ifndef __MVME_ANALYSIS_SERIALIZATION_H__
#define __MVME_ANALYSIS_SERIALIZATION_H__

#include "analysis_fwd.h"
#include "object_visitor.h"
#include <QJsonArray>
#include <QJsonObject>

namespace analysis
{

QJsonObject serialize(SourceInterface *source);
QJsonObject serialize(OperatorInterface *op);
QJsonObject serialize(SinkInterface *sink);
QJsonObject serialize(Directory *dir);

QJsonArray serialize_internal_connections(const AnalysisObjectVector &objects);

struct ObjectSerializerVisitor: public ObjectVisitor
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

struct AddObjectsVisitor: public ObjectVisitor
{
    AddObjectsVisitor(Analysis *dest);

    virtual void visit(SourceInterface *source) override;
    virtual void visit(OperatorInterface *op) override;
    virtual void visit(SinkInterface *sink) override;
    virtual void visit(Directory *dir) override;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_SERIALIZATION_H__ */
