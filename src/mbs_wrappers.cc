#include "mbs_wrappers.h"

std::ostream &operator<<(std::ostream &os, const s_filhe &fileHeader)
{
    os << "s_filhe { filhe_dlen=" << fileHeader.filhe_dlen
       << ", filhe_type=" << fileHeader.filhe_type << ", filhe_subtype=" << fileHeader.filhe_subtype
       << ", filhe_frag=" << fileHeader.filhe_frag << ", filhe_used=" << fileHeader.filhe_used
       << ", filhe_buf=" << fileHeader.filhe_buf << ", filhe_evt=" << fileHeader.filhe_evt
       << ", filhe_current_i=" << fileHeader.filhe_current_i
       << ", filhe_stime[0]=" << fileHeader.filhe_stime[0]
       << ", filhe_stime[1]=" << fileHeader.filhe_stime[1]
       << ", filhe_label=" << fileHeader.filhe_label << ", filhe_file=" << fileHeader.filhe_file
       << ", filhe_user=" << fileHeader.filhe_user << ", filhe_time=" << fileHeader.filhe_time
       << ", filhe_run=" << fileHeader.filhe_run << ", filhe_exp=" << fileHeader.filhe_exp;
    for (size_t i = 0; i < fileHeader.filhe_lines; ++i)
        os << ", filhe_strings[" << i << "]=" << fileHeader.s_strings[i].string;
    os << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const s_bufhe &buf)
{
    os << "s_bufhe { l_dlen=" << buf.l_dlen << ", i_type=" << buf.i_type
       << ", i_subtype=" << buf.i_subtype << ", h_begin=" << static_cast<int>(buf.h_begin)
       << ", h_end=" << static_cast<int>(buf.h_end) << ", i_used=" << buf.i_used
       << ", l_buf=" << buf.l_buf << ", l_evt=" << buf.l_evt << ", l_current_i=" << buf.l_current_i
       << ", l_time[0]=" << buf.l_time[0] << ", l_time[1]=" << buf.l_time[1] << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const s_ve10_1 &event)
{
    os << "s_ve10_1 { l_dlen=" << event.l_dlen << ", i_type=" << event.i_type
       << ", i_subtype=" << event.i_subtype << ", i_trigger=" << event.i_trigger
       << ", i_dummy=" << event.i_dummy << ", l_count=" << event.l_count << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const s_ves10_1 &event)
{
    os << "s_ves10_1 { l_dlen=" << event.l_dlen << ", i_type=" << event.i_type
       << ", i_subtype=" << event.i_subtype << ", i_procid=" << event.i_procid
       << ", h_subcrate=" << static_cast<int>(event.h_subcrate)
       << ", h_control=" << static_cast<int>(event.h_control) << " }";
    return os;
}

namespace mesytec::mvme::mbs
{

LmdQuickInfo scan_lmd_file(const std::string &lmdFilename)
{
    LmdWrapper lmd;
    lmd.open(lmdFilename, false);
    LmdQuickInfo result{};
    result.fileHeader = *lmd.fileHeaderPtr;

    while (lmd.nextEvent())
    {
        ++result.triggerCounts[lmd.eventPtr->i_trigger];
        if (result.firstEventNumber == 0)
            result.firstEventNumber = lmd.eventPtr->l_count;

        while (lmd.nextSubEvent())
        {
            result.subcrates.insert(lmd.subEventPtr->h_subcrate);
            ++result.subEventCount;
        }
        ++result.eventCount;
    }

    if (lmd.eventPtr)
        result.lastEventNumber = lmd.eventPtr->l_count;

    return result;
}

std::ostream &operator<<(std::ostream &os, const LmdQuickInfo &info)
{
    os << "mesytec::mvme::mbs::LmdQuickInfo { "
       << "triggerCounts={";
    for (const auto &[trigger, count]: info.triggerCounts)
        os << " " << trigger << ":" << count;
    os << " }, "
       << "subcrates={";
    for (const auto &subcrate: info.subcrates)
        os << " " << subcrate;
    os << " }, "
       << "firstEventNumber=" << info.firstEventNumber << ", "
       << "lastEventNumber=" << info.lastEventNumber << ", "
       << "eventCount=" << info.eventCount << ", "
       << "subEventCount=" << info.subEventCount << " }";
    return os;
}

} // namespace mesytec::mvme::mbs
