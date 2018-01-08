/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __TEMPLATE_SYSTEM_H__
#define __TEMPLATE_SYSTEM_H__

#include "libmvme_export.h"
#include "typedefs.h"
#include <QString>
#include <QTextStream>
#include <QVector>
#include <functional>

// VME/Analysis Template System

namespace vats
{

struct LIBMVME_EXPORT VMETemplate
{
    QString contents;
    QString name;
    QString sourceFileName;
};

struct LIBMVME_EXPORT VMEEventTemplates
{
    VMETemplate daqStart;
    VMETemplate daqStop;
    VMETemplate readoutCycleStart;
    VMETemplate readoutCycleEnd;
};

struct LIBMVME_EXPORT VMEModuleTemplates
{
    VMETemplate reset;
    VMETemplate readout;
    QVector<VMETemplate> init;
};

struct LIBMVME_EXPORT VMEModuleMeta
{
    static const u8 InvalidTypeId = 0;

    u8 typeId = InvalidTypeId;
    QString typeName;
    QString displayName;
    QString vendorName;
    VMEModuleTemplates templates;
    QByteArray eventHeaderFilter;

    QString templatePath;
};

struct LIBMVME_EXPORT MVMETemplates
{
    VMEEventTemplates eventTemplates;
    QVector<VMEModuleMeta> moduleMetas;
};

using TemplateLogger = std::function<void (const QString &)>;

// Read templates from the default template path
MVMETemplates LIBMVME_EXPORT read_templates(TemplateLogger logger = TemplateLogger());
// Read templates from the given path
MVMETemplates LIBMVME_EXPORT read_templates_from_path(const QString &path, TemplateLogger logger = TemplateLogger());

QString LIBMVME_EXPORT get_module_path(const QString &moduleTypeName);

// Returns the base template path
QString LIBMVME_EXPORT get_template_path();

// Output diagnostic information about the templates.
LIBMVME_EXPORT QTextStream &operator<<(QTextStream &out, const MVMETemplates &templates);

VMEModuleMeta LIBMVME_EXPORT get_module_meta_by_typename(const MVMETemplates &templates, const QString &moduleTypename);

}  // namespace vats

#endif /* __TEMPLATE_SYSTEM_H__ */
