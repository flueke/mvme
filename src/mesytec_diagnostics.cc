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
#include "mesytec_diagnostics.h"
#include "ui_mesytec_diagnostics.h"
#include "realtimedata.h"
#include "histo1d.h"

#include <cmath>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

//
// MesytecDiagnostics
//
#define MAXIDX 40
#define MINIDX 41
#define ODD 42
#define EVEN 43
#define MAXFILT 44
#define MINFILT 45
#define ODDFILT 46
#define EVENFILT 47

#define MESYTEC_DIAGNOSTICS_DEBUG 0

static const int histoCount = 34;
static const int histoBits = 13;
static const int histoBins = 1 << histoBits;
static const int dataExtractMask = 0x00001FFF;

MesytecDiagnostics::MesytecDiagnostics(QObject *parent)
    : QObject(parent)
    , m_rtd(new RealtimeData(this))
    , m_eventBuffers(2)
{
    double minValue = 0;
    double maxValue = 1 << histoBits;


    for (int i=0; i<histoCount; ++i)
    {
        m_histograms.push_back(new Histo1D(histoBins, minValue, maxValue, this));
    }

    m_currentEventBuffer = &m_eventBuffers[0];
    m_lastEventBuffer = &m_eventBuffers[1];

    beginRun();
}

void MesytecDiagnostics::beginRun()
{
#if MESYTEC_DIAGNOSTICS_DEBUG
    qDebug() << __PRETTY_FUNCTION__;;
#endif

    clearChannelStats();
    m_nHeaders = 0;
    m_nEOEs = 0;
    m_nEvents = 0;

    m_eventBuffers[0].clear();
    m_eventBuffers[1].clear();

    m_currentStamp = -1;
    m_lastStamp = -1;


    for (auto histo: m_histograms)
    {
        histo->clear();
    }

    m_rtd->clearData();
}


void MesytecDiagnostics::setEventAndModuleIndices(const QPair<int, int> &indices)
{
#if MESYTEC_DIAGNOSTICS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << indices;
#endif
    m_eventIndex = indices.first;
    m_moduleIndex = indices.second;
}

void MesytecDiagnostics::beginEvent(int eventIndex)
{
    if (m_eventIndex != eventIndex)
    {
#if MESYTEC_DIAGNOSTICS_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "return cause eventindex not matching" << m_eventIndex << eventIndex;
#endif
        return;
    }

#if MESYTEC_DIAGNOSTICS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "doing beginEvent for eventIndex" << eventIndex;
#endif

    ++m_nEvents;
    m_nHeadersInEvent = 0;
    m_nEOEsInEvent = 0;
}

