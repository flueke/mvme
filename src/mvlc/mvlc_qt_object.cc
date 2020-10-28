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
#include "mvlc/mvlc_qt_object.h"

#include <cassert>
#include <QDebug>
#include <QtConcurrent>

#include "mesytec-mvlc/mvlc_basic_interface.h"

namespace mesytec
{
namespace mvme_mvlc
{

MVLCObject::MVLCObject(mvlc::MVLC mvlc, QObject *parent)
    : QObject(parent)
    , m_mvlc(mvlc)
    , m_state(Disconnected)
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->getConnectionInfo();

    if (m_mvlc.isConnected())
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->getConnectionInfo();
}

void MVLCObject::setState(const State &newState)
{
    if (m_state != newState)
    {
        auto prevState = m_state;
        m_state = newState;

        if (newState == Connected)
            clearStackErrorCounters();

        emit stateChanged(prevState, newState);
    }
};

} // end namespace mvme_mvlc
} // end namespace mesytec
