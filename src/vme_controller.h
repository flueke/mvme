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
#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include <QObject>
#include <system_error>
#include <type_traits>

#include "libmvme_export.h"

#include "globals.h"
#include "vme_error.h"
#include "vme.h"

enum class VMEControllerType
{
    VMUSB       = 1 << 0,
    SIS3153     = 1 << 1,
    MVLC_USB    = 1 << 2,
    MVLC_ETH    = 1 << 3,
};

inline constexpr VMEControllerType operator|(VMEControllerType lhs, VMEControllerType rhs)
{
    return static_cast<VMEControllerType>(
        static_cast<std::underlying_type<VMEControllerType>::type>(lhs) |
        static_cast<std::underlying_type<VMEControllerType>::type>(rhs));
}

inline constexpr VMEControllerType operator&(VMEControllerType lhs, VMEControllerType rhs)
{
    return static_cast<VMEControllerType>(
        static_cast<std::underlying_type<VMEControllerType>::type>(lhs) &
        static_cast<std::underlying_type<VMEControllerType>::type>(rhs));
}

inline constexpr bool operator!(VMEControllerType t)
{
    return static_cast<std::underlying_type<VMEControllerType>::type>(t) == 0;
}

static const constexpr VMEControllerType AllControllers = (
    VMEControllerType::VMUSB | VMEControllerType::SIS3153 |
    VMEControllerType::MVLC_USB | VMEControllerType::MVLC_ETH);

static const constexpr VMEControllerType MvlcControllers = (
    VMEControllerType::MVLC_USB | VMEControllerType::MVLC_ETH);

class LIBMVME_EXPORT VMEController: public QObject
{
    Q_OBJECT
    signals:
        void controllerOpened();
        void controllerClosed();
        void controllerStateChanged(ControllerState state);

    public:
        explicit VMEController(QObject *parent = 0);
        virtual ~VMEController() {}

        virtual VMEControllerType getType() const = 0;

        virtual VMEError write32(u32 address, u32 value, u8 amod) = 0;
        virtual VMEError write16(u32 address, u16 value, u8 amod) = 0;

        virtual VMEError read32(u32 address, u32 *value, u8 amod) = 0;
        virtual VMEError read16(u32 address, u16 *value, u8 amod) = 0;

        virtual VMEError blockRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo) = 0;

        virtual bool isOpen() const = 0;
        virtual VMEError open() = 0;
        virtual VMEError close() = 0;

        virtual ControllerState getState() const = 0;

        virtual QString getIdentifyingString() const = 0;
};

QString LIBMVME_EXPORT to_string(VMEControllerType type);
VMEControllerType LIBMVME_EXPORT from_string(const QString &str);

QString LIBMVME_EXPORT to_string(ControllerState state);

inline bool is_mvlc_controller(const VMEControllerType &type)
{
    return (type == VMEControllerType::MVLC_ETH
            || type == VMEControllerType::MVLC_USB);
}

inline bool is_mvlc_controller(const VMEController *ctrl)
{
    return ctrl ? is_mvlc_controller(ctrl->getType()) : false;
}

inline bool is_mvlc_controller(const QString &ctrlTypeName)
{
    return is_mvlc_controller(from_string(ctrlTypeName));
}

#endif // VMECONTROLLER_H
