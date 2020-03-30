#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include <chrono>
#include <system_error>
#include "mvlc.h"
#include "mvlc_dialog_util.h"

namespace mesytec
{
namespace mvlc
{

class ReadoutWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused };

        ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers);

        State state() const;
        std::error_code start(const std::chrono::seconds &duration);
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
