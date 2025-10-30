#include "histo_algo.h"
#include <mesytec-mvlc/util/string_util.h>

namespace mesytec::mvme
{

std::string to_string(const HistoOpsBinningMode &mode)
{
    switch (mode)
    {
    case HistoOpsBinningMode::MinimumBins:
        return "MinimumBins";
    case HistoOpsBinningMode::MaximumBins:
        return "MaximumBins";
    default:
        return "Unknown";
    }
}

std::optional<HistoOpsBinningMode> histo_ops_binning_mode_from_string(const std::string &mode_)
{
    auto mode = mvlc::util::str_tolower(mode_);

    if (mode == "minimumbins")
        return HistoOpsBinningMode::MinimumBins;
    else if (mode == "maximumbins")
        return HistoOpsBinningMode::MaximumBins;

    return std::nullopt;
}

}
