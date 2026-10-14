#ifndef D945D603_D859_4A03_B6A8_9E003FCF1ECF
#define D945D603_D859_4A03_B6A8_9E003FCF1ECF

#include <memory>
#include "stream_processor_consumers.h"

namespace mesytec::mvme
{

// MvmeStreamServer is a server for streaming (raw VME readout) data over TCP.
// Implements the IStreamBufferConsumer interface, so it can be attached to any
// StreamWorkerBase instance. Stream workers take data from the non-blocking,
// possibly lossfull queue between the readout and the analysis systems. This
// means slow clients will not block the readout but will slow down the internal
// mvme analysis. The slowest client dominates what everyone gets.
//
// The server guarantees that only complete readout buffers as taken from the
// queue are sent. Each buffer is prefixed by a 32-bit buffer number and a
// 32-bit word specifying the number of words in the buffer. The prefix is
// written in little-endian format, readout sent unmodified.
// format: bufferNumber: u32, bufferSize: u32, buffer: u32[]
//
// Newly connected clients will receive the next complete buffer.

class MvmeStreamServer: public IStreamBufferConsumer
{
  public:

    // Note: these must be expanded using expand_env_vars() from util/expand_env_vars.h
    static const std::vector<std::string> DefaultListenUris;

    MvmeStreamServer();
    ~MvmeStreamServer() override;

    void startup() override;
    void shutdown() override;

    void beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                  const analysis::Analysis *analysis) override;

    void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

    void processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                       size_t bufferSize) override;

    void setLogger(Logger logger) override;
    Logger &getLogger() override;

    void reloadConfiguration() override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvme

#endif /* D945D603_D859_4A03_B6A8_9E003FCF1ECF */
