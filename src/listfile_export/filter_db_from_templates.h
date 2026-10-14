#ifndef MVME_LISTFILE_EXPORT_FILTER_DB_FROM_TEMPLATES_H
#define MVME_LISTFILE_EXPORT_FILTER_DB_FROM_TEMPLATES_H

#include "listfile_export/listfile_export.h"
#include "template_system.h"

namespace mesytec::mvme::listfile_export
{

// Builds the filter db from the default analysis filters of the vme module templates. Only
// single word extraction filters without a word index are used.
FilterDb filter_db_from_templates(const vats::MVMETemplates &templates);

} // namespace mesytec::mvme::listfile_export

#endif /* MVME_LISTFILE_EXPORT_FILTER_DB_FROM_TEMPLATES_H */
