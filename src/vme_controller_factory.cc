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
#include "vme_controller_factory.h"

#include "mvlc_listfile_worker.h"
#include "mvlc_readout_worker.h"
#include "mvme_listfile_worker.h"
#include "sis3153.h"
#include "sis3153_readout_worker.h"
#include "vme_controller_ui.h"
#include "vme_controller_ui_p.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"

#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_vme_controller.h"

//
// VMEControllerFactory
//
VMEControllerFactory::VMEControllerFactory(VMEControllerType type)
    : m_type(type)
{
}

VMEController *VMEControllerFactory::makeController(const QVariantMap &settings)
{
    auto make_mvlc_ctrl = [](std::unique_ptr<mesytec::mvlc::AbstractImpl> impl)
    {
        // Dependencies of the MVLC_VMEController object:
        // MVLC_VMEController -> MVLCObject -> (eth|usb)::Impl
        //
        // Note that the ownership transfer is not required by the API itself.
        // It's only done to have a valid parent for MVLCObject and not leak anything.
        // Constructing a MVLC_VMEController on the stack using a MVLCObject is
        // fine without transfering ownership.

        auto obj  = std::make_unique<mesytec::mvlc::MVLCObject>(std::move(impl));
        auto ctrl = std::make_unique<mesytec::mvlc::MVLC_VMEController>(obj.get());

        // Transfer ownership of the MVLCObject to the newly created
        // MVLC_VMEController wrapper
        obj->setParent(ctrl.get());
        obj.release();

        return ctrl.release();
    };

    switch (m_type)
    {
        case VMEControllerType::VMUSB:
            {
                return new VMUSB;
            } break;

        case VMEControllerType::SIS3153:
            {
                auto result = new SIS3153;
                result->setAddress(settings["hostname"].toString());
                return result;
            } break;

        case VMEControllerType::MVLC_USB:
            {
                std::unique_ptr<mesytec::mvlc::AbstractImpl> impl;

                auto method = settings["method"].toString();

                if (method == "by_index")
                {
                    impl = mesytec::mvlc::make_mvlc_usb(settings["index"].toUInt());
                }
                else if (method == "by_serial")
                {
                    impl = mesytec::mvlc::make_mvlc_usb_using_serial(
                        settings["serial"].toString().toStdString());
                }
                else
                {
                    impl = mesytec::mvlc::make_mvlc_usb();
                }

                return make_mvlc_ctrl(std::move(impl));

            } break;

        case VMEControllerType::MVLC_ETH:
            {
                auto hostname = settings["mvlc_hostname"].toString();
                auto impl = mesytec::mvlc::make_mvlc_eth(hostname.toStdString().c_str());
                return make_mvlc_ctrl(std::move(impl));
            } break;
    }

    return nullptr;
}

VMEControllerSettingsWidget *VMEControllerFactory::makeSettingsWidget()
{
    switch (m_type)
    {
        case VMEControllerType::VMUSB:
            {
                return new VMUSBSettingsWidget;
            } break;

        case VMEControllerType::SIS3153:
            {
                return new SIS3153EthSettingsWidget;
            } break;

        case VMEControllerType::MVLC_USB:
            return new MVLC_USB_SettingsWidget;

        case VMEControllerType::MVLC_ETH:
            return new MVLC_ETH_SettingsWidget;
    }

    return nullptr;
}

VMEReadoutWorker *VMEControllerFactory::makeReadoutWorker()
{
    switch (m_type)
    {
        case VMEControllerType::VMUSB:
            {
                return new VMUSBReadoutWorker;
            } break;

        case VMEControllerType::SIS3153:
            {
                return new SIS3153ReadoutWorker;
            } break;

        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            return new MVLCReadoutWorker;
    }

    return nullptr;
}

ListfileReplayWorker *VMEControllerFactory::makeReplayWorker(
    ThreadSafeDataBufferQueue *emptyBuffers,
    ThreadSafeDataBufferQueue *filledBuffers)
{
    switch (m_type)
    {
        case VMEControllerType::VMUSB:
        case VMEControllerType::SIS3153:
            return new MVMEListfileWorker(emptyBuffers, filledBuffers);

        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            return new MVLCListfileWorker(emptyBuffers, filledBuffers);
    }

    return nullptr;
}