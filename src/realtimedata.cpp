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
#include "realtimedata.h"
#include <math.h>

RealtimeData::RealtimeData(QObject *parent) :
    QObject(parent)
{
    m_pRD = new quint16[CHANPERMOD * CHANSIZE * 2];
    m_lo = 0;
    m_hi = 34;
    qDebug("new RTdata %d, %d", m_lo, m_hi);
    clearData();
}

RealtimeData::~RealtimeData()
{
    delete[] m_pRD;
}

void RealtimeData::clearData()
{
    for(quint8 i=0; i<CHANPERMOD; i++){
        m_RDbufferCounter[i] = 0;
        m_RDreadPointer[i] = 0;
        m_RDwritePointer[i] = 0;
        m_RDmean[i] = 0;
        m_RDsigma[i] = 0;
    }
    m_RDmean[CHANPERMOD] = 0;
    m_RDsigma[CHANPERMOD] = 0;
    m_RDmean[CHANPERMOD+1] = 0;
    m_RDsigma[CHANPERMOD+1] = 0;
}

void RealtimeData::insertData(quint8 chan, quint16 value)
{
    // check limit
    if(chan < m_lo && chan > m_hi)
        return;
    m_pRD[chan*CHANSIZE+m_RDwritePointer[chan]] = value;
    m_RDwritePointer[chan]++;
    if(m_RDwritePointer[chan] == CHANSIZE)
        m_RDwritePointer[chan] = 0;
    if(m_RDbufferCounter[chan] < (CHANSIZE-1))
        m_RDbufferCounter[chan]++;
}

void RealtimeData::calcData(void)
{
    quint16 i, j;
    double dval;
    quint8 odds=0, evens=0;

    for(i=0;i<CHANPERMOD+2;i++){
        m_RDmean[i] = 0;
        m_RDsigma[i] = 0;
    }
    // iterate through ringbuffer for 36 means
    for(j=0;j<CHANPERMOD;j++){
        for(i=0;i<m_RDbufferCounter[j];i++){
            m_RDmean[j] += m_pRD[j*CHANSIZE+i];
        }
        if(m_RDbufferCounter[j])
            m_RDmean[j] /= m_RDbufferCounter[j];
        else
            m_RDmean[j] = 0;
    }
    // now iterate again for sigma
    for(j=0;j<CHANPERMOD;j++){
        for(i=0;i<m_RDbufferCounter[j];i++){
            dval = m_pRD[j*CHANSIZE+i] - m_RDmean[j];
            dval *= dval;
            m_RDsigma[j] += dval;
        }
        if(m_RDbufferCounter[j])
            m_RDsigma[j] /= m_RDbufferCounter[j];
        else
            m_RDsigma[j] = 0;
        m_RDsigma[j] = sqrt(m_RDsigma[j]);
//    qDebug("%d, %2.2f", slot, m_RDsigma[slot]);
    }
    // now calc mean/sigma for odd / even
    for(j=m_lo; j<=m_hi;j++){
        if(j%2){
            m_RDmean[CHANPERMOD+1] += m_RDmean[j];
            m_RDsigma[CHANPERMOD+1] += m_RDsigma[j];
            odds++;
        }
        else{
            m_RDmean[CHANPERMOD] += m_RDmean[j];
            m_RDsigma[CHANPERMOD] += m_RDsigma[j];
            evens++;
        }
    }
    if(odds){
        m_RDmean[CHANPERMOD+1] /= odds;
        m_RDsigma[CHANPERMOD+1] /= odds;
    }
    else{
        m_RDmean[CHANPERMOD+1] =0;
        m_RDsigma[CHANPERMOD+1] =0;
    }
    if(evens){
        m_RDmean[CHANPERMOD] /= evens;
        m_RDsigma[CHANPERMOD] /= evens;
    }
    else{
        m_RDmean[CHANPERMOD] =0;
        m_RDsigma[CHANPERMOD] =0;
    }
}

double RealtimeData::getRdMean(quint8 slot)
{
    return m_RDmean[CHANPERMOD+slot];
}

double RealtimeData::getRdSigma(quint8 slot)
{
    return m_RDsigma[CHANPERMOD+slot];
}

void RealtimeData::setFilter(quint8 lo, quint8 hi)
{
    qDebug("setRtFilter %d %d", lo, hi);
    clearData();
    m_lo = lo;
    m_hi = hi;
}
