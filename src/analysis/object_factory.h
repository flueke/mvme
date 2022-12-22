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

template<typename T>
AnalysisObject *createGeneric()
{
    auto result = new T;
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

        template<typename T>
        bool registerGeneric(const QString &name)
        {
            if (m_genericRegistry.contains(name))
                return false;

            m_genericRegistry.insert(name, &createGeneric<T>);

            return true;
        }

        template<typename T>
        bool registerGeneric()
        {
            QString className = T::staticMetaObject.className();
            return registerGeneric<T>(className);
        }

        AnalysisObject *makeObject(const QString &name) const;
        SourceInterface *makeSource(const QString &name) const;
        OperatorInterface *makeOperator(const QString &name) const;
        SinkInterface *makeSink(const QString &name) const;
        AnalysisObject *makeGeneric(const QString &name) const;

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

        QStringList getGenericNames() const
        {
            return m_genericRegistry.keys();
        }

    private:
        QMap<QString, SourceInterface *(*)()> m_sourceRegistry;
        QMap<QString, OperatorInterface *(*)()> m_operatorRegistry;
        QMap<QString, SinkInterface *(*)()> m_sinkRegistry;
        QMap<QString, AnalysisObject *(*)()> m_genericRegistry;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_FACTORY_H__ */
