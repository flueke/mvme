#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include <chrono>
#include <system_error>

#include "mesytec-mvlc_export.h"

#include "mvlc.h"
#include "mvlc_constants.h"
#include "mvlc_dialog_util.h"
#include "util/readout_buffer.h"

namespace mesytec
{
namespace mvlc
{

#if 0
enum class MESYTEC_MVLC_EXPORT ReadoutWorkerError
{
    NoError,
    ReadoutRunning,
    ReadoutNotRunning,
    ReadoutPaused,
    ReadoutNotPaused,
};

MESYTEC_MVLC_EXPORT std::error_code make_error_code(ReadoutWorkerError error);

class MESYTEC_MVLC_EXPORT ReadoutWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused, Stopping };

        struct Counters
        {
            std::chrono::time_point<std::chrono::steady_clock> tStart;
            std::chrono::time_point<std::chrono::steady_clock> tEnd;
            size_t buffersRead;
            size_t bytesRead;
            size_t framingErrors;
            size_t unusedBytes;
            size_t readTimeouts;
            std::array<size_t, stacks::StackCount> stackHits;
        };

        ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers);

        State state() const;
        std::error_code start(const std::chrono::seconds &duration = {});
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif

struct MESYTEC_MVLC_EXPORT CrateConfig
{
    ConnectionType connectionType;
    int usbIndex = -1;
    std::string usbSerial;
    std::string ethHost;

    std::vector<StackCommandBuilder> stacks;
    std::vector<u32> triggers;
    StackCommandBuilder initCommands;

    bool operator==(const CrateConfig &o) const;
    bool operator!=(const CrateConfig &o) const { return !(*this == o); }
};

std::string MESYTEC_MVLC_EXPORT to_yaml(const CrateConfig &crateConfig);
CrateConfig MESYTEC_MVLC_EXPORT crate_config_from_yaml(const std::string &yaml);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
