#include "sis3153_util.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include "sis3153.h"
#include "sis3153_readout_worker.h"
#include "sis3153/sis3153ETH_vme_class.h"

static const QVector<const char *> LabelTexts =
{
    "recv_timeout_sec",
    "recv_timeout_usec",
    "info_udp_receive_timeout_counter",
    "info_wrong_cmd_ack_counter",
    "info_wrong_received_nof_bytes_counter",
    "info_wrong_received_packet_id_counter",
    "info_clear_UdpReceiveBuffer_counter",
    "info_read_dma_packet_reorder_counter",
    "udp_single_read_receive_ack_retry_counter",
    "udp_single_read_req_retry_counter",
    "udp_single_write_receive_ack_retry_counter",
    "udp_single_write_req_retry_counter",
    "udp_dma_read_receive_ack_retry_counter",
    "udp_dma_read_req_retry_counter",
    "udp_dma_write_receive_ack_retry_counter",
    "udp_dma_write_req_retry_counter",
};

static const QVector<const char *> RdoCounterLabels =
{
    "stackListCounts",
    "BERR Counters",
    "multiEventPackets",
    "embeddedEventCount",
    "lostEvents",
};

SIS3153DebugWidget::SIS3153DebugWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    setWindowTitle(QSL("SIS3153 Debug Widget"));

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(0);

    auto tabWidget = new QTabWidget;
    widgetLayout->addWidget(tabWidget);

    auto readoutCountersWidget = new QWidget;
    auto libCountersWidget = new QWidget;
    auto toolsWidget  = new QWidget;

    tabWidget->addTab(readoutCountersWidget, QSL("&Readout Counters"));
    tabWidget->addTab(libCountersWidget, QSL("&Lib Counters"));
    tabWidget->addTab(toolsWidget, QSL("&Tools"));

    //
    // readout counters
    //
    auto readoutCountersLayout = new QFormLayout(readoutCountersWidget);

    readoutCountersLayout->addRow(make_aligned_label(QSL("<b>sis3153 readout</b>"), Qt::AlignCenter));

    for (const char *text: RdoCounterLabels)
    {
        auto label = new QLabel;
        readoutCountersLayout->addRow(text, label);
        m_rdoCounterLabels.push_back(label);
    }

    //
    // sis library internal counters
    //
    auto libCountersLayout = new QFormLayout(libCountersWidget);

    libCountersLayout->addRow(make_aligned_label(QSL("<b>sis3153 lib</b>"), Qt::AlignCenter));

    for (const char *text: LabelTexts)
    {
        auto label = new QLabel;
        libCountersLayout->addRow(text, label);
        m_labels.push_back(label);
    }

    auto resetButton = new QPushButton(QSL("Reset Counters"));
    libCountersLayout->addRow(resetButton);
    connect(resetButton, &QPushButton::clicked, this, &SIS3153DebugWidget::resetCounters);

    // refresh every second
    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &SIS3153DebugWidget::refresh);
    refreshTimer->setInterval(1000);
    refreshTimer->start();


    //
    // tools
    //
    auto toolsLayout = new QGridLayout(toolsWidget);

    auto le_regAddress = new QLineEdit;
    le_regAddress->setText(QSL("0x01000017"));
    auto le_regResult  = new QLineEdit;
    auto pb_readRegister = new QPushButton(QSL("Read"));

    connect(pb_readRegister, &QPushButton::clicked, this, [=]() {

        if (auto sis = qobject_cast<SIS3153 *>(m_context->getVMEController()))
        {
            u32 addr  = le_regAddress->text().toUInt(nullptr, 0);
            u32 value = 0;

            auto err = sis->readRegister(addr, &value);

            if (err.isError())
            {
                le_regResult->setText(err.toString());
            }
            else
            {
                le_regResult->setText(QString("0x%1 / %2")
                                      .arg(value, 8, 16, QLatin1Char('0'))
                                      .arg(value)
                                     );
            }

        }
    });

    s32 row = 0;
    toolsLayout->addWidget(le_regAddress, row, 0);
    toolsLayout->addWidget(le_regResult, row, 1);
    toolsLayout->addWidget(pb_readRegister, row, 2);
    row++;

    refresh();
}

