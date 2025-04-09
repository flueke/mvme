#ifndef EEE8AF02_8820_460F_828A_F7F3A9994DB7
#define EEE8AF02_8820_460F_828A_F7F3A9994DB7

#include <map>
#include <ostream>
#include <set>

#include <mesytec-mvlc/util/fmt.h>
#include "typedefs.h"

// FIXME: make wrapper for the mbs internal structs. do not include any mbs headers here. do not leak mbs stuff.
extern "C"
{
#include <gsi-mbs-api/f_evt.h>
#include <gsi-mbs-api/s_filhe.h>
}

std::ostream &operator<<(std::ostream &os, const s_filhe &fileHeader);
std::ostream &operator<<(std::ostream &os, const s_bufhe &buf);
std::ostream &operator<<(std::ostream &os, const s_ve10_1 &event);
std::ostream &operator<<(std::ostream &os, const s_ves10_1 &event);

template <> struct fmt::formatter<s_filhe>: fmt::ostream_formatter{};
template <> struct fmt::formatter<s_bufhe>: fmt::ostream_formatter{};
template <> struct fmt::formatter<s_ve10_1>: fmt::ostream_formatter{};
template <> struct fmt::formatter<s_ves10_1>: fmt::ostream_formatter{};

namespace mesytec::mvme::mbs
{

struct LmdWrapper
{
    s_evt_channel *eventChannel = nullptr;
    s_filhe *fileHeaderPtr = nullptr;
    s_bufhe *bufferPtr = nullptr;
    s_ve10_1 *eventPtr = nullptr;
    s_ves10_1 *subEventPtr = nullptr;
    size_t subEventIndex = 1; // first valid subevent index is 1
    size_t subEventCount_ = 0;
    std::basic_string_view<const u32> subEventView_;

    LmdWrapper()
    {
        eventChannel = f_evt_control();
        if (!eventChannel)
            throw std::runtime_error("failed to create event channel");
    }

    ~LmdWrapper()
    {
        close();
        free(eventChannel);
    }

    void open(const std::string &lmdFilename, bool doPrint = true)
    {
        auto status = f_evt_get_tagopen(
            eventChannel,
            nullptr, // tag file
            const_cast<char *>(lmdFilename.c_str()), // lmd file
            reinterpret_cast<char **>(&fileHeaderPtr), // may be null in which case no file "header or other" information is returned
            doPrint
        );
        if (status) throw status;
    }

    void close()
    {
        auto status = f_evt_get_tagclose(eventChannel);
        if (status) throw status;
    }

    bool nextEvent()
    {
        if (!eventChannel) throw std::runtime_error("file not opened");

        auto status = f_evt_get_event(eventChannel, reinterpret_cast<INTS4 **>(&eventPtr), reinterpret_cast<INTS4 **>(&bufferPtr));
        if (status == GETEVT__NOMORE)
            return false; // no more events
        if (status) throw status;
        subEventCount_ = f_evt_get_subevent(eventPtr, 0, nullptr, nullptr, nullptr);
        subEventIndex = 1; // reset to first subevent
        return true;
    }

    bool nextSubEvent()
    {
        if (!eventPtr) throw std::runtime_error("no event available");

        if (subEventCount_ == 0)
            return false; // no more subevents

        if (subEventIndex > subEventCount_)
        {
            subEventCount_ = 0;
            return false; // no more subevents
        }

        // get the next subevent
        u32 *dataPtr = nullptr;
        INTS4 longwords = 0;
        auto status = f_evt_get_subevent(eventPtr, subEventIndex,
            reinterpret_cast<INTS4 **>(&subEventPtr),
            reinterpret_cast<INTS4 **>(&dataPtr),
            &longwords);

        if (status == GETEVT__NOMORE)
        {
            subEventCount_ = 0;
            return false; // no more subevents
        }
        else if (status) throw status;
        subEventView_ = std::basic_string_view<const u32>(dataPtr, longwords);
        ++subEventIndex;
        return true;
    }

    size_t subEventCount() const { return subEventCount_; }
    std::basic_string_view<const u32> subEventView() const { return subEventView_; }

    std::ostream &dumpState(std::ostream &out)
    {
        out << "LmdWrapper state:\n";
        out << "  eventChannel: " << eventChannel << "\n";
        out << "  fileHeaderPtr: " << fileHeaderPtr << "\n";
        out << "  bufferPtr: " << bufferPtr << "\n";
        out << "  eventPtr: " << eventPtr << "\n";
        out << "  subEventPtr: " << subEventPtr << "\n";
        out << "  subEventIndex: " << subEventIndex << "\n";
        out << "  subEventCount: " << subEventCount_ << "\n";
        return out;
    }
};

struct LmdQuickInfo
{
    std::map<int, size_t> triggerCounts;
    std::set<int> subcrates;
    s64 firstEventNumber;
    s64 lastEventNumber;
    size_t eventCount;
    size_t subEventCount;
    s_filhe fileHeader;
};

LmdQuickInfo scan_lmd_file(const std::string &lmdFilename);

std::ostream &operator<<(std::ostream &os, const LmdQuickInfo &info);

}

template <> struct fmt::formatter<mesytec::mvme::mbs::LmdQuickInfo>: fmt::ostream_formatter{};

#endif /* EEE8AF02_8820_460F_828A_F7F3A9994DB7 */