void MesytecDiagnostics::endEvent(int eventIndex)
{
    if (m_eventIndex != eventIndex)
    {
#if MESYTEC_DIAGNOSTICS_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "return cause eventindex not matching" << m_eventIndex << eventIndex;
#endif
        return;
    }

#if MESYTEC_DIAGNOSTICS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "doing endEvent for eventIndex" << eventIndex;
#endif

    bool doLog = false;
    QVector<QString> messages;

    //
    // consistency checks
    //
    if (m_reportMissingEOE && m_nHeadersInEvent != m_nEOEsInEvent)
    {
        doLog = true;
        messages.push_back(QSL("#Headers != #EOEs"));
    }

    if (m_reportMissingEOE && !m_nEOEsInEvent)
    {
        doLog = true;
        messages.push_back(QSL("No EOE in event"));
    }
    else if (m_reportCounterDiff)
    {
        // m_currentStamp was extracted from this events EOE
        if (m_stampMode == Counter && m_lastStamp >= 0)
        {
            auto diff = m_currentStamp - m_lastStamp;

            if (diff != 1)
            {
                doLog = true;
                messages.push_back(QString("Counter difference != 1: last=%1, current=%2")
                                   .arg(m_lastStamp)
                                   .arg(m_currentStamp));
            }
        }
        else if (m_stampMode == TimeStamp && m_lastStamp >= 0)
        {
            if (!(m_currentStamp > m_lastStamp))
            {
                doLog = true;
                messages.push_back(QString("Timestamp not increasing: last=%1, current=%2")
                                   .arg(m_lastStamp)
                                   .arg(m_currentStamp));
            }
        }
    }



    //
    // output generation
    //
    if (doLog)
    {
        QStringList messagesToLog;

        messagesToLog.append(QSL(">>>>>>>>>>>>>>>>>>>>"));

        if (m_lastEventBuffer->size())
        {

            messagesToLog.append(QString("Last Event (#%1, size=%2):")
                            .arg(m_nEvents - 1)
                            .arg(m_lastEventBuffer->size())
                            );

            for (int i=0; i<m_lastEventBuffer->size(); ++i)
            {
                messagesToLog.append(QString("  %1: 0x%2")
                                .arg(i, 2)
                                .arg(m_lastEventBuffer->at(i), 8, 16, QLatin1Char('0'))
                               );
            }

            messagesToLog.append(QString());
        }

        messagesToLog.append(QString("Current Event (#%1, size=%2):")
                        .arg(m_nEvents)
                        .arg(m_currentEventBuffer->size())
                        );

        for (int i=0; i<m_currentEventBuffer->size(); ++i)
        {
            messagesToLog.append(QString("  %1: 0x%2")
                            .arg(i, 2)
                            .arg(m_currentEventBuffer->at(i), 8, 16, QLatin1Char('0'))
                           );

        }

        messagesToLog.append(QString());

        for (const auto &msg: messages)
        {
            messagesToLog.append(msg);
        }

        messagesToLog.append(QSL("<<<<<<<<<<<<<<<<<<<<"));

        emit logMessage(messagesToLog.join("\n"));
    }

    if (m_logNextEvent)
    {
        BufferIterator iter(reinterpret_cast<u8 *>(m_currentEventBuffer->data()),
                            m_currentEventBuffer->size() * sizeof(u32),
                            BufferIterator::Align32);
        logBuffer(iter, [this] (const QString &str) { logMessage(str); });
        m_logNextEvent = false;
    }

    // swap buffers and clear new current buffer
    std::swap(m_currentEventBuffer, m_lastEventBuffer);
    m_currentEventBuffer->clear();

    m_lastStamp = m_currentStamp;
    m_currentStamp = -1;
}

void MesytecDiagnostics::handleDataWord(quint32 currentWord)
{
    m_currentEventBuffer->push_back(currentWord);


    //
    // header
    //
    bool header_found_flag = (currentWord & 0xC0000000) == 0x40000000;

    if (header_found_flag)
    {
        ++m_nHeaders;
        ++m_nHeadersInEvent;
    }

    //
    // data
    //
    bool data_found_flag = ((currentWord & 0xF0000000) == 0x10000000) // MDPP
        || ((currentWord & 0xFF800000) == 0x04000000); // MxDC

    if (data_found_flag)
    {
        u16 channel = (currentWord & 0x003F0000) >> 16; // 6 bit address
        u32 value   = (currentWord & dataExtractMask);

        if (channel < m_histograms.size())
        {
            m_histograms[channel]->fill(value);
        }

        m_rtd->insertData(channel, value);
    }

    //
    // eoe
    //
    bool eoe_found_flag = (currentWord & 0xC0000000) == 0xC0000000;

    if (eoe_found_flag)
    {
        ++m_nEOEs;
        ++m_nEOEsInEvent;

        // low 30 bits of timestamp/counter
        u32 low_stamp = (currentWord & 0x3FFFFFFF);

        m_currentStamp = low_stamp;

    }

#if 0 // currently not used
    //
    // extended timestamp
    //
    bool ext_ts_flag = ((currentWord & 0xFF800000) == 0x04800000);

    if (ext_ts_flag)
    {
        // high 16 bits of timestamp
        u32 high_stamp = (currentWord & 0x0000FFFF);
    }
#endif
}

void MesytecDiagnostics::processModuleData(int eventIndex, int moduleIndex, const u32 *data, u32 size)
{
    if (!(eventIndex == m_eventIndex && moduleIndex == m_moduleIndex))
    {
#if MESYTEC_DIAGNOSTICS_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "indices do not match. returning."
            << "m_eventIndex =" << m_eventIndex << ", eventIndex =" << eventIndex
            << ", m_moduleIndex =" << m_moduleIndex << ", moduleIndex =" << moduleIndex;
#endif
        return;
    }

#if MESYTEC_DIAGNOSTICS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "matching indices, handling" << size << " data words";
#endif

    for (u32 i = 0; i < size; ++i)
    {
        handleDataWord(data[i]);
    }
}

