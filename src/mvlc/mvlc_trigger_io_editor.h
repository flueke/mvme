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
#ifndef __MVME_MVLC_TRIGGER_IO_EDITOR_H__
#define __MVME_MVLC_TRIGGER_IO_EDITOR_H__

#include <memory>
#include "libmvme_export.h"

#include "mvlc/mvlc_qt_object.h"
#include "vme_config.h"

namespace mesytec
{

class LIBMVME_EXPORT MVLCTriggerIOEditor: public QWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &msg);
        void runScriptConfig(VMEScriptConfig *setupScript);
        void addApplicationWidget(QWidget *widget);

    public:
        MVLCTriggerIOEditor(
            VMEScriptConfig *triggerIOScript,
            QWidget *parent = nullptr);

        ~MVLCTriggerIOEditor();

    public slots:
        // Set the event names from the vme config. This information is
        // displayed when editing one of the MVLC StackStart or StackBusy
        // units.
        void setVMEEventNames(const QStringList &names);

        // Set the MVLC to use. Needed for the trigger_io scope code.
        void setMVLC(mvlc::MVLC mvlc);

    private slots:
        void runScript_();
        void setupModified();
        void regenerateScript();
        void reload();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_EDITOR_H__ */
