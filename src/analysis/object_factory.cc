#include "object_factory.h"
#include "analysis.h"

namespace analysis
{

/* Returns a new object that was registered under the given name.
 * In the case of duplicate names priority is as follows: sources, operators, sinks.
 */
AnalysisObject *ObjectFactory::makeObject(const QString &name) const
{
    if (auto result = makeSource(name))
        return result;

    if (auto result = makeOperator(name))
        return result;

    if (auto result = makeSink(name))
        return result;

    return nullptr;
}

SourceInterface *ObjectFactory::makeSource(const QString &name) const
{
    SourceInterface *result = nullptr;

    if (m_sourceRegistry.contains(name))
    {
        result = m_sourceRegistry[name]();
    }

    return result;
}

OperatorInterface *ObjectFactory::makeOperator(const QString &name) const
{
    OperatorInterface *result = nullptr;

    if (m_operatorRegistry.contains(name))
    {
        result = m_operatorRegistry[name]();
    }

    return result;
}

SinkInterface *ObjectFactory::makeSink(const QString &name) const
{
    SinkInterface *result = nullptr;

    if (m_sinkRegistry.contains(name))
    {
        result = m_sinkRegistry[name]();
    }

    return result;
}

} // end namespace analysis
