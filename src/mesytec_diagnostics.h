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
#ifndef __MESYTEC_DIAGNOSTICS_H__
#define __MESYTEC_DIAGNOSTICS_H__

#include "util.h"

class Histo1D;
class RealtimeData;

class MesytecDiagnostics : public QObject
{
    Q_OBJECT
signals:
    void logMessage(const QString &message);

public:
    explicit MesytecDiagnostics(QObject *parent = 0);

    void setEventAndModuleIndices(const QPair<int, int> &indices);
    inline int getEventIndex() const { return m_eventIndex; }
    inline int getModuleIndex() const { return m_moduleIndex; }
    void beginEvent(int eventIndex);
    void endEvent(int eventIndex);
    void processModuleData(int eventIndex, int moduleIndex, const u32 *data, u32 size);
    RealtimeData *getRealtimeData() const { return m_rtd; }
    void setLogNextEvent() { m_logNextEvent = true; }

    void clearChannelStats(void);
    // resets all internal data. to be called when a new run/replay starts
    void beginRun();
    void calcAll(quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi);
    double getMean(quint16 chan);
    double getSigma(quint16 chan);
    quint32 getMeanchannel(quint16 chan);
    quint32 getSigmachannel(quint16 chan);
    quint32 getMax(quint16 chan);
    quint32 getMaxchan(quint16 chan);
    quint32 getCounts(quint16 chan);
    quint32 getChannel(quint16 chan, quint32 bin);
    u64 getNumberOfHeaders() const { return m_nHeaders; }
    u64 getNumberOfEOEs() const { return m_nEOEs; }

    friend class MesytecDiagnosticsWidget;

    enum StampMode { TimeStamp, Counter};

private:
    void handleDataWord(quint32 data);

    double mean[50];
    double sigma[50];
    quint32 meanchannel[50];
    quint32 sigmachannel[50];
    quint32 max[50];
    quint32 maxchan[50];
    quint32 counts[50];
    u64 m_nHeaders = 0;
    u64 m_nEOEs = 0;

    u64 m_nEvents = 0;

    RealtimeData *m_rtd = nullptr;;
    QVector<Histo1D *> m_histograms;
    int m_eventIndex = -1;
    int m_moduleIndex = -1;

    u32 m_nHeadersInEvent = 0;
    u32 m_nEOEsInEvent = 0;
    QVarLengthArray<QVector<u32>, 2> m_eventBuffers;
    QVector<u32> *m_currentEventBuffer;
    QVector<u32> *m_lastEventBuffer;
    s64 m_currentStamp = -1;
    s64 m_lastStamp = -1;
    StampMode m_stampMode = Counter;
    bool m_reportCounterDiff = false;
    bool m_reportMissingEOE = false;
    bool m_logNextEvent = false;
};

namespace Ui
{
    class DiagnosticsWidget;
}

class MesytecDiagnosticsWidget: public MVMEWidget
{
    Q_OBJECT
    public:
        MesytecDiagnosticsWidget(std::shared_ptr<MesytecDiagnostics> diag, QWidget *parent = 0);
        ~MesytecDiagnosticsWidget();

        void clearResultsDisplay();

    private slots:
        void on_calcAll_clicked();
        void on_pb_reset_clicked();
        void on_diagBin_valueChanged(int value);
        void on_diagChan_valueChanged(int value);
        void on_diagLowChannel2_valueChanged(int value);
        void on_diagHiChannel2_valueChanged(int value);
        void on_rb_timestamp_toggled(bool checked);
        void onLogMessage(const QString &);
        void on_tb_saveResultList_clicked();
        void on_cb_reportCounterDiff_toggled(bool checked);
        void on_cb_reportMissingEOE_toggled(bool checked);

    private:
        void dispAll();
        void dispDiag1();
        void dispDiag2();
        void dispResultList();
        void dispChan();
        void dispRt();
        void updateDisplay();

        Ui::DiagnosticsWidget *ui;
        std::shared_ptr<MesytecDiagnostics> m_diag;
};

#endif /* __MESYTEC_DIAGNOSTICS_H__ */
