#include "vme_controller_factory.h"

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
        auto obj  = std::make_unique<mesytec::mvlc::MVLCObject>(std::move(impl));
        auto ctrl = std::make_unique<mesytec::mvlc::MVLC_VMEController>(obj.get());

        // Transfer ownership of the MVLCObject to the newly created
        // MVLC_VMEController
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
                // TODO: support index and serial number based mvlc usb connetions
                auto impl = mesytec::mvlc::make_mvlc_usb();
                return make_mvlc_ctrl(std::move(impl));

            } break;

        case VMEControllerType::MVLC_ETH:
            {
                auto hostname = settings["hostname"].toString();
                auto impl = mesytec::mvlc::make_mvlc_udp(hostname.toStdString().c_str());
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
    }

    return nullptr;
}
