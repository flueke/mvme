#ifndef __MVME_ANALYSIS_OBJECT_FACTORY_H__
#define __MVME_ANALYSIS_OBJECT_FACTORY_H__

#include "analysis_fwd.h"
#include "libmvme_export.h"

#include <QObject>
#include <QMap>
#include <QStringList>

namespace analysis
{

/* Note: The qobject_cast()s in the createXXX() functions are there to ensure
 * that the types properly implement the declared interface. They need to have
 * the Q_INTERFACES() macro either directly in their declaration or inherit it
 * from a parent class.
 */
template<typename T>
SourceInterface *createSource()
{
    auto result = new T;
    assert(qobject_cast<SourceInterface *>(result));
    return result;
}

template<typename T>
OperatorInterface *createOperator()
{
    auto result = new T;
    assert(qobject_cast<OperatorInterface *>(result));
    return result;
}

template<typename T>
SinkInterface *createSink()
{
    auto result = new T;
    assert(qobject_cast<SinkInterface *>(result));
    return result;
}

class LIBMVME_EXPORT ObjectFactory
{
    public:
        template<typename T>
        bool registerSource(const QString &name)
        {
            if (m_sourceRegistry.contains(name))
                return false;

            m_sourceRegistry.insert(name, &createSource<T>);

            return true;
        }

        template<typename T>
        bool registerSource()
        {
            QString className = T::staticMetaObject.className();
            return registerSource<T>(className);
        }

        template<typename T>
        bool registerOperator(const QString &name)
        {
            if (m_operatorRegistry.contains(name))
                return false;

            m_operatorRegistry.insert(name, &createOperator<T>);

            return true;
        }

        template<typename T>
        bool registerOperator()
        {
            QString className = T::staticMetaObject.className();
            return registerOperator<T>(className);
        }

        template<typename T>
        bool registerSink(const QString &name)
        {
            if (m_sinkRegistry.contains(name))
                return false;

            m_sinkRegistry.insert(name, &createSink<T>);

            return true;
        }

        template<typename T>
        bool registerSink()
        {
            QString className = T::staticMetaObject.className();
            return registerSink<T>(className);
        }

        AnalysisObject *makeObject(const QString &name);
        SourceInterface *makeSource(const QString &name);
        OperatorInterface *makeOperator(const QString &name);
        SinkInterface *makeSink(const QString &name);

        QStringList getSourceNames() const
        {
            return m_sourceRegistry.keys();
        }

        QStringList getOperatorNames() const
        {
            return m_operatorRegistry.keys();
        }

        QStringList getSinkNames() const
        {
            return m_sinkRegistry.keys();
        }

    private:
        QMap<QString, SourceInterface *(*)()> m_sourceRegistry;
        QMap<QString, OperatorInterface *(*)()> m_operatorRegistry;
        QMap<QString, SinkInterface *(*)()> m_sinkRegistry;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_FACTORY_H__ */