void MesytecDiagnostics::clearChannelStats(void)
{
    quint16 i;

    mean[0] = 0;
    for(i=0;i<50;i++){
        mean[i] = 0;
        sigma[i] = 0;
        meanchannel[i] = 0;
        sigmachannel[i] = 0;
        max[i] = 0;
        maxchan[i] = 0;
        counts[i] = 0;
    }
    // set minima to hi values
    mean[MINIDX] = 128000;
    sigma[MINIDX] = 128000;
    mean[MINFILT] = 128000;
    sigma[MINFILT] = 128000;
}

void MesytecDiagnostics::calcAll(quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi)
{
    quint16 i, j;
    quint16 evencounts = 0, evencounts2 = 0, oddcounts = 0, oddcounts2 = 0;
    double dval;
    //quint32 res = 1 << histoBits;
    qDebug("%d %d", binLo, binHi);
    //reset all old calculations
    clearChannelStats();

    // iterate through all channels (34 real channels max.)
    for(i=0; i<34; i++){
        // calculate means and maxima
        for(j=binLo; j<=binHi; j++){
            auto value = m_histograms[i]->getBinContent(j);

            mean[i] += value * j;
            counts[i] += value;

            if (value > max[i])
            {
                maxchan[i] = j;
                max[i] = value;
            }
        }

        if(counts[i])
            mean[i] /= counts[i];
        else
            mean[i] = 0;

        if(mean[i]){
            // calculate sigmas
            for(j=binLo; j<=binHi; j++){
                dval =  j - mean[i];
                dval *= dval;
                dval *= m_histograms[i]->getBinContent(j);
                //dval *= p_myHist->m_data[i*res + j];
                sigma[i] += dval;
            }
            if(counts[i])
                sigma[i] = std::sqrt(sigma[i]/counts[i]);
            else
                sigma[i] = 0;
        }
    }

    // find max and min mean and sigma
    for(i=0; i<34; i++){
        if(i>=lo && i <= hi){
            if(mean[i] > mean[MAXIDX]){
                mean[MAXIDX] = mean[i];
                meanchannel[MAXIDX] = i;
            }
            if(mean[i] < mean[MINIDX]){
                mean[MINIDX] = mean[i];
                meanchannel[MINIDX] = i;
            }
            if(sigma[i] > sigma[MAXIDX]){
                sigma[MAXIDX] = sigma[i];
                sigmachannel[MAXIDX] = i;
            }
            if(sigma[i] < sigma[MINIDX]){
                sigma[MINIDX] = sigma[i];
                sigmachannel[MINIDX] = i;
            }
        }
        if(i>=lo2 && i <= hi2){
            if(mean[i] > mean[MAXFILT]){
                mean[MAXFILT] = mean[i];
                meanchannel[MAXFILT] = i;
            }
            if(mean[i] < mean[MINFILT]){
                mean[MINFILT] = mean[i];
                meanchannel[MINFILT] = i;
            }
            if(sigma[i] > sigma[MAXFILT]){
                sigma[MAXFILT] = sigma[i];
                sigmachannel[MAXFILT] = i;
            }
            if(sigma[i] < sigma[MINFILT]){
                sigma[MINFILT] = sigma[i];
                sigmachannel[MINFILT] = i;
            }
        }
    }

    // now odds and evens
    for(i=0; i<34; i++){
        // calculate means and maxima
        // odd?
        if(i%2){
            if(i>=lo && i <= hi){
                mean[ODD] += mean[i];
                counts[ODD] += counts[i];
                oddcounts++;
            }
            if(i>=lo2 && i <= hi2){
                mean[ODDFILT] += mean[i];
                counts[ODDFILT] += counts[i];
                oddcounts2++;
            }
        }
        else{
            if(i>=lo && i <= hi){
                mean[EVEN] += mean[i];
                counts[EVEN] += counts[i];
                evencounts++;
            }
            if(i>=lo2 && i <= hi2){
                mean[EVENFILT] += mean[i];
                counts[EVENFILT] += counts[i];
                evencounts2++;
            }
        }
    }
    mean[EVEN] /= evencounts;
    mean[ODD] /= oddcounts;
    mean[EVENFILT] /= evencounts2;
    mean[ODDFILT] /= oddcounts2;

    // calculate sigmas
    for(i=0; i<34; i++){
        for(j=binLo; j<=binHi; j++){
            dval =  j - mean[i];
            dval *= dval,
            dval *= m_histograms[i]->getBinContent(j);
            //dval *= p_myHist->m_data[i*res + j];
            if(i%2){
                if(i>=lo && i <= hi)
                    sigma[ODD] += dval;
                if(i>=lo2 && i <= hi2)
                    sigma[ODDFILT] += dval;
            }
            else{
                if(i>=lo && i <= hi)
                    sigma[EVEN] += dval;
                if(i>=lo2 && i <= hi2)
                    sigma[EVENFILT] += dval;
            }
        }
    }
//    qDebug("%2.2f, %2.2f, %2.2f, %2.2f, %2.2f, %2.2f", counts[EVEN], sigma[EVEN], counts[ODD], sigma[ODD], sigma[32], sigma[33]);

    if(counts[EVEN])
        sigma[EVEN] = std::sqrt(sigma[EVEN]/counts[EVEN]);
    else
        sigma[EVEN] = 0;

    if(counts[ODD])
        sigma[ODD] = std::sqrt(sigma[ODD]/counts[ODD]);
    else
        sigma[ODD] = 0;

    if(counts[EVENFILT])
        sigma[EVENFILT] = std::sqrt(sigma[EVENFILT]/counts[EVENFILT]);
    else
        sigma[EVENFILT] = 0;

    if(counts[ODDFILT])
        sigma[ODDFILT] = std::sqrt(sigma[ODDFILT]/counts[ODDFILT]);
    else
        sigma[ODDFILT] = 0;

}

