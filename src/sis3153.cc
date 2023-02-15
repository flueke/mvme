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

#include "sis3153.h"

#include <QDebug>
#include <QHostInfo>
#include <QMutex>
#include <QThread>

#include "sis3153/sis3153ETH_vme_class.h"
#include "vme.h"

#define SIS3153_DEBUG

/* SIS3153 implementation notes
 * ===========================================================================
 * Ansonsten sind die Listen Ackn. folgendermaßen definert
    0x50:	Not Last packet List 1
    0x58:	Last packet List 1
    Oder umgehrt

    0x51:	Not Last packet List 2
    0x59:	Last packet List 2
    ...
    0x57:	Not Last packet List 8
    0x5F:	Last packet List 8


 * Receive errors und queue schaue ich mit folgendem Befehl an:
    watch -n1 'netstat -anup; echo; netstat -anus'

 * Die Events werden in einem Multievent-packet folgender maßen "eingepackt":
    3 Bytes Multievent Header:
	  0x60 0x00 0x00
    Dann 4 Bytes Single Event Header (anstelle von 3Byte):
	  0x5x   "upper-length-byte"   "lower-length-byte"   "status"
 */

static const QMap<u32, const char *> RegisterNames =
{
    { SIS3153Registers::USBControlAndStatus,        "USB Control/Status" },
    { SIS3153Registers::ModuleIdAndFirmware,        "Module ID/Firmware Version" },
    { SIS3153Registers::SerialNumber,               "Serial Number" },
    { SIS3153Registers::LemoIOControl,              "LEMO IO Control" },
    { SIS3153Registers::UDPConfiguration,           "UDP Configuration" },

    { SIS3153Registers::StackListConfig1,           "StackListConfig1" },
    { SIS3153Registers::StackListTrigger1,          "StackListTrigger1" },
    { SIS3153Registers::StackListConfig2,           "StackListConfig2" },
    { SIS3153Registers::StackListTrigger2,          "StackListTrigger2" },
    { SIS3153Registers::StackListConfig3,           "StackListConfig3" },
    { SIS3153Registers::StackListTrigger3,          "StackListTrigger3" },
    { SIS3153Registers::StackListConfig4,           "StackListConfig4" },
    { SIS3153Registers::StackListTrigger4,          "StackListTrigger4" },
    { SIS3153Registers::StackListConfig5,           "StackListConfig5" },
    { SIS3153Registers::StackListTrigger5,          "StackListTrigger5" },
    { SIS3153Registers::StackListConfig6,           "StackListConfig6" },
    { SIS3153Registers::StackListTrigger6,          "StackListTrigger6" },
    { SIS3153Registers::StackListConfig7,           "StackListConfig7" },
    { SIS3153Registers::StackListTrigger7,          "StackListTrigger7" },
    { SIS3153Registers::StackListConfig8,           "StackListConfig8" },
    { SIS3153Registers::StackListTrigger8,          "StackListTrigger8" },

    { SIS3153Registers::StackListTimer1Config,      "StackListTimer1Config" },
    { SIS3153Registers::StackListTimer2Config,      "StackListTimer2Config" },
    { SIS3153Registers::StackListDynSizedBlockRead, "StackListDynSizedBlockRead" }
};

static const int SocketReceiveBufferSize = Megabytes(4);

VMEError make_sis_error(int sisCode)
{
    switch (sisCode)
    {
        case 0:
            break;

        case PROTOCOL_ERROR_CODE_TIMEOUT:
            return VMEError(VMEError::Timeout);

        case PROTOCOL_VME_CODE_BUS_ERROR:
            return VMEError(VMEError::BusError);

        default:
            return VMEError(VMEError::CommError, sisCode);
    }

    return VMEError();
}

struct SIS3153Private
{
    explicit SIS3153Private(SIS3153 *q)
        : q(q)
        , sis(new sis3153eth)
        , sis_ctrl(new sis3153eth)
    {
    }

    ~SIS3153Private()
    {
    }

    SIS3153 *q;
    QMutex lock;
    std::unique_ptr<sis3153eth> sis;            // socket used for readout and non DAQ mode communication
    std::unique_ptr<sis3153eth> sis_ctrl;       // socket used to leave DAQ mode

    // ip address or hostname of the SIS
    QString address;
    ControllerState ctrlState = ControllerState::Disconnected;

    // shadow registers
    u32 moduleIdAndFirmware;
    u32 serialNumber;

