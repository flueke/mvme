#ifndef __MESYTEC_DIAGNOSTICS_H__
#define __MESYTEC_DIAGNOSTICS_H__

#include "util.h"

class Hist1D;
class RealtimeData;

class MesytecDiagnostics : public QObject
{
    Q_OBJECT
public:
    explicit MesytecDiagnostics(QObject *parent = 0);

    void setEventAndModuleIndices(const QPair<int, int> &indices);
    inline int getEventIndex() const { return m_eventIndex; }
    inline int getModuleIndex() const { return m_moduleIndex; }
    void handleDataWord(quint32 data);
    RealtimeData *getRealtimeData() const { return m_rtd; }

    void clear(void);
    void calcAll(quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi);
    double getMean(quint16 chan);
    double getSigma(quint16 chan);
    quint32 getMeanchannel(quint16 chan);
    quint32 getSigmachannel(quint16 chan);
    quint32 getMax(quint16 chan);
    quint32 getMaxchan(quint16 chan);
    quint32 getCounts(quint16 chan);
    quint32 getChannel(quint16 chan, quint32 bin);

private:
    double mean[50];
    double sigma[50];
    quint32 meanchannel[50];
    quint32 sigmachannel[50];
    quint32 max[50];
    quint32 maxchan[50];
    quint32 counts[50];

    RealtimeData *m_rtd = nullptr;;
    QVector<Hist1D *> m_histograms;
    int m_eventIndex = -1;
    int m_moduleIndex = -1;
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

    private slots:
        void on_calcAll_clicked();
        void on_diagBin_valueChanged(int value);
        void on_diagChan_valueChanged(int value);

    private:
        void dispAll();
        void dispDiag1();
        void dispDiag2();
        void dispResultList();
        void dispChan();
        void dispRt();
        void updateRtDisplay();

        Ui::DiagnosticsWidget *ui;
        MesytecDiagnostics *m_diag;
};

#endif /* __MESYTEC_DIAGNOSTICS_H__ */
