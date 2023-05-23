#include "replay_ui_p.h"

namespace mesytec::mvme
{

BrowseFilterModel::~BrowseFilterModel() {}
BrowseByRunTreeModel::~BrowseByRunTreeModel() {}
QueueTableModel::~QueueTableModel() {}
const QStringList QueueTableModel::Headers = { QSL("Name"), QSL("Date Modified"), QSL("Info0"), QSL("Info1") };

}