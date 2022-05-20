#include "vme_config_model_view.h"

namespace mesytec
{
namespace mvme
{

const std::vector<QString> EventModel::EventChildNodes =
{
    QSL("Modules Init"),
    QSL("Readout Loop"),
    QSL("Multicast DAQ Start/Stop"),
};

EventModel::~EventModel()
{ }

}
}
