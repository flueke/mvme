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
#ifndef __TEMPLATE_SYSTEM_H__
#define __TEMPLATE_SYSTEM_H__

#include "typedefs.h"
#include <QString>
#include <QTextStream>
#include <QVector>
#include <functional>

// VME/Analysis Template System

namespace vats
{

struct VMETemplate
{
    QString contents;
    QString name;
    QString sourceFileName;
};

struct VMEEventTemplates
{
    VMETemplate daqStart;
    VMETemplate daqStop;
    VMETemplate readoutCycleStart;
    VMETemplate readoutCycleEnd;
};

struct VMEModuleTemplates
{
    VMETemplate reset;
    VMETemplate readout;
    QVector<VMETemplate> init;
};

struct VMEModuleMeta
{
    static const u8 InvalidTypeId = 0;

    u8 typeId = InvalidTypeId;
    QString typeName;
    QString displayName;
    VMEModuleTemplates templates;

    QString templatePath;
};

struct MVMETemplates
{
    VMEEventTemplates eventTemplates;
    QVector<VMEModuleMeta> moduleMetas;
};

using TemplateLogger = std::function<void (const QString &)>;

// Read templates from the default template path
MVMETemplates read_templates(TemplateLogger logger = TemplateLogger());
// Read templates from the given path
MVMETemplates read_templates_from_path(const QString &path, TemplateLogger logger = TemplateLogger());

QString get_module_path(const QString &moduleTypeName);

// Returns the base template path
QString get_template_path();

// Output diagnostic information about the templates.
QTextStream &operator<<(QTextStream &out, const MVMETemplates &templates);

}


#endif /* __TEMPLATE_SYSTEM_H__ */