    bool m_resetOnConnect = false;

    /*
    u32 lemoIOControl;
    u32 udpConfig;
    u32 vmeMasterControlAndStatus;
    u32 vmeMasterCycleStatus;
    u32 vmeInterruptStatus;
    */

    inline VMEError readRegisterImpl(u32 address, u32 *outValue)
    {
        int resultCode = sis->udp_sis3153_register_read(address, outValue);
        auto result = make_sis_error(resultCode);
        return result;
    }

    inline VMEError writeRegisterImpl(u32 address, u32 value)
    {
        int resultCode = sis->udp_sis3153_register_write(address, value);
        auto result = make_sis_error(resultCode);
        return result;
    }
};

SIS3153::SIS3153(QObject *parent)
    : VMEController(parent)
    , m_d(new SIS3153Private(this))
{
}

SIS3153::~SIS3153()
{
    close();
    delete m_d;
}

VMEControllerType SIS3153::getType() const
{
    return VMEControllerType::SIS3153;
}

bool SIS3153::isOpen() const
{
    return m_d->ctrlState == ControllerState::Connected;
}

static const int ReceiveTimeout_ms = 100;

VMEError SIS3153::open()
{
    /* As UDP is stateless this code tries to connect to the currently set
     * address/hostname and read the firmware and serial number registers. If
     * the register reads succeed we are "connected". */

    QMutexLocker locker(&m_d->lock);

    if (isOpen())
        return VMEError(VMEError::DeviceIsOpen);

    m_d->ctrlState = ControllerState::Connecting;
    emit controllerStateChanged(m_d->ctrlState);

    /* This is a bit of a hack but the easiest thing to do right now: Once a
     * sis3153eth instance is disconnected it never sets the socket parameters
     * correctly again. Instead of touching that code I'll just recreate new
     * sis3153eth instances here and this way I'll always have properly setup
     * sockets to work with. */

    m_d->sis.reset(new sis3153eth);
    m_d->sis_ctrl.reset(new sis3153eth);

    // set custom timeout and custom receive buffer size
    for (auto sis: { m_d->sis.get(), m_d->sis_ctrl.get()})
    {
        sis->recv_timeout_sec = 0;
        sis->recv_timeout_usec = ReceiveTimeout_ms * 1000;
        sis->set_UdpSocketOptionTimeout();
        sis->set_UdpSocketOptionBufSize(SocketReceiveBufferSize);
    }


    /* Resolve the hostname here using the Qt layer and pass an IPv4 address string to
     * set_UdpSocketSIS3153_IpAddress(). This method internally uses gethostbyname() which
     * when given an IP-address just copies the argument to its result structure. */

    auto hostInfo = QHostInfo::fromName(m_d->address);

    if (hostInfo.error() != QHostInfo::NoError)
    {
        return VMEError(VMEError::HostLookupFailed, hostInfo.errorString());
    }

    auto addresses = hostInfo.addresses();

    if (addresses.isEmpty())
    {
        return VMEError(VMEError::HostNotFound);
    }

    auto addressData = addresses.first().toString().toLocal8Bit();


    // set ip address / hostname
    //auto addressData = m_d->address.toLocal8Bit();

#ifdef SIS3153_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "addressData =" << addressData;
#endif


    auto sis_setAddress_result_to_error = [](int resultCode)
    {
        VMEError result;

        // -2: gethostbyname() returned nullptr
        // -1: empty ip address string or broadcast address (0xf..f) given
        // -3: 0.0.0.0 or 255.255.255.255 given
        switch (resultCode)
        {
            case 0:
                break;
            case -1:
            case -3:
                result = VMEError(VMEError::InvalidIPAddress);
                break;
            case -2:
                result = VMEError(VMEError::HostNotFound);
                break;
            default:
                result = VMEError(VMEError::UnknownError);
                break;
        }

        return result;
    };

    VMEError result;

    int resultCode = m_d->sis->set_UdpSocketSIS3153_IpAddress(const_cast<char *>(addressData.constData()));

#ifdef SIS3153_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "result from set_UdpSocketSIS3153_IpAddress (main socket)" << resultCode;
#endif

    result = sis_setAddress_result_to_error(resultCode);

    if (result.isError())
    {
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerStateChanged(m_d->ctrlState);
        return result;
    }

    // create a second socket for issuing control requests while in daq mode
    resultCode = m_d->sis_ctrl->set_UdpSocketSIS3153_IpAddress(const_cast<char *>(addressData.constData()));
    result = sis_setAddress_result_to_error(resultCode);

    if (result.isError())
    {
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerStateChanged(m_d->ctrlState);
        return result;
    }

#ifdef SIS3153_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "set_UdpSocketSIS3153_IpAddress() ok:" << m_d->address;
#endif

    {
        static const int PostResetDelay_ms = 250;
        static const int ReconnectRetryLimit = 3;
        int retryCount = 0;
        do
        {
            /* This loop tries to get the sis into a good state by sending the
             * udp_reset_cmd if m_resetOnConnect is set. */

            char msgBuf[1024];
            u32 numDevices = 0;
            result = make_sis_error(m_d->sis->get_vmeopen_messages(msgBuf, sizeof(msgBuf), &numDevices));

            if (result.isTimeout() && m_d->m_resetOnConnect)
            {
#ifdef SIS3153_DEBUG
                qDebug() << "sis connect timed out. sending udp_reset_cmd and delaying for" << PostResetDelay_ms << "ms";
#endif
                m_d->sis->udp_reset_cmd();
                QThread::msleep(PostResetDelay_ms);
            }
        } while (result.isTimeout() && ++retryCount < ReconnectRetryLimit);

        if (result.isError())
        {
#ifdef SIS3153_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "get_vmeopen_messages:" << result.toString();
#endif
            m_d->ctrlState = ControllerState::Disconnected;
            m_d->m_resetOnConnect = false; // reset the resetOnConnect flag as we tried connecting but failed
            emit controllerStateChanged(m_d->ctrlState);
            return result;
        }

        u32 controlReg = 0;
        resultCode     = m_d->sis->udp_sis3153_register_read(SIS3153Registers::StackListControl, &controlReg);
        result         = make_sis_error(resultCode);

        if (result.isError())
        {
            m_d->ctrlState = ControllerState::Disconnected;
            m_d->m_resetOnConnect = false; // reset the resetOnConnect flag as we tried connecting but failed
            emit controllerStateChanged(m_d->ctrlState);
            return result;
        }

        if (controlReg & SIS3153Registers::StackListControlValues::StackListEnable)
        {
            if (m_d->m_resetOnConnect)
            {
                m_d->m_resetOnConnect = false;
                m_d->sis->udp_reset_cmd();
                QThread::msleep(PostResetDelay_ms);

                controlReg     = 0;
                resultCode     = m_d->sis->udp_sis3153_register_read(SIS3153Registers::StackListControl, &controlReg);
                result         = make_sis_error(resultCode);

                if (result.isError())
                {
                    result.setMessage(QSL("Error re-reading StackListControl register after controller reset."));
                    m_d->ctrlState = ControllerState::Disconnected;
                    emit controllerStateChanged(m_d->ctrlState);
                    return result;
                }

                if (controlReg & SIS3153Registers::StackListControlValues::StackListEnable)
                {
                    result = VMEError(VMEError::DeviceIsOpen,
                                      QSL("SIS3153 remains in autonomous DAQ mode after controller reset."
                                          "A manual reset/powercycle is needed."));
                    m_d->ctrlState = ControllerState::Disconnected;
                    emit controllerStateChanged(m_d->ctrlState);
                    return result;
                }
            }
            else
            {
                result = VMEError(VMEError::DeviceIsOpen,
                                  QSL("SIS3153 is in autonomous DAQ mode (another instance of mvme might be controlling it)."
                                      " Use \"force reset\" to attempt to reset the controller."));
                m_d->ctrlState = ControllerState::Disconnected;
                emit controllerStateChanged(m_d->ctrlState);
                return result;
            }
        }
    }

    // read module id and firmware
    resultCode = m_d->sis->udp_sis3153_register_read(
        SIS3153Registers::ModuleIdAndFirmware, &m_d->moduleIdAndFirmware);
    result = make_sis_error(resultCode);

    if (result.isError())
    {
#ifdef SIS3153_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "read ModuleIdAndFirmware:" << result.toString();
#endif
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerStateChanged(m_d->ctrlState);
        return result;
    }

    // read serial number
    resultCode = m_d->sis->udp_sis3153_register_read(
        SIS3153Registers::SerialNumber, &m_d->serialNumber);
    result = make_sis_error(resultCode);

    if (result.isError())
    {
#ifdef SIS3153_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "read SerialNumber:" << result.toString();
#endif
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerStateChanged(m_d->ctrlState);
        return result;
    }

    // write KeyResetAll
    result = m_d->writeRegisterImpl(SIS3153Registers::KeyResetAll, 1);
    if (result.isError())
    {
#ifdef SIS3153_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "write KeyResetAll:" << result.toString();
#endif
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerStateChanged(m_d->ctrlState);
        return result;
    }

    qDebug("%s: opened SIS3153 %04u, moduleIdAndFirmware=0x%08x",
           __PRETTY_FUNCTION__, m_d->serialNumber, m_d->moduleIdAndFirmware);

    m_d->ctrlState = ControllerState::Connected;
    emit controllerOpened();
    emit controllerStateChanged(ControllerState::Connected);

    return result;
}

