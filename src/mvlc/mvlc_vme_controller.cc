#include "mvlc/mvlc_vme_controller.h"

#include <QDebug>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_util.h"


namespace
{

using namespace mesytec::mvlc;

// Checks for certain MVLCErrorCode values and returns a VMEError containing
// additional information if applicable. Otherwise a VMEError object
// constructed from the given error_code is returned.
VMEError error_wrap(const MVLCObject &mvlc, const std::error_code &ec)
{
    if (ec == MVLCErrorCode::InvalidBufferHeader ||
        ec == MVLCErrorCode::UnexpectedBufferHeader)
    {
        auto buffer = mvlc.getResponseBuffer();

        if (!buffer.isEmpty())
        {
            QStringList strings;

            for (u32 word: mvlc.getResponseBuffer())
            {
                strings.append(QString("0x%1").arg(word, 8, 16, QLatin1Char('0')));
            }

            QString msg(ec.message().c_str());
            msg += ": " + strings.join(", ");

            return VMEError(VMEError::CommError, msg);
        }
    }

    return ec;
}

} // end anon namespace

namespace mesytec
{
namespace mvlc
{

MVLC_VMEController::MVLC_VMEController(MVLCObject *mvlc, QObject *parent)
    : VMEController(parent)
    , m_mvlc(mvlc)
    , m_notificationPoller(*mvlc)
{
    assert(m_mvlc);

    connect(m_mvlc, &MVLCObject::stateChanged,
            this, &MVLC_VMEController::onMVLCStateChanged);

#if 0
    // XXX: Debug
    connect(this, &MVLC_VMEController::stackErrorNotification,
            [] (const QVector<u32> &data)
    {
        if (data.isEmpty())
        {
            qDebug("%s - Empty notification!", __PRETTY_FUNCTION__);
            assert(false);
            return;
        }

        if (data.size() != 2)
        {
            qDebug("%s - Notification size != 2: %d",
                   __PRETTY_FUNCTION__, data.size());
        }

        auto frameInfo = extract_frame_info(data[0]);

        qDebug("%s - MVLC_VMEController polled a stack error notification:"
               "header=0x%08x, data[1]=0x%08x, len=%u, stack=%u, flags=0x%02x",
               QDateTime::currentDateTime().toString().toStdString().c_str(),
               data[0],
               data[1],
               frameInfo.len,
               frameInfo.stack,
               frameInfo.flags);
    });
#endif

    auto debug_print_stack_error_counters = [this] ()
    {
        qDebug("Stack Error Info Dump:");

        auto errorCounters = m_mvlc->getStackErrorCounters();

        for (size_t stackId = 0; stackId < errorCounters.stackErrors.size(); ++stackId)
        {
            const auto &errorInfoCounts = errorCounters.stackErrors[stackId];

            if (errorInfoCounts.empty())
                continue;


            for (auto it = errorInfoCounts.begin();
                 it != errorInfoCounts.end();
                 it++)
            {
                const auto &errorInfo = it->first;
                const auto &count = it->second;

                qDebug("  stackId=%lu, line=%u, flags=0x%02x, count=%lu",
                       stackId,
                       errorInfo.line,
                       errorInfo.flags,
                       count
                       );
            }
        }

        if (errorCounters.nonErrorFrames)
            qDebug("nonErrorFrames=%lu", errorCounters.nonErrorFrames);

        for (auto it=errorCounters.nonErrorHeaderCounts.begin();
             it!=errorCounters.nonErrorHeaderCounts.end();
             ++it)
        {
            u32 header = it->first;
            size_t count = it->second;

            qDebug("  0x%08x: %lu", header, count);
        }

        for (const auto &frameCopy: errorCounters.framesCopies)
        {
            qDebug("copy of a frame recevied via polling:");
            for (u32 word: frameCopy)
            {
                qDebug("  0x%08x", word);
            }
            qDebug("----");
        }
    };

    auto dumpTimer = new QTimer(this);
    connect(dumpTimer, &QTimer::timeout, this, debug_print_stack_error_counters);
    dumpTimer->setInterval(1000);
    dumpTimer->start();
}

void MVLC_VMEController::onMVLCStateChanged(const MVLCObject::State &oldState,
                                            const MVLCObject::State &newState)
{
    switch (newState)
    {
        case MVLCObject::Disconnected:
            m_notificationPoller.disablePolling();
            emit controllerClosed();
            emit controllerStateChanged(ControllerState::Disconnected);
            break;

        case MVLCObject::Connected:
            m_notificationPoller.enablePolling();
            emit controllerOpened();
            emit controllerStateChanged(ControllerState::Connected);
            break;

        case MVLCObject::Connecting:
            emit controllerStateChanged(ControllerState::Connecting);
            break;
    }
}

bool MVLC_VMEController::isOpen() const
{
    return m_mvlc->isConnected();
}

VMEError MVLC_VMEController::open()
{
    return m_mvlc->connect();
}

VMEError MVLC_VMEController::close()
{
    return m_mvlc->disconnect();
}

ControllerState MVLC_VMEController::getState() const
{
    switch (m_mvlc->getState())
    {
        case MVLCObject::Disconnected:
            return ControllerState::Disconnected;

        case MVLCObject::Connecting:
            return ControllerState::Connecting;

        case MVLCObject::Connected:
            return ControllerState::Connected;
    }

    return ControllerState::Disconnected;
}

QString MVLC_VMEController::getIdentifyingString() const
{
    switch (getType())
    {
        case VMEControllerType::MVLC_USB:
            return "MVLC_USB";

        case VMEControllerType::MVLC_ETH:
            return "MVLC_ETH";

        default:
            break;
    }

    assert(!"invalid vme controller type");
    return "<invalid_vme_controller_type>";
}

VMEControllerType MVLC_VMEController::getType() const
{
    switch (m_mvlc->connectionType())
    {
        case ConnectionType::USB:
            return VMEControllerType::MVLC_USB;

        case ConnectionType::ETH:
            return VMEControllerType::MVLC_ETH;
    }

    InvalidCodePath;
    return VMEControllerType::MVLC_USB;
}

VMEError MVLC_VMEController::write32(u32 address, u32 value, u8 amod)
{
    auto ec = m_mvlc->vmeSingleWrite(address, value, amod, VMEDataWidth::D32);
    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::write16(u32 address, u16 value, u8 amod)
{
    auto ec = m_mvlc->vmeSingleWrite(address, value, amod, VMEDataWidth::D16);
    return error_wrap(*m_mvlc, ec);
}


VMEError MVLC_VMEController::read32(u32 address, u32 *value, u8 amod)
{
    auto ec = m_mvlc->vmeSingleRead(address, *value, amod, VMEDataWidth::D32);
    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::read16(u32 address, u16 *value, u8 amod)
{
    u32 tmpVal = 0u;
    auto ec = m_mvlc->vmeSingleRead(address, tmpVal, amod, VMEDataWidth::D16);
    *value = tmpVal;
    return error_wrap(*m_mvlc, ec);
}


VMEError MVLC_VMEController::blockRead(u32 address, u32 transfers,
                                       QVector<u32> *dest, u8 amod, bool fifo)
{
    auto ec = m_mvlc->vmeBlockRead(address, amod, transfers, *dest);
    return error_wrap(*m_mvlc, ec);
}

} // end namespace mvlc
} // end namespace mesytec
