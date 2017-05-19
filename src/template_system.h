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
};

struct MVMETemplates
{
    VMEEventTemplates eventTemplates;
    QVector<VMEModuleMeta> moduleMetas;
};

using TemplateLogger = std::function<void (const QString &)>;

MVMETemplates read_templates(TemplateLogger logger = TemplateLogger());
MVMETemplates read_templates_from_path(const QString &path, TemplateLogger logger = TemplateLogger());

QTextStream &operator<<(QTextStream &out, const MVMETemplates &templates);

}


#endif /* __TEMPLATE_SYSTEM_H__ */