void SIS3153DebugWidget::refresh()
{
    auto sis = qobject_cast<SIS3153 *>(m_context->getVMEController());
    if (!sis) return;

    // sis library
    {
        auto sisImpl = sis->getImpl();

        s32 i = 0;

        m_labels[i++]->setText(QString::number(sisImpl->recv_timeout_sec));
        m_labels[i++]->setText(QString::number(sisImpl->recv_timeout_usec));

        m_labels[i++]->setText(QString::number(sisImpl->info_udp_receive_timeout_counter));
        m_labels[i++]->setText(QString::number(sisImpl->info_wrong_cmd_ack_counter));
        m_labels[i++]->setText(QString::number(sisImpl->info_wrong_received_nof_bytes_counter));
        m_labels[i++]->setText(QString::number(sisImpl->info_wrong_received_packet_id_counter));


        m_labels[i++]->setText(QString::number(sisImpl->info_clear_UdpReceiveBuffer_counter));
        m_labels[i++]->setText(QString::number(sisImpl->info_read_dma_packet_reorder_counter));

        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_single_read_receive_ack_retry_counter));
        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_single_read_req_retry_counter));

        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_single_write_receive_ack_retry_counter));
        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_single_write_req_retry_counter));

        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_dma_read_receive_ack_retry_counter));
        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_dma_read_req_retry_counter));

        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_dma_write_receive_ack_retry_counter));
        m_labels[i++]->setText(QString::number((u32) sisImpl->udp_dma_write_req_retry_counter));
    }

    // readout worker
    {
        auto rdoWorker = qobject_cast<SIS3153ReadoutWorker *>(m_context->getReadoutWorker());

        if (!rdoWorker)
            return;

        const auto counters = rdoWorker->getCounters();

        QString pcText; // packet count text

        for (s32 si = 0; si < SIS3153Constants::NumberOfStackLists; si++)
        {
            auto count = counters.stackListCounts[si];
            if (count)
            {
                if (!pcText.isEmpty()) pcText += QSL("\n");
                pcText += QString(QSL("stackList=%1, count=%2")).arg(si).arg(count);
                if (si == counters.watchdogStackList)
                    pcText += QSL(" (watchdog)");
            }
        }

        QString berrText;

        for (s32 si = 0; si < SIS3153Constants::NumberOfStackLists; si++)
        {
            auto berrBlock = counters.stackListBerrCounts_Block[si];
            auto berrRead  = counters.stackListBerrCounts_Read[si];
            auto berrWrite = counters.stackListBerrCounts_Write[si];

            if (berrBlock || berrRead || berrWrite)
            {
                if (!berrText.isEmpty()) berrText += QSL("\n");

                berrText += (QString(QSL("stackList=%1, blt=%2, read=%3, write=%4"))
                             .arg(si).arg(berrBlock).arg(berrRead).arg(berrWrite));

                if (si == counters.watchdogStackList)
                    berrText += QSL(" (watchdog)");
            }
        }

        s32 i = 0;
        m_rdoCounterLabels[i++]->setText(pcText);
        m_rdoCounterLabels[i++]->setText(berrText);
        m_rdoCounterLabels[i++]->setText(QString::number(counters.multiEventPackets));
        m_rdoCounterLabels[i++]->setText(QString::number(counters.embeddedEvents));
        m_rdoCounterLabels[i++]->setText(QString::number(counters.lostPackets));
    }
}

void SIS3153DebugWidget::resetCounters()
{
    auto sis = qobject_cast<SIS3153 *>(m_context->getVMEController());
    if (!sis) return;

    auto sisImpl = sis->getImpl();

    sisImpl->info_udp_receive_timeout_counter = 0;
    sisImpl->info_wrong_cmd_ack_counter = 0;
    sisImpl->info_wrong_received_nof_bytes_counter = 0;
    sisImpl->info_wrong_received_packet_id_counter = 0;


    sisImpl->info_clear_UdpReceiveBuffer_counter = 0;
    sisImpl->info_read_dma_packet_reorder_counter = 0;

    sisImpl->udp_single_read_receive_ack_retry_counter = 0;
    sisImpl->udp_single_read_req_retry_counter = 0;

    sisImpl->udp_single_write_receive_ack_retry_counter = 0;
    sisImpl->udp_single_write_req_retry_counter = 0;

    sisImpl->udp_dma_read_receive_ack_retry_counter = 0;
    sisImpl->udp_dma_read_req_retry_counter = 0;

    sisImpl->udp_dma_write_receive_ack_retry_counter = 0;
    sisImpl->udp_dma_write_req_retry_counter = 0;

    refresh();
}

void format_sis3153_single_event(
    QTextStream &out,
    u64 bufferNumber,
    u8 packetAck, u8 packetIdent, u8 packetStatus,
    u8 *data, size_t size)
{
    int stackList = packetAck & SIS3153Constants::AckStackListMask;

    if (size)
    {
        BufferIterator iter(data, size);
        u32 beginHeader = iter.extractU32();
        s32 packetNumber = (beginHeader & SIS3153Constants::BeginEventPacketNumberMask);

        out << (QString("beginHeader=0x%1, packetNumber=%2 (0x%3)")
                .arg(beginHeader, 8, 16, QLatin1Char('0'))
                .arg(packetNumber)
                .arg(packetNumber, 6, 16, QLatin1Char('0'))
               )
            << endl;
    }

#if 0
    BufferIterator iter(data, size);

    while (iter.longwordsLeft())
    {
        out << "  " << tmp.arg(iter.extractU32(), 8, 16, QLatin1Char('0')) << endl;
    }
#endif

    debugOutputBuffer(out, data, size);
}

