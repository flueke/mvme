#ifndef __MVME_VME_CONFIG_UTIL_H__
#define __MVME_VME_CONFIG_UTIL_H__

#include <memory>
#include <QMimeData>

#include "libmvme_export.h"
#include "vme_config.h"

namespace mvme
{
namespace vme_config
{

// Creates a symbol table containing the standard vme event variables:
// sys_irq, mesy_mcst, mesy_reaodut_num_events, mesy_eoe_marker
vme_script::SymbolTable make_standard_event_variables(u8 irq = 1, u8 mcst = 0xbb);

// Factory funcion to setup a new EventConfig instance which is to be added to
// the given VMEConfig.
// - A new unique objectName is set on the returned value.
// - The trigger IRQ value is set to the first unused irq in the system or '0' if
//   all irqs are in use.
// - The set of default variables for the event is created: sys_irq, mesy_mcst,
//   mesy_readout_num_events, mesy_eoe_marker.

std::unique_ptr<EventConfig> LIBMVME_EXPORT make_new_event_config(const VMEConfig *parentVMEConfig);

// Returns the first unused irq number in the given config or 0 if there are no
// unused irqs.
u8 LIBMVME_EXPORT get_next_free_irq(const VMEConfig *vmeConfig);

//
// Copy/Paste and Drag/Drop support via QMimeData
//

static const QString MIMEType_JSON_ContainerObject = QSL("application/x-mvme-vmeconf-container");
static const QString MIMEType_JSON_VMEEventConfig  = QSL("application/x-mvme-vmeconf-event");
static const QString MIMEType_JSON_VMEModuleConfig = QSL("application/x-mvme-vmeconf-module");
static const QString MIMEType_JSON_VMEScriptConfig = QSL("application/x-mvme-vmeconf-script");

QString get_mime_type(const ConfigObject *obj);
bool can_mime_copy_object(const ConfigObject *obj);
bool contains_object_mime_type(const QMimeData *mimeData);
std::unique_ptr<QMimeData> make_mime_data(const ConfigObject *obj);
std::unique_ptr<ConfigObject> make_object_from_mime_data(const QMimeData *mimeData);
void generate_new_object_ids(ConfigObject *root);

} // end namespace vme_config
} // end namespace mvme

#endif /* __MVME_VME_CONFIG_UTIL_H__ */
