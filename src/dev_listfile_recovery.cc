#include <filesystem>
#include <spdlog/spdlog.h>
#include "listfile_recovery.h"
using namespace mesytec::mvme::listfile_recovery;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        spdlog::error("Usage: {} <input zip filename>", argv[0]);
        return 1;
    }

    EntryFindResult findResult{};

    try
    {
        findResult = find_first_entry(argv[1]);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Error from find_first_entry: {}", e.what());
        return 1;
    }

    spdlog::info("Found zip entry: startOffset={}, compressionType={}, entryName={}",
        findResult.headerOffset, findResult.compressionType, findResult.entryName);

    if (findResult.entryName.empty())
    {
        spdlog::error("Found zip entryName is empty, aborting.");
        return 1;
    }

    auto outputFilename = std::filesystem::path(findResult.entryName).filename().stem().string() + "-recovered.zip";

    spdlog::info("Attempting recovery. Output filename: {}", outputFilename);

    mesytec::mvlc::Protected<RecoveryProgress> recoveryProgress;

    auto recoveryResult = recover_listfile(
        argv[1],
        outputFilename,
        findResult,
        recoveryProgress);

    auto p = recoveryResult;

    spdlog::info("Recovery attempt completed. inputFileSize={}, inputBytesRead={}, outputBytesWritten={}",
                 p.inputBytesRead, p.inputFileSize, p.outputBytesWritten);

    return 0;
}