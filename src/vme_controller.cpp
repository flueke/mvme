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
#include "vme_controller.h"
#include <QThread>

QString to_string(VMEControllerType type)
{
    switch (type)
    {
        case VMEControllerType::VMUSB:
            return "VMUSB";
        case VMEControllerType::SIS3153:
            return "SIS3153";
        case VMEControllerType::MVLC_USB:
            return "MVLC_USB";
        case VMEControllerType::MVLC_ETH:
            return "MVLC_ETH";
    }

    InvalidCodePath;
    return QString();
}

VMEControllerType from_string(const QString &str)
{
    if (str == QSL("VMUSB"))
        return VMEControllerType::VMUSB;

    if (str == QSL("SIS3153"))
        return VMEControllerType::SIS3153;

    if (str == QSL("MVLC_USB"))
        return VMEControllerType::MVLC_USB;

    if (str == QSL("MVLC_ETH"))
        return VMEControllerType::MVLC_ETH;

    return VMEControllerType::MVLC_ETH;
}

QString to_string(ControllerState state)
{
    switch (state)
    {
        case ControllerState::Connected:
            return QSL("Connected");
        case ControllerState::Disconnected:
            return QSL("Disconnected");
        case ControllerState::Connecting:
            return QSL("Connecting");
    }

    InvalidCodePath;
    return QString();
}

static const QMap<VMEError::ErrorType, QString> errorNames =
{
    { VMEError::NoError,                QSL("No error") },
    { VMEError::UnknownError,           QSL("Unknown error") },
    { VMEError::NotOpen,                QSL("Controller not open") },
    { VMEError::WriteError,             QSL("Write error") },
    { VMEError::ReadError,              QSL("Read error") },
    { VMEError::CommError,              QSL("Communication error") },
    { VMEError::BusError,               QSL("VME Bus Error") },
    { VMEError::NoDevice,               QSL("No device found") },
    { VMEError::DeviceIsOpen,           QSL("Device is open") },
    { VMEError::Timeout,                QSL("Timeout") },
    { VMEError::HostNotFound,           QSL("Host not found") },
    { VMEError::InvalidIPAddress,       QSL("Invalid IP address") },
    { VMEError::UnexpectedAddressMode,  QSL("Unexpected address mode") },
    { VMEError::HostLookupFailed,       QSL("Host lookup failed") },
    { VMEError::WrongControllerType,    QSL("Wrong VME controller type") },
    { VMEError::StdErrorCode,           QSL("std::error_code") },
    { VMEError::UnsupportedCommand,     QSL("Unsupported command") },
};

QString VMEError::toString() const
{
    if (error() == VMEError::UnknownError && !m_message.isEmpty())
        return m_message;

    if (auto ec = getStdErrorCode())
    {
        return QString(ec.message().c_str());
    }

    QString result(errorName());

    if (!m_message.isEmpty())
        result += QSL(": ") + m_message;

    if (m_errorCode != 0)
        result += QString("(code=%1)").arg(m_errorCode);

    return result;
}

QString VMEError::errorName() const
{
    return VMEError::errorName(error());
}

QString VMEError::errorName(ErrorType type)
{
    return errorNames.value(type, QSL("Unknown error"));
}

VMEController::VMEController(QObject *parent)
    : QObject(parent)
{}