double MesytecDiagnostics::getMean(quint16 chan)
{
    return mean[chan];
}

double MesytecDiagnostics::getSigma(quint16 chan)
{
    return sigma[chan];
}

quint32 MesytecDiagnostics::getMeanchannel(quint16 chan)
{
    return meanchannel[chan];
}

quint32 MesytecDiagnostics::getSigmachannel(quint16 chan)
{
    return sigmachannel[chan];
}

quint32 MesytecDiagnostics::getMax(quint16 chan)
{
    return max[chan];
}

quint32 MesytecDiagnostics::getMaxchan(quint16 chan)
{
    return maxchan[chan];
}

quint32 MesytecDiagnostics::getCounts(quint16 chan)
{
    return counts[chan];
}

quint32 MesytecDiagnostics::getChannel(quint16 chan, quint32 bin)
{
    return m_histograms[chan]->getBinContent(bin);
}

//
// MesytecDiagnosticsWidget
//

static const int updateInterval = 500;
static const int resultMaxBlockCount = 10000;

MesytecDiagnosticsWidget::MesytecDiagnosticsWidget(std::shared_ptr<MesytecDiagnostics> diag, QWidget *parent)
    : MVMEWidget(parent)
    , ui(new Ui::DiagnosticsWidget)
    , m_diag(diag)
{
    ui->setupUi(this);
    ui->diagResult->setMaximumBlockCount(10000);

    connect(diag.get(), &MesytecDiagnostics::logMessage,
            this, &MesytecDiagnosticsWidget::onLogMessage);

    connect(ui->tb_dumpNextEvent, &QAbstractButton::clicked, this, [this]() { m_diag->setLogNextEvent(); });


    auto updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MesytecDiagnosticsWidget::updateDisplay);
    updateTimer->setInterval(updateInterval);
    updateTimer->start();
}

MesytecDiagnosticsWidget::~MesytecDiagnosticsWidget()
{
    delete ui;
}

void MesytecDiagnosticsWidget::clearResultsDisplay()
{
    ui->diagResult->clear();
}

