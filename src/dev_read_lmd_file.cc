//#include "mvme_session.h"
#include <argh.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <mesytec-mvlc/util/fmt.h>

extern "C"
{
//#include <gsi-mbs-api/fLmd.h>
#include <gsi-mbs-api/f_evt.h>
}

std::ostream &operator<<(std::ostream &os, const s_bufhe &buf)
{
    os << "s_bufhe { l_dlen=" << buf.l_dlen
       << ", i_subtype=" << buf.i_subtype
       << ", i_type=" << buf.i_type
       << ", h_begin=" << static_cast<int>(buf.h_begin)
       << ", h_end=" << static_cast<int>(buf.h_end)
       << ", i_used=" << buf.i_used
       << ", l_buf=" << buf.l_buf
       << ", l_evt=" << buf.l_evt
       << ", l_current_i=" << buf.l_current_i
       << ", l_time[0]=" << buf.l_time[0]
       << ", l_time[1]=" << buf.l_time[1]
       << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const s_ve10_1 &event)
{
    os << "s_ve10_1 { l_dlen=" << event.l_dlen
       << ", i_subtype=" << event.i_subtype
       << ", i_type=" << event.i_type
       << ", i_trigger=" << event.i_trigger
       << ", i_dummy=" << event.i_dummy
       << ", l_count=" << event.l_count
       << " }";
    return os;
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    argh::parser parser;
    parser.parse(argc, argv, argh::parser::PREFER_FLAG_FOR_UNREG_OPTION | argh::parser::SINGLE_DASH_IS_MULTIFLAG);;
    auto verbosity = parser.flags().count("v");
    bool optPrintData = parser["--print-data"];

    if (parser.pos_args().size() != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <lmd-file> <mvme-vme-config-file>" << std::endl;
        return 1;
    }

    auto lmdFilename = parser.pos_args()[1];
    auto mvmeVmeConfigFilename = parser.pos_args()[2];

    // open
    void *headPtr = nullptr;
    auto eventChannel = f_evt_control();
    bool doPrint = false; // make the f_evt_get_tagopen() print file info
    if (verbosity > 0)
        doPrint = true;
    auto status = f_evt_get_tagopen(
        eventChannel,
        nullptr, // tag file
        const_cast<char *>(lmdFilename.c_str()), // lmd file
        reinterpret_cast<char **>(&headPtr), // may be null in which case no file "header or other" information is returned
        doPrint
    );

    if (status) return status;

    // event loop

    struct State
    {
        size_t eventIndex = 0;
    };

    State state;
    std::map<int, size_t> triggerCounts;

    while (true)
    {
        s_ve10_1 *eventPtr = nullptr;
        s_bufhe *bufferPtr = nullptr;
        //auto status = f_evt_get_tagnext(eventChannel, eventsToSkip, &eventPtr);
        auto status = f_evt_get_event(eventChannel, reinterpret_cast<INTS4 **>(&eventPtr), reinterpret_cast<INTS4 **>(&bufferPtr));
        if (status)
        {
            if (status == GETEVT__NOMORE)
                break;
            else
                return status;
        }

        if (bufferPtr && verbosity > 1)
            std::cout << "Buffer: " << *bufferPtr << "\n";

        if (eventPtr && verbosity > 1)
            std::cout << fmt::format("Event #{}:", state.eventIndex) << *eventPtr << "\n";

        if (eventPtr && optPrintData)
        {
            // Source: https://github.com/gsi-ee/go4/blob/7b0b86c929a16c94472b4c189ab1af12213ba8eb/Go4EventServer/TGo4MbsSource.cxx#L146
            //   Char_t *endofevent = (Char_t *) (fxEvent) + (fxEvent->l_dlen) * fguSHORTBYCHAR + fguEVHEBYCHAR;
            auto startOfEvent = reinterpret_cast<const char *>(eventPtr + 1);
            auto endOfEvent = reinterpret_cast<const char *>(eventPtr) + eventPtr->l_dlen * sizeof(std::uint16_t) + sizeof(std::uint32_t);
            auto eventBytes = endOfEvent - startOfEvent;
            auto eventWords = eventBytes / sizeof(std::uint32_t);
            auto remainder = eventBytes % sizeof(std::uint32_t);
            if (remainder)
                std::terminate();

            //auto startOfData = reinterpret_cast<const std::uint32_t*>(startOfEvent);
            //auto endOfData = reinterpret_cast<const std::uint32_t *>(endOfEvent);
            //assert(endOfData >= startOfData);
            //std::basic_string_view<const std::uint32_t> eventData(startOfData, endOfData - startOfData);
            //fmt::print("Event data (size={}): {:#010x}\n", eventData.size(), fmt::join(eventData, ", "));
            spdlog::info("Event #{}: {} bytes, {} words, %={}", state.eventIndex, eventBytes, eventWords, remainder);

            if (eventPtr->i_type == 10 && eventPtr->l_dlen > 4)
            {
                std::basic_string_view<const std::uint8_t> eventData(startOfEvent, eventBytes);
                auto *subevent = reinterpret_cast<s_ves10_1 *>(eventPtr + 1);
                auto startOfSubEvent = reinterpret_cast<const char *>(subevent + 1);
                auto endOfSubEvent = startOfSubEvent + subevent->l_dlen * sizeof(std::uint16_t);
                std::uint8_t *subeventPtr = nullptr;
            }
        }

        if (verbosity > 2)
            status = f_evt_type(bufferPtr, reinterpret_cast<s_evhe *>(eventPtr), -1, 1, 1, 1);

        if (status) return status;

        ++state.eventIndex;
        ++triggerCounts[eventPtr->i_trigger];
    }

    spdlog::info("Processed {} events", state.eventIndex);
    spdlog::info("Trigger counts: {}", fmt::join(triggerCounts, ", "));

    // close & cleanup
    status = f_evt_get_tagclose(eventChannel);
    if (status) return status;

    free(eventChannel);

    return 0;
}
