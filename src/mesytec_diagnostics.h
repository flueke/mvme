#ifndef __MESYTEC_DIAGNOSTICS_H__
#define __MESYTEC_DIAGNOSTICS_H__

#include "util.h"

class Hist1D;
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
    void beginEvent();
    void endEvent();
    void handleDataWord(quint32 data);
    RealtimeData *getRealtimeData() const { return m_rtd; }

    void clearChannelStats(void);
    // resets all internal data. to be called when a new run/replay starts
    void reset();
    void calcAll(quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi);
    double getMean(quint16 chan);
    double getSigma(quint16 chan);
    quint32 getMeanchannel(quint16 chan);
    quint32 getSigmachannel(quint16 chan);
    quint32 getMax(quint16 chan);
    quint32 getMaxchan(quint16 chan);
    quint32 getCounts(quint16 chan);
    quint32 getChannel(quint16 chan, quint32 bin);
    quint32 getNumberOfHeaders() const { return m_nHeaders; }
    quint32 getNumberOfEOEs() const { return m_nEOEs; }

    friend class MesytecDiagnosticsWidget;

    enum StampMode { TimeStamp, Counter};

private:
    double mean[50];
    double sigma[50];
    quint32 meanchannel[50];
    quint32 sigmachannel[50];
    quint32 max[50];
    quint32 maxchan[50];
    quint32 counts[50];
    quint32 m_nHeaders = 0;
    quint32 m_nEOEs = 0;

    u64 m_nEvents = 0;

    RealtimeData *m_rtd = nullptr;;
    QVector<Hist1D *> m_histograms;
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
    bool m_reportCounterDiff = true;
    bool m_reportMissingEOE = true;
};

namespace Ui
{
    class DiagnosticsWidget;
}

class MesytecDiagnosticsWidget: public MVMEWidget
{
    Q_OBJECT
    public:
        MesytecDiagnosticsWidget(MesytecDiagnostics *diag, QWidget *parent = 0);
        ~MesytecDiagnosticsWidget();

        void clearResultsDisplay();

    private slots:
        void on_calcAll_clicked();
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
        MesytecDiagnostics *m_diag;
};

#endif /* __MESYTEC_DIAGNOSTICS_H__ */