VMEError SIS3153::close()
{
    QMutexLocker locker(&m_d->lock);
    if (isOpen())
    {
        // close the sockets
        m_d->sis->vmeclose();
        m_d->sis_ctrl->vmeclose();
        m_d->ctrlState = ControllerState::Disconnected;
        emit controllerClosed();
        emit controllerStateChanged(m_d->ctrlState);
    }
    return VMEError();
}

ControllerState SIS3153::getState() const
{
    return m_d->ctrlState;
}

QString SIS3153::getIdentifyingString() const
{
    if (isOpen())
    {
        return QString("SIS3153-%1 (address=%2)")
            .arg(m_d->serialNumber, 4, 10, QLatin1Char('0'))
            .arg(m_d->address)
            ;
    }

    return QString("SIS3153 (address=%1)")
        .arg(m_d->address)
        ;
}

VMEError SIS3153::write32(u32 address, u32 value, u8 amod)
{
    QMutexLocker locker(&m_d->lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    VMEError result;

    switch (amod)
    {
        case vme_address_modes::a32UserData:
        case vme_address_modes::a32PrivData:
            result = make_sis_error(m_d->sis->vme_A32D32_write(address, value));
            break;

        case vme_address_modes::a24UserData:
        case vme_address_modes::a24PrivData:
            result = make_sis_error(m_d->sis->vme_A24D32_write(address, value));
            break;

        case vme_address_modes::a16User:
        case vme_address_modes::a16Priv:
            result = make_sis_error(m_d->sis->vme_A16D32_write(address, value));
            break;

        default:
            result = VMEError(VMEError::UnexpectedAddressMode);
            break;
    }

    return result;
}

VMEError SIS3153::write16(u32 address, u16 value, u8 amod)
{
    QMutexLocker locker(&m_d->lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    VMEError result;

    switch (amod)
    {
        case vme_address_modes::a32UserData:
        case vme_address_modes::a32PrivData:
            result = make_sis_error(m_d->sis->vme_A32D16_write(address, value));
            break;

        case vme_address_modes::a24UserData:
        case vme_address_modes::a24PrivData:
            result = make_sis_error(m_d->sis->vme_A24D16_write(address, value));
            break;

        case vme_address_modes::a16User:
        case vme_address_modes::a16Priv:
            result = make_sis_error(m_d->sis->vme_A16D16_write(address, value));
            break;

        default:
            result = VMEError(VMEError::UnexpectedAddressMode);
            break;
    }

    return result;
}

VMEError SIS3153::read32(u32 address, u32 *value, u8 amod)
{
    QMutexLocker locker(&m_d->lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    VMEError result;

    switch (amod)
    {
        case vme_address_modes::a32UserData:
        case vme_address_modes::a32PrivData:
            result = make_sis_error(m_d->sis->vme_A32D32_read(address, value));
            break;

        case vme_address_modes::a24UserData:
        case vme_address_modes::a24PrivData:
            result = make_sis_error(m_d->sis->vme_A24D32_read(address, value));
            break;

        case vme_address_modes::a16User:
        case vme_address_modes::a16Priv:
            result = make_sis_error(m_d->sis->vme_A16D32_read(address, value));
            break;

        default:
            result = VMEError(VMEError::UnexpectedAddressMode);
            break;
    }

    return result;
}

VMEError SIS3153::read16(u32 address, u16 *value, u8 amod)
{
    QMutexLocker locker(&m_d->lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    VMEError result;

    switch (amod)
    {
        case vme_address_modes::a32UserData:
        case vme_address_modes::a32PrivData:
            result = make_sis_error(m_d->sis->vme_A32D16_read(address, value));
            break;

        case vme_address_modes::a24UserData:
        case vme_address_modes::a24PrivData:
            result = make_sis_error(m_d->sis->vme_A24D16_read(address, value));
            break;

        case vme_address_modes::a16User:
        case vme_address_modes::a16Priv:
            result = make_sis_error(m_d->sis->vme_A16D16_read(address, value));
            break;

        default:
            result = VMEError(VMEError::UnexpectedAddressMode);
            break;
    }

    return result;
}

static inline void fix_word_order(QVector<u32> &vec)
{
    const s32 maxIdx = vec.size() - 1;

    for (s32 idx=0;
         idx < maxIdx;
         idx += 2)
    {
        std::swap(vec[idx], vec[idx+1]);
    }
}

VMEError SIS3153::blockRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo)
{
    QMutexLocker locker(&m_d->lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    int resultCode = 0;
    bool isMBLT = vme_address_modes::is_mblt_mode(amod);
    u32 wordsRead = 0; // sis3153 fills in the number of 32-bit words received

    dest->resize(isMBLT ? transfers * 2 : transfers);

    if (fifo)
    {
        if (isMBLT)
            resultCode = m_d->sis->vme_A32MBLT64FIFO_read(address, dest->data(), transfers, &wordsRead);
        else
            resultCode = m_d->sis->vme_A32BLT32FIFO_read(address, dest->data(), transfers, &wordsRead);
    }
    else
    {
        if (isMBLT)
            resultCode = m_d->sis->vme_A32MBLT64_read(address, dest->data(), transfers, &wordsRead);
        else
            resultCode = m_d->sis->vme_A32BLT32_read(address, dest->data(), transfers, &wordsRead);
    }

#ifdef SIS3153_DEBUG
    qDebug() << __PRETTY_FUNCTION__
        << ": address =" << QString::number(address, 16)
        << ", transfers =" << transfers
        << ", dest->size() =" << dest->size()
        << ", amod =" << QString::number((u32)amod, 16)
        << ", fifo =" << fifo
        << ", wordsRead =" << wordsRead
        ;
#endif

    dest->resize(wordsRead);

    if (isMBLT)
    {
        fix_word_order(*dest);
    }

    /* Handle the case where sis3153 returns PROTOCOL_VME_CODE_BUS_ERROR but we
     * did receive data. I'm not sure if this is always safe to do. */
    if (wordsRead > 0)
    {
        resultCode = 0;
    }

    return make_sis_error(resultCode);
}

sis3153eth *SIS3153::getImpl()
{
    return m_d->sis.get();
}

sis3153eth *SIS3153::getCtrlImpl()
{
    return m_d->sis_ctrl.get();
}

void SIS3153::setAddress(const QString &address)
{
    m_d->address = address;
}

QString SIS3153::getAddress() const
{
    return m_d->address;
}

VMEError SIS3153::readRegister(u32 address, u32 *outValue)
{
    QMutexLocker locker(&m_d->lock);
    return m_d->readRegisterImpl(address, outValue);
}

VMEError SIS3153::writeRegister(u32 address, u32 value)
{
    QMutexLocker locker(&m_d->lock);
    return m_d->writeRegisterImpl(address, value);
}

void SIS3153::setResetOnConnect(bool sendReset)
{
    m_d->m_resetOnConnect = sendReset;
}

bool SIS3153::doesResetOnConnect() const
{
    return m_d->m_resetOnConnect;
}

void dump_registers(SIS3153 *sis, std::function<void (const QString &)> dumper)
{
    dumper(QSL("Begin SIS3153 register dump"));

    for (u32 addr: RegisterNames.keys())
    {
        u32 value = 0;
        auto error = sis->readRegister(addr, &value);
        auto name  = RegisterNames.value(addr, "Unknown register");

        if (error.isError())
        {
            dumper(QString("  Error reading register (%1, 0x%2, %3)")
                   .arg(addr)
                   .arg(addr, 2, 16, QLatin1Char('0'))
                   .arg(name));
        }
        else if (value != 0)
        {
            dumper(QString("  0x%1 = 0x%2 (%3)")
                   .arg(addr, 8, 16, QLatin1Char('0'))
                   .arg(value, 8, 16, QLatin1Char('0'))
                   .arg(name));
        }
    }
}