void MesytecDiagnosticsWidget::onLogMessage(const QString &message)
{
    ui->diagResult->appendPlainText(message);
}

void MesytecDiagnosticsWidget::on_calcAll_clicked()
{
    QString str;
    quint16 lobin = 0, hibin = 8192;
    // evaluate bin filter
    if(ui->bin1->isChecked()){
        lobin = ui->binRange1lo->value();
        hibin = ui->binRange1hi->value();
    }
    if(ui->bin2->isChecked()){
        lobin = ui->binRange2lo->value();
        hibin = ui->binRange2hi->value();
    }
    if(ui->bin3->isChecked()){
        lobin = ui->binRange3lo->value();
        hibin = ui->binRange3hi->value();
    }

    m_diag->calcAll(ui->diagLowChannel2->value(), ui->diagHiChannel2->value(),
                    ui->diagLowChannel->value(), ui->diagHiChannel->value(),
                    lobin, hibin);
    dispAll();
}

void MesytecDiagnosticsWidget::on_pb_reset_clicked()
{
    m_diag->beginRun();
    dispAll();
}

void MesytecDiagnosticsWidget::on_diagBin_valueChanged(int /*value*/)
{
    dispChan();
}

void MesytecDiagnosticsWidget::on_diagChan_valueChanged(int /*value*/)
{
    dispChan();
}

void MesytecDiagnosticsWidget::on_diagLowChannel2_valueChanged(int)
{
    m_diag->getRealtimeData()->setFilter(
        ui->diagLowChannel2->value(),
        ui->diagHiChannel2->value());
}

void MesytecDiagnosticsWidget::on_diagHiChannel2_valueChanged(int)
{
    m_diag->getRealtimeData()->setFilter(
        ui->diagLowChannel2->value(),
        ui->diagHiChannel2->value());
}

void MesytecDiagnosticsWidget::on_rb_timestamp_toggled(bool checked)
{
    m_diag->m_stampMode = (checked ? MesytecDiagnostics::TimeStamp : MesytecDiagnostics::Counter);
}

void MesytecDiagnosticsWidget::dispAll()
{
    dispDiag1();
    dispDiag2();
    dispResultList();
}

void MesytecDiagnosticsWidget::dispDiag1()
{
    // upper range
    ui->meanmax->setText(QString::number(m_diag->getMean(MAXIDX), 'f', 4));
    ui->meanmaxchan->setText(QString::number(m_diag->getMeanchannel(MAXIDX)));
    ui->sigmax->setText(QString::number(m_diag->getSigma(MAXIDX), 'f', 4));
    ui->sigmaxchan->setText(QString::number(m_diag->getSigmachannel(MAXIDX)));
    ui->meanmin->setText(QString::number(m_diag->getMean(MINIDX), 'f', 4));
    ui->meanminchan->setText(QString::number(m_diag->getMeanchannel(MINIDX)));
    ui->sigmin->setText(QString::number(m_diag->getSigma(MINIDX), 'f', 4));
    ui->sigminchan->setText(QString::number(m_diag->getSigmachannel(MINIDX)));

    // odd even values upper range
    ui->meanodd->setText(QString::number(m_diag->getMean(ODD), 'f', 4));
    ui->meaneven->setText(QString::number(m_diag->getMean(EVEN), 'f', 4));
    ui->sigmodd->setText(QString::number(m_diag->getSigma(ODD), 'f', 4));
    ui->sigmeven->setText(QString::number(m_diag->getSigma(EVEN), 'f', 4));

    // delta between min and max mean values
    ui->delta_mean_maxmin->setText(QString::number(m_diag->getMean(MAXIDX) - m_diag->getMean(MINIDX), 'f', 4));
}

