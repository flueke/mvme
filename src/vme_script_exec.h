#ifndef __MVME_VME_SCRIPT_EXEC_H__
#define __MVME_VME_SCRIPT_EXEC_H__

#include "vme_script.h"
#include "vme_controller.h"

namespace vme_script
{

struct LIBMVME_CORE_EXPORT Result
{
    VMEError error;
    uint32_t value = 0u;
    QVector<uint32_t> valueVector = {};
    Command command;
};

using ResultList = QVector<Result>;
using LoggerFun = std::function<void (const QString &)>;

namespace run_script_options
{
    using Flag = u8;
    static const Flag LogEachResult = 1u << 0;
    static const Flag AbortOnError  = 1u << 1;
}

// Classic version of run_script taking a single logger function which handles
// both normal and error messages.
ResultList LIBMVME_CORE_EXPORT run_script(
    VMEController *controller,
    const VMEScript &script,
    LoggerFun logger = LoggerFun(),
    const run_script_options::Flag &options = 0);

// Updated version taking a second logger to handle error messages.
ResultList LIBMVME_CORE_EXPORT run_script(
    VMEController *controller,
    const VMEScript &script,
    LoggerFun logger,
    LoggerFun error_logger,
    const run_script_options::Flag &options = 0);

inline bool has_errors(const ResultList &results)
{
    return std::any_of(
        std::begin(results), std::end(results),
        [] (const Result &r) { return r.error.isError(); });
}

LIBMVME_CORE_EXPORT Result run_command(VMEController *controller,
                                  const Command &cmd,
                                  LoggerFun logger = LoggerFun());

LIBMVME_CORE_EXPORT QString format_result(const Result &result);

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_EXEC_H__ */