void format_sis3153_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber)
{
    try
    {
        out << "buffer #" << bufferNumber
            << ": bytes=" << buffer->used
            << ", shortwords=" << buffer->used/sizeof(u16)
            << ", longwords=" << buffer->used/sizeof(u32)
            << endl;

        QString tmp;
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

        u8 padding      = buffer->data[0];
        u8 packetAck    = buffer->data[1];
        u8 packetIdent  = buffer->data[2];
        u8 packetStatus = buffer->data[3];
        u32 header      = iter.extractU32();
        int stackList   = packetAck & SIS3153Constants::AckStackListMask;

        bool isMultiEventPacket = SIS3153Constants::MultiEventPacketAck;

        tmp = (QString("buffer #%1: header=0x%2, padding=0x%3"
                       ", packetAck=0x%4, packetIdent=0x%5, packetStatus=0x%6, stackList=%7")
               .arg(bufferNumber)
               .arg(header, 8, 16, QLatin1Char('0'))
               .arg(padding, 2, 16, QLatin1Char('0'))
               .arg(packetAck, 2, 16, QLatin1Char('0'))
               .arg(packetIdent, 2, 16, QLatin1Char('0'))
               .arg(packetStatus, 2, 16, QLatin1Char('0'))
               .arg(isMultiEventPacket ? QSL("multiEvent") : QString::number(stackList))
              );

        out << tmp << endl;

        if (packetAck == SIS3153Constants::MultiEventPacketAck)
        {
            out << "buffer #" << bufferNumber << ": multi event packet" << endl;

            while (iter.longwordsLeft())
            {
                u32 eventHeader    = iter.extractU32();
                u8  internalAck    = eventHeader & 0xff;    // same as packetAck in non-buffered mode
                u8  internalIdent  = (eventHeader >> 24) & 0xff; // Not sure about this byte
                u8  internalStatus = 0; // FIXME: what's up with this?

                u16 length = ((eventHeader & 0xff0000) >> 16) | (eventHeader & 0xff00); // length in 32-bit words
                int stackList = internalAck & SIS3153Constants::AckStackListMask;

                tmp = (QString("buffer #%1: embedded ack=0x%2, ident=0x%3, status=0x%4"
                               ", length=%5 (%6 bytes), header=0x%7, stackList=%8")
                       .arg(bufferNumber)
                       .arg((u32)internalAck, 2, 16, QLatin1Char('0'))
                       .arg((u32)internalIdent, 2, 16, QLatin1Char('0'))
                       .arg((u32)internalStatus, 2, 16, QLatin1Char('0'))
                       .arg(length)
                       .arg(length * sizeof(u32))
                       .arg(eventHeader, 8, 16, QLatin1Char('0'))
                       .arg(stackList)
                       );

                out << tmp << endl;

                format_sis3153_single_event(
                    out,
                    bufferNumber,
                    internalAck, internalIdent, internalStatus,
                    iter.buffp, length * sizeof(u32));

                iter.skip(sizeof(u32), length); // advance the local iterator by the embedded events length
            }
        }
        else
        {
            //debugOutputBuffer(out, buffer->data, buffer->used);

            bool isLastPacket = (packetAck & SIS3153Constants::AckIsLastPacketMask);
            if (!isLastPacket)
            {
                out << "buffer #" << bufferNumber << ": middle part of a partial event" << endl;
            }
            else
            {
                out << "buffer #" << bufferNumber << ": single event or end of a partial event" << endl;
            }

            format_sis3153_single_event(
                out,
                bufferNumber,
                packetAck, packetIdent, packetStatus,
                buffer->data + sizeof(u32), buffer->used);
        }

    }
    catch (const end_of_buffer &)
    {
        out << "!!! end of buffer reached unexpectedly !!!" << endl;
    }
}

/* Note: this takes a 16-bit part of a StackListControlValue. If you want to
 * print out the "disable" part shift the original 32-bit value down by 16
 * first. */
QString format_sis3153_stacklist_control_value(u16 value)
{
    using namespace SIS3153Registers::StackListControlValues;

    QStringList strings;

    if (value & ListBufferEnable)
        strings << QSL("Buffering");

    if (value & Timer2Enable)
        strings << QSL("Timer2");

    if (value & Timer1Enable)
        strings << QSL("Timer1");

    if (value & StackListEnable)
        strings << QSL("StackListEnable");

    auto valueText = strings.join(QSL(" | "));

    QString result = (QString(QSL("StackListControl: 0x%1 -> (%2)"))
                      .arg(value, 4, 16, QLatin1Char('0'))
                      .arg(valueText));

    return result;
}
