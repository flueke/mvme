/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#ifndef __VME_DAQ_H__
#define __VME_DAQ_H__

#include "vme_config.h"
#include "vme_controller.h"
#include "vme_readout_worker.h"

/* Both functions throw on error:
 * QString, std::runtime_error, vme_script::ParseError
 */

void vme_daq_init(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger);

void vme_daq_shutdown(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger);

vme_script::VMEScript build_event_readout_script(EventConfig *eventConfig);

struct DAQReadoutListfileHelperPrivate;

class DAQReadoutListfileHelper
{
    public:
        DAQReadoutListfileHelper(VMEReadoutWorkerContext readoutContext);
        ~DAQReadoutListfileHelper();

        void beginRun();
        void endRun();
        void writeBuffer(DataBuffer *buffer);
        void writeBuffer(const u8 *buffer, size_t size);
        void writeTimetickSection();

    private:
        std::unique_ptr<DAQReadoutListfileHelperPrivate> m_d;
        VMEReadoutWorkerContext m_readoutContext;
};

#endif /* __VME_DAQ_H__ */
