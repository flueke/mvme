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
#ifndef REALTIMEDATA_H
#define REALTIMEDATA_H

#define CHANPERMOD 36
#define CHANSIZE 2500 // was 10000, but when changing device thresholds it takes too long for the number to catch up

#include <QObject>

class RealtimeData : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeData(QObject *parent = 0);
    ~RealtimeData();
    void clearData();
    void insertData(quint8 chan, quint16 value);
    void calcData(void);
    double getRdMean(quint8 slot);
    double getRdSigma(quint8 slot);
    void setFilter(quint8 lo, quint8 hi);

signals:
    
public slots:

private:
    quint16* m_pRD;
    quint16 m_RDreadPointer[CHANPERMOD];
    quint16 m_RDwritePointer[CHANPERMOD];
    quint16 m_RDbufferCounter[CHANPERMOD];
    double m_RDmean[CHANPERMOD+2];
    double m_RDsigma[CHANPERMOD+2];
    quint8 m_lo;
    quint8 m_hi;
};

#endif // REALTIMEDATA_H
