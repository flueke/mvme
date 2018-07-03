#include "object_factory.h"
#include "analysis.h"

namespace analysis
{

AnalysisObject *ObjectFactory::makeObject(const QString &name)
{
    if (auto result = makeSource(name))
        return result;

    if (auto result = makeOperator(name))
        return result;

    if (auto result = makeSink(name))
        return result;

    return nullptr;
}

SourceInterface *ObjectFactory::makeSource(const QString &name)
{
    SourceInterface *result = nullptr;

    if (m_sourceRegistry.contains(name))
    {
        result = m_sourceRegistry[name]();
    }

    return result;
}

OperatorInterface *ObjectFactory::makeOperator(const QString &name)
{
    OperatorInterface *result = nullptr;

    if (m_operatorRegistry.contains(name))
    {
        result = m_operatorRegistry[name]();
    }

    return result;
}

SinkInterface *ObjectFactory::makeSink(const QString &name)
{
    SinkInterface *result = nullptr;

    if (m_sinkRegistry.contains(name))
    {
        result = m_sinkRegistry[name]();
    }

    return result;
}

} // end namespace analysis
