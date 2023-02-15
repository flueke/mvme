/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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

    if (auto result = makeGeneric(name))
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

AnalysisObject *ObjectFactory::makeGeneric(const QString &name) const
{
    AnalysisObject *result = nullptr;

    if (m_genericRegistry.contains(name))
        result = m_genericRegistry[name]();

    return result;
}

} // end namespace analysis
