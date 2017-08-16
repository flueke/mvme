#ifndef __VME_CONTROLLER_FACTORY_H__
#define __VME_CONTROLLER_FACTORY_H__

#include "vme_controller.h"
#include "vme_controller_ui.h"
#include "vme_readout_worker.h"

class VMEControllerFactory
{
    public:
        VMEControllerFactory(VMEControllerType type);

        VMEController *makeController(const QVariantMap &settings);
        VMEControllerSettingsWidget *makeSettingsWidget();
        VMEReadoutWorker *makeReadoutWorker();

    private:
        VMEControllerType m_type;
};

#endif /* __VME_CONTROLLER_FACTORY_H__ */