void MesytecDiagnosticsWidget::dispDiag2()
{
    // lower range
    ui->meanmax_filt->setText(QString::number(m_diag->getMean(MAXFILT), 'f', 4));
    ui->meanmaxchan_filt->setText(QString::number(m_diag->getMeanchannel(MAXFILT)));
    ui->sigmax_filt->setText(QString::number(m_diag->getSigma(MAXFILT), 'f', 4));
    ui->sigmaxchan_filt->setText(QString::number(m_diag->getSigmachannel(MAXFILT)));
    ui->meanmin_filt->setText(QString::number(m_diag->getMean(MINFILT), 'f', 4));
    ui->meanminchan_filt->setText(QString::number(m_diag->getMeanchannel(MINFILT)));
    ui->sigmin_filt->setText(QString::number(m_diag->getSigma(MINFILT), 'f', 4));
    ui->sigminchan_filt->setText(QString::number(m_diag->getSigmachannel(MINFILT)));

    // odd even values lower range
    ui->meanodd_filt->setText(QString::number(m_diag->getMean(ODDFILT), 'f', 4));
    ui->meaneven_filt->setText(QString::number(m_diag->getMean(EVENFILT), 'f', 4));
    ui->sigmodd_filt->setText(QString::number(m_diag->getSigma(ODDFILT), 'f', 4));
    ui->sigmeven_filt->setText(QString::number(m_diag->getSigma(EVENFILT), 'f', 4));

    // delta between min and max mean values
    ui->delta_mean_maxmin_filt->setText(QString::number(m_diag->getMean(MAXFILT) - m_diag->getMean(MINFILT), 'f', 4));
}

void MesytecDiagnosticsWidget::dispResultList()
{
    QString text;

    for(quint16 i=0;i<34;i++)
    {
        auto str = QStringLiteral("%1:\t mean: %2,\t sigma: %3,\t\t counts: %4\n")
            .arg(i)
            .arg(m_diag->getMean(i), 0, 'f', 2)
            .arg(m_diag->getSigma(i), 0, 'f', 2)
            .arg(m_diag->getCounts(i));

        text.append(str);
    }

    ui->diagResult->setPlainText(text);
}

void MesytecDiagnosticsWidget::dispChan()
{
    ui->diagCounts->setText(QString::number(m_diag->getChannel(ui->diagChan->value(), ui->diagBin->value())));
}

void MesytecDiagnosticsWidget::dispRt()
{
    auto rtd = m_diag->getRealtimeData();
    QString str;
    ui->rtMeanEven->setText(QString::number(rtd->getRdMean(0), 'f', 4));
    ui->rtMeanOdd->setText(QString::number(rtd->getRdMean(1), 'f', 4));
    ui->rtSigmEven->setText(QString::number(rtd->getRdSigma(0), 'f', 4));
    ui->rtSigmOdd->setText(QString::number(rtd->getRdSigma(1), 'f', 4));
}

void MesytecDiagnosticsWidget::updateDisplay()
{
    m_diag->getRealtimeData()->calcData();
    dispRt();

    auto headers = m_diag->getNumberOfHeaders();
    auto eoes = m_diag->getNumberOfEOEs();
    double delta = std::abs(static_cast<double>(headers) - static_cast<double>(eoes));

    ui->label_nHeaders->setText(QString("%L1").arg(headers));
    ui->label_nEOEs->setText(QString("%L1").arg(eoes));
    ui->label_delta->setText(QString("%L1").arg(delta));
    ui->label_nEvents->setText(QString("%L1").arg(m_diag->m_nEvents));
}

void MesytecDiagnosticsWidget::on_tb_saveResultList_clicked()
{
    QSettings settings;

    QString lastFile = settings.value("Files/LastDiagnosticsResultFile").toString();

    if (lastFile.isEmpty())
        lastFile = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", lastFile,
                                                    "Text Files (*.txt);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".txt";

        if (QFile::exists(fileName))
        {
            int result = QMessageBox::question(this, "Overwrite?", "Overwrite the file?",
                                               QMessageBox::Yes | QMessageBox::No);

            if (result == QMessageBox::No)
                return;
        }
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return;
    }

    int result = outFile.write(ui->diagResult->toPlainText().toLocal8Bit());

    if (result >= 0)
    {
        settings.setValue("Files/LastDiagnosticsResultFile", fileName);
    }
}

void MesytecDiagnosticsWidget::on_cb_reportCounterDiff_toggled(bool checked)
{
    m_diag->m_reportCounterDiff = checked;
}

void MesytecDiagnosticsWidget::on_cb_reportMissingEOE_toggled(bool checked)
{
    m_diag->m_reportMissingEOE = checked;
}
