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

#include <functional>
#include <QJsonObject>
#include <QString>
#include <QTextStream>
#include <QVector>

#include "libmvme_export.h"
#include "typedefs.h"

// VME/Analysis Template System - VATS (not related to the Vault-Tec Assisted
// Targeting System, that would be V.A.T.S. :-)

namespace vats
{

struct LIBMVME_EXPORT VMETemplate
{
    QString contents;
    QString name;
    QString sourceFileName;
};

bool operator==(const VMETemplate &ta, const VMETemplate &tb);
bool operator!=(const VMETemplate &ta, const VMETemplate &tb);

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

bool operator==(const VMEModuleTemplates &mta, const VMEModuleTemplates &mtb);
bool operator!=(const VMEModuleTemplates &mta, const VMEModuleTemplates &mtb);

struct LIBMVME_EXPORT VMEModuleMeta
{
    static const u8 InvalidTypeId = 0;

    u8 typeId = InvalidTypeId;
    QString typeName;
    QString displayName;
    QString vendorName;
    VMEModuleTemplates templates;
    QByteArray eventHeaderFilter;
    u32 vmeAddress = 0u;

    QString templatePath;
};

bool operator==(const VMEModuleMeta &mma, const VMEModuleMeta &mmb);
bool operator!=(const VMEModuleMeta &mma, const VMEModuleMeta &mmb);

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

VMEModuleMeta LIBMVME_EXPORT get_module_meta_by_typename(const MVMETemplates &templates,
                                                         const QString &moduleTypename);

VMEModuleMeta LIBMVME_EXPORT get_module_meta_by_typeId(const MVMETemplates &templates,
                                                       u8 typeId);

struct LIBMVME_EXPORT AuxiliaryVMEScriptInfo
{
    // JSON info object associated with this script. Contains data like fileName,
    // scriptName, vendorName, moduleName and variables.
    QJsonObject info;

    // The contents of the script file.
    QString contents;

    // The name of the JSON file that contained the 'info' object.
    QString auxInfoFileName;

    QString fileName() const { return info["fileName"].toString(); }
    QString scriptName() const { return info["scriptName"].toString(); }
    QString vendorName() const { return info["vendorName"].toString(); }
    QString moduleName() const { return info["moduleName"].toString(); }
};

QVector<AuxiliaryVMEScriptInfo> LIBMVME_EXPORT read_auxiliary_scripts(
    TemplateLogger logger = TemplateLogger());

QVector<AuxiliaryVMEScriptInfo> LIBMVME_EXPORT read_auxiliary_scripts(
    const QString &path, TemplateLogger logger = TemplateLogger());

}  // namespace vats

#endif /* __TEMPLATE_SYSTEM_H__ */
