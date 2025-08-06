#ifndef D945D603_D859_4A03_B6A8_9E003FCF1ECF
#define D945D603_D859_4A03_B6A8_9E003FCF1ECF

#include <memory>
#include "stream_processor_consumers.h"

namespace mesytec::mvme
{

class MvmeTcpStreamServer: public IStreamBufferConsumer
{
  public:
    explicit MvmeTcpStreamServer(const std::string &address = "localhost", uint16_t port = 42333u);
    ~MvmeTcpStreamServer() override = default;

    void startup() override;
    void shutdown() override;

    void beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                  const analysis::Analysis *analysis) override;

    void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

    void processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                       size_t bufferSize) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvme

#endif /* D945D603_D859_4A03_B6A8_9E003FCF1ECF */
