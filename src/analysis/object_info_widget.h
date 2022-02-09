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
#ifndef __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__
#define __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__

#include <QFrame>

#include "analysis_fwd.h"
#include "vme_config.h"
#include "analysis_service_provider.h"

class MVMEContext;

namespace analysis
{

class ObjectInfoWidget: public QFrame
{
    Q_OBJECT
    public:
        ObjectInfoWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~ObjectInfoWidget();

    public slots:
        void setAnalysisObject(const AnalysisObjectPtr &obj);
        void setVMEConfigObject(const ConfigObject *obj);
        void refresh();
        void clear();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__ */
