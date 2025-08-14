#ifndef D945D603_D859_4A03_B6A8_9E003FCF1ECF
#define D945D603_D859_4A03_B6A8_9E003FCF1ECF

#include <memory>
#include "stream_processor_consumers.h"

namespace mesytec::mvme
{

// MvmeTcpStreamServer is a server for streaming raw VME readout data over TCP.
// Implements the IStreamBufferConsumer interface, so it can be attached to any
// StreamWorkerBase instance. Stream workers take data from the non-blocking,
// possibly lossfull queue between the readout and the analysis systems. This
// means slow clients will not block the readout but will slow down the internal
// mvme analysis.
//
// The server guarantees that only complete readout buffers as taken from the
// queue are sent. Each buffer is prefixed by a 32-bit word specifying the
// number of words in the data buffer. Newly connected clients will receive the
// next complete buffer. There is no need to handle partial buffers on clients.
//
// TODO: write down where to find the client code.

class MvmeTcpStreamServer: public IStreamBufferConsumer
{
  public:

    static const char *DefaultListenUri;

    explicit MvmeTcpStreamServer(const std::string &listenUri = DefaultListenUri);
    ~MvmeTcpStreamServer() override;

    void startup() override;
    void shutdown() override;

    void beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                  const analysis::Analysis *analysis) override;

    void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

    void processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                       size_t bufferSize) override;

    void setLogger(Logger logger);
    Logger &getLogger();

    void reloadConfiguration() override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvme

#endif /* D945D603_D859_4A03_B6A8_9E003FCF1ECF */
