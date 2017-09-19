#include "vme_controller_factory.h"

#include "sis3153.h"
#include "sis3153_readout_worker.h"
#include "vme_controller_ui.h"
#include "vme_controller_ui_p.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"

//
// VMEControllerFactory
//
VMEControllerFactory::VMEControllerFactory(VMEControllerType type)
    : m_type(type)
{
}

VMEController *VMEControllerFactory::makeController(const QVariantMap &settings)
{
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
                //return new SIS3153ReadoutWorkerIRQPolling;
                return new SIS3153ReadoutWorker;
            } break;
    }

    return nullptr;
}
