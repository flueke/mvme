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
       << ", i_type=" << buf.i_type
       << ", i_subtype=" << buf.i_subtype
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
       << ", i_type=" << event.i_type
       << ", i_subtype=" << event.i_subtype
       << ", i_trigger=" << event.i_trigger
       << ", i_dummy=" << event.i_dummy
       << ", l_count=" << event.l_count
       << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const s_ves10_1 &event)
{
    os << "s_ves10_1 { l_dlen=" << event.l_dlen
       << ", i_type=" << event.i_type
       << ", i_subtype=" << event.i_subtype
       << ", i_procid=" << event.i_procid
       << ", h_subcrate=" << static_cast<int>(event.h_subcrate)
       << ", h_control=" << static_cast<int>(event.h_control)
       << " }";
    return os;
}

using u8 = std::uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);

    fmt::print("Struct sizes: s_ve10_1={}, s_ves10_1={}, s_bufhe={}\n",
                sizeof(s_ve10_1), sizeof(s_ves10_1), sizeof(s_bufhe));

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
    bool doPrint = false; // make the f_evt_get_tagopen() print file info using f_evt_type()
    if (verbosity > 0)
        doPrint = true;

    // No real difference between the calls except that f_evt_get_tagopen()
    #if 1
    auto status = f_evt_get_tagopen(
        eventChannel,
        nullptr, // tag file
        const_cast<char *>(lmdFilename.c_str()), // lmd file
        reinterpret_cast<char **>(&headPtr), // may be null in which case no file "header or other" information is returned
        doPrint
    );
    #else
    auto status = f_evt_get_open(
        GETEVT__FILE,  // mode flag: file, stream, transport, event_server, remote event_server
        const_cast<char *>(lmdFilename.c_str()), // lmd file
        eventChannel,
        reinterpret_cast<char **>(&headPtr), // may be null in which case no file "header or other" information is returned
        0, // l_sample: tell server to send only every nth event
        0 // l_para: not used
    );
    #endif

    if (status) return status;

    // event loop

    struct State
    {
        size_t eventIndex = 0;
        std::map<int, size_t> triggerCounts;
    };

    State state;

    while (true)
    {
        s_ve10_1 *eventPtr = nullptr;
        s_bufhe *bufferPtr = nullptr;
        auto status = f_evt_get_event(eventChannel, reinterpret_cast<INTS4 **>(&eventPtr), reinterpret_cast<INTS4 **>(&bufferPtr));

        if (status)
        {
            if (status == GETEVT__NOMORE)
                break;
            else
            {
                spdlog::error("f_evt_get_event() failed with status {}", status);
                return status;
            }
        }

        if (!bufferPtr)
        {
            spdlog::error("f_evt_get_event() returned nullptr for s_bufhe bufferPtr");
            return 1;
        }

        if (!eventPtr)
        {
            spdlog::error("f_evt_get_event() returned nullptr for s_ve10_1 eventPtr");
            return 1;
        }

        //if (bufferPtr && verbosity > 1)
        //    std::cout << "Buffer: " << *bufferPtr << "\n";

        //if (eventPtr && verbosity > 1)
        //    std::cout << fmt::format("Event #{}:", state.eventIndex) << *eventPtr << "\n";

        //std::cout << fmt::format(">>> Buffer:   ") << *bufferPtr << "\n";
        std::cout << fmt::format(">>> Event#{}: ", state.eventIndex) << *eventPtr << "\n";
        f_evt_type(bufferPtr, reinterpret_cast<s_evhe *>(eventPtr), -1, 1, 1, 1);

        if (eventPtr)
        {
            #if 0
            // Source: https://github.com/gsi-ee/go4/blob/7b0b86c929a16c94472b4c189ab1af12213ba8eb/Go4EventServer/TGo4MbsSource.cxx#L146
            //   Char_t *endofevent = (Char_t *) (fxEvent) + (fxEvent->l_dlen) * fguSHORTBYCHAR + fguEVHEBYCHAR;
            auto firstSubEvent = reinterpret_cast<const s_ves10_1 *>(eventPtr + 1);
            assert(eventPtr->l_dlen >= firstSubEvent->l_dlen);

            auto subEventBytes = (firstSubEvent->l_dlen - 2) * sizeof(u16); // the magic -2 comes from gsi code
            auto startOfSubEvent = reinterpret_cast<const u8 *>(firstSubEvent+1);
            //auto endOfSubEvent = startOfSubEvent + subEventBytes;
            auto subEventWords = subEventBytes / sizeof(u32);
            auto subEventRemainder = subEventBytes % sizeof(u32);
            assert(subEventRemainder == 0);

            std::basic_string_view<const u32> subEventData(reinterpret_cast<const u32 *>(startOfSubEvent), subEventWords);

            std::cout << fmt::format(">>>>>> First Subevent: ") << *firstSubEvent << "\n";
            std::cout << fmt::format(">>>>>> Subevent data (size={} words, {} bytes): {:#010x}\n",
                subEventData.size(), subEventData.size() * sizeof(u32), fmt::join(subEventData, ", "));
            //f_evt_type(nullptr, reinterpret_cast<s_evhe *>(const_cast<s_ves10_1 *>(firstSubEvent)), -1, 1, 1, 1);

            std::cout << "\n";

            #if 0
            auto startOfEvent = reinterpret_cast<const u8 *>(eventPtr + 1);
            auto endOfEvent = reinterpret_cast<const u8 *>(eventPtr) + eventPtr->l_dlen * sizeof(u16) + sizeof(u32);
            auto eventBytes = endOfEvent - startOfEvent;
            auto eventWords = eventBytes / sizeof(u32);
            auto remainder = eventBytes % sizeof(u32);
            if (remainder)
                std::terminate();

            std::basic_string_view<const u8> eventData(startOfEvent, eventWords);
            std::cout << fmt::format(">>> Event data (size={}): {:#010x}\n", eventData.size(), fmt::join(eventData, ", "));

            //auto startOfData = reinterpret_cast<const u32*>(startOfEvent);
            //auto endOfData = reinterpret_cast<const u32 *>(endOfEvent);
            //assert(endOfData >= startOfData);
            //std::basic_string_view<const u32> eventData(startOfData, eventWords);
            //fmt::print("Event data (size={}): {:#010x}\n", eventData.size(), fmt::join(eventData, ", "));
            //spdlog::info("Event #{}: {} bytes, {} words, %={}", state.eventIndex, eventBytes, eventWords, remainder);

            if (eventPtr->i_type == 10 && eventPtr->l_dlen > 0 && static_cast<size_t>(eventPtr->l_dlen) >= sizeof(s_ves10_1))
            {
                while (eventData.size() >= sizeof(s_ves10_1))
                {
                    auto *subevent = reinterpret_cast<const s_ves10_1 *>(eventData.data());
                    f_evt_type(nullptr, reinterpret_cast<s_evhe *>(const_cast<s_ves10_1 *>(subevent)), -1, 1, 1, 1);
                    auto startOfSubEvent = reinterpret_cast<const u8 *>(subevent + 1);
                    auto endOfSubEvent = startOfSubEvent + subevent->l_dlen * sizeof(u16);
                    std::basic_string_view<const u8> subeventData(startOfSubEvent, endOfSubEvent - startOfSubEvent);
                    std::basic_string_view<const u32> subeventWords(reinterpret_cast<const u32 *>(startOfSubEvent), subeventData.size() / sizeof(u32));
                    //spdlog::info("  Subevent: {} bytes, {} words", subeventData.size(), subeventWords.size());
                    //spdlog::info("  Subevent data: {:#010x}\n", fmt::join(subeventWords, ", "));

                    eventData.remove_prefix(sizeof(s_ves10_1) + subeventData.size()); // consume the subevent
                }
            }
            #endif
            #elif 0
            auto startOfEventData = reinterpret_cast<const u8 *>(eventPtr + 1);
            auto endOfEventData = reinterpret_cast<const u8 *>(eventPtr) + eventPtr->l_dlen * sizeof(u16) + sizeof(u32);
            auto eventDataBytes = endOfEventData - startOfEventData;
            auto eventDataWords = eventDataBytes / sizeof(u32);
            auto remainder = eventDataBytes % sizeof(u32);
            if (remainder)
                std::terminate();

            std::basic_string_view<const u8> eventData(startOfEventData, eventDataBytes);
            std::cout << fmt::format(">>> Event data: size={} words, {} shorts, {} bytes\n",
                eventData.size() / sizeof(u32), eventData.size() / sizeof(u16), eventData.size());

            size_t subEventsSeen = 0;

            while (eventData.size() >= sizeof(s_ves10_1))
            {
                auto subEvent = reinterpret_cast<const s_ves10_1 *>(eventData.data());

                if (subEvent->l_dlen < 2)
                {
                    spdlog::error("Subevent {} has l_dlen < 2 ({})", subEventsSeen, subEvent->l_dlen);
                    break;
                }

                auto subEventDataBytes = (subEvent->l_dlen - 2) * sizeof(u16); // -2 magic is from gsi code
                auto subEventDataWords = subEventDataBytes / sizeof(u32);
                auto startOfSubEventData = reinterpret_cast<const u8 *>(subEvent + 1);
                std::basic_string_view<const u32> subEventData(reinterpret_cast<const u32 *>(startOfSubEventData), subEventDataWords);
                std::cout << fmt::format(">>>>>> SubEvent {} data (size={} words, {} shorts, {} bytes): {:#010x}\n",
                    subEventsSeen, subEventData.size(), subEventData.size() * sizeof(u16), subEventData.size() * sizeof(u32), fmt::join(subEventData, ", "));

                // consume the subevent
                auto bytesToRemove = subEventDataBytes;
                assert(bytesToRemove % sizeof(u32) == 0);
                assert(eventData.size() >= bytesToRemove);

                eventData.remove_prefix(bytesToRemove);
                ++subEventsSeen;
            }

            std::cout << fmt::format(">>> Event contained {} subevents\n", subEventsSeen);
            if (!eventData.empty())
            {
                std::cout << fmt::format(">> event data left after consuming subevents: size={} words\n", eventData.size());
            }
            assert(eventData.size() == 0);
            #else

            #endif
        }

        if (status) return status;

        ++state.eventIndex;
        ++state.triggerCounts[eventPtr->i_trigger];
    }

    spdlog::info("Processed {} events", state.eventIndex);
    spdlog::info("Trigger counts: {}", fmt::join(state.triggerCounts, ", "));

    // close & cleanup
    status = f_evt_get_tagclose(eventChannel);
    if (status) return status;

    free(eventChannel);

    return 0;
}
