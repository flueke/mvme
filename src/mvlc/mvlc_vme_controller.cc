#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_error.h"

#include <QDebug>

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

    connect(m_mvlc, &MVLCObject::stackErrorNotification,
            this, &MVLC_VMEController::stackErrorNotification);

    connect(&m_notificationPoller, &MVLCNotificationPoller::stackErrorNotification,
            this, &MVLC_VMEController::stackErrorNotification);

    enableNotificationPolling();
}

void MVLC_VMEController::onMVLCStateChanged(const MVLCObject::State &oldState,
                                            const MVLCObject::State &newState)
{
    switch (newState)
    {
        case MVLCObject::Disconnected:
            emit controllerClosed();
            emit controllerStateChanged(ControllerState::Disconnected);
            break;

        case MVLCObject::Connected:
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

        case ConnectionType::UDP:
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
