#include "sis3153_util.h"

#include <QFormLayout>
#include <QPushButton>
#include <QTimer>

#include "sis3153.h"
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

SIS3153DebugWidget::SIS3153DebugWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    setWindowTitle(QSL("SIS3153 Debug Widget"));
    auto layout = new QFormLayout(this);

    for (const char *text: LabelTexts)
    {
        auto label = new QLabel;
        layout->addRow(text, label);
        m_labels.push_back(label);
    }

    auto resetButton = new QPushButton(QSL("Reset Counters"));
    layout->addRow(resetButton);
    connect(resetButton, &QPushButton::clicked, this, &SIS3153DebugWidget::resetCounters);

    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &SIS3153DebugWidget::refresh);
    refreshTimer->setInterval(1000);
    refreshTimer->start();

    refresh();
}

void SIS3153DebugWidget::refresh()
{
    auto sis = qobject_cast<SIS3153 *>(m_context->getVMEController());
    if (!sis) return;

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
