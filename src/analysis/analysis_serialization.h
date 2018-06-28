#ifndef __MVME_ANALYSIS_SERIALIZATION_H__
#define __MVME_ANALYSIS_SERIALIZATION_H__

#include "analysis.h"
#include <QJsonArray>

namespace analysis
{

QJsonObject serialize(SourceInterface *source);
QJsonObject serialize(OperatorInterface *op);
QJsonObject serialize(SinkInterface *sink);
QJsonObject serialize(Directory *dir);

struct ObjectSerializerVisitor: public ObjectVisitor
{
    virtual void visit(SourceInterface *source) override
    {
        sourcesArray.append(serialize(source));
    }

    virtual void visit(OperatorInterface *op) override
    {
        operatorsArray.append(serialize(op));
    }

    virtual void visit(SinkInterface *sink) override
    {
        operatorsArray.append(serialize(sink));
    }

    virtual void visit(Directory *dir) override
    {
        directoriesArray.append(serialize(dir));
    }

    QJsonArray sourcesArray;
    QJsonArray operatorsArray;
    QJsonArray directoriesArray;
};

QJsonArray serialize_internal_connections(const AnalysisObjectVector &objects);

} // end namespace analysis

#endif /* __MVME_ANALYSIS_SERIALIZATION_H__ */
