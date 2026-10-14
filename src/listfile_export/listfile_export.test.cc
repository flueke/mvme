#include <cmath>
#include <gtest/gtest.h>
#include <sstream>

#include "listfile_export/listfile_export.h"

using namespace mesytec;
using namespace mesytec::mvme::listfile_export;

namespace
{

FilterDb make_test_db()
{
    FilterDb db;
    db.modules.push_back({"mdpp16", "mesytec", "MDPP-16", {{"generic", "0001XXXXXXXXAAAADDDDDDDDDDDDDDDD"}}});
    db.modules.push_back({"mdpp16_scp",
                          "mesytec",
                          "MDPP-16_SCP",
                          {{"amplitude", "0001XXXXPO00AAAADDDDDDDDDDDDDDDD"},
                           {"channel_time", "0001XXXXXX01AAAADDDDDDDDDDDDDDDD"},
                           {"module_timestamp", "11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"}}});
    return db;
}

mvlc::CrateConfig make_test_crate_config()
{
    mvlc::CrateConfig cc;

    mvlc::StackCommandBuilder ev0("ev0");
    ev0.beginGroup("my_mdpp", {{"vme_module_type", "mdpp16_scp"}});
    ev0.addVMEBlockRead(0x00000000, 0x08, 0xffff);
    ev0.beginGroup("mdpp16_scp_1"); // no meta, matched by name
    ev0.addVMEBlockRead(0x00010000, 0x08, 0xffff);
    ev0.beginGroup("foo", {{"vme_module_type", "unknown_type"}});
    ev0.addVMEBlockRead(0x00020000, 0x08, 0xffff);
    cc.stacks.push_back(ev0);

    mvlc::StackCommandBuilder ev1("ev1");
    ev1.beginGroup("other_mdpp", {{"vme_module_type", "mdpp16_scp"}});
    ev1.addVMEBlockRead(0x00030000, 0x08, 0xffff);
    cc.stacks.push_back(ev1);

    return cc;
}

u32 amplitude_word(u32 channel, u32 value) { return (0b0001u << 28) | (channel << 16) | value; }
u32 time_word(u32 channel, u32 value) { return (0b0001u << 28) | (0b01u << 20) | (channel << 16) | value; }

mvlc::readout_parser::ModuleData make_module_data(const std::vector<u32> &data)
{
    mvlc::readout_parser::ModuleData md{};
    md.data = {data.data(), static_cast<u32>(data.size())};
    md.dynamicSize = data.size();
    md.hasDynamic = true;
    return md;
}

} // namespace

TEST(ListfileExport, Model)
{
    auto model = make_listfile_model(make_test_crate_config(), make_test_db());

    ASSERT_EQ(model.events.size(), 2u);
    ASSERT_EQ(model.events[0].modules.size(), 3u);

    const auto &m0 = model.events[0].modules[0];
    ASSERT_EQ(m0.match, ModuleMatch::ByMeta);
    ASSERT_EQ(m0.typeName, "mdpp16_scp");
    ASSERT_EQ(m0.arrays.size(), 3u);
    ASSERT_EQ(m0.arrays[0].size, 16u);
    ASSERT_EQ(m0.arrays[2].size, 1u);

    const auto &m1 = model.events[0].modules[1];
    ASSERT_EQ(m1.match, ModuleMatch::ByName);
    ASSERT_EQ(m1.typeName, "mdpp16_scp"); // longest match wins over 'mdpp16'

    const auto &m2 = model.events[0].modules[2];
    ASSERT_EQ(m2.match, ModuleMatch::UnknownType);
    ASSERT_TRUE(m2.arrays.empty());

    ASSERT_EQ(array_path(model, 0, 0, 0), "ev0.my_mdpp.amplitude");
}

TEST(ListfileExport, ResolveSelectors)
{
    auto model = make_listfile_model(make_test_crate_config(), make_test_db());

    {
        auto r = resolve_selectors(model, {"ev0.my_mdpp.channel_time", "ev0.mdpp16_scp_1.amplitude"});
        ASSERT_TRUE(r.selection);
        ASSERT_EQ(r.selection->eventIndex, 0u);
        ASSERT_EQ(r.selection->arrays.size(), 2u);
        ASSERT_EQ(r.selection->arrays[0].moduleIndex, 0u);
        ASSERT_EQ(r.selection->arrays[0].arrayIndex, 1u);
        ASSERT_EQ(r.selection->arrays[1].moduleIndex, 1u);
        ASSERT_EQ(r.selection->arrays[1].columnOffset, 16u);
        ASSERT_EQ(r.selection->columnNames.size(), 32u);
        ASSERT_EQ(r.selection->columnNames[0], "ev0.my_mdpp.channel_time[0]");
        ASSERT_EQ(r.selection->columnNames[31], "ev0.mdpp16_scp_1.amplitude[15]");
    }

    ASSERT_FALSE(resolve_selectors(model, {"ev0.my_mdpp.nope"}).selection);
    ASSERT_FALSE(resolve_selectors(model, {}).selection);
    ASSERT_FALSE(resolve_selectors(model, {"ev0.my_mdpp.amplitude", "ev1.other_mdpp.amplitude"}).selection);
}

TEST(ListfileExport, Export)
{
    auto model = make_listfile_model(make_test_crate_config(), make_test_db());
    auto sel = resolve_selectors(model, {"ev0.my_mdpp.amplitude", "ev0.my_mdpp.module_timestamp"}).selection;
    ASSERT_TRUE(sel);

    std::ostringstream out;
    TextRowWriter writer(out, {" ", "nan", false});
    Exporter exporter(model, *sel, writer);

    std::vector<u32> data0 = {amplitude_word(0, 100), amplitude_word(15, 200), time_word(3, 42),
                              (0b11u << 30) | 0x3fffffffu};
    std::vector<u32> data1 = {time_word(0, 1)}; // no amplitudes, no timestamp
    std::vector<u32> data2 = {amplitude_word(1, 5), amplitude_word(1, 6)};
    std::vector<u32> empty;

    exporter.begin();

    auto process = [&](int ei, const std::vector<u32> &d)
    {
        std::vector<mvlc::readout_parser::ModuleData> mds = {make_module_data(d), make_module_data(empty),
                                                             make_module_data(empty)};
        exporter.processEvent(ei, mds.data(), mds.size());
    };

    process(0, data0);
    process(1, data0); // other event, ignored
    process(0, data1);
    process(0, data2);
    exporter.end();

    const auto &c = exporter.counters();
    ASSERT_EQ(c.eventsSeen, 3u);
    ASSERT_EQ(c.rowsWritten, 3u);
    ASSERT_EQ(c.emptyRows, 1u);
    ASSERT_EQ(c.overwrittenHits, 1u);

    std::istringstream in(out.str());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line))
        lines.push_back(line);

    ASSERT_EQ(lines.size(), 3u);
    ASSERT_EQ(lines[0], "100 nan nan nan nan nan nan nan nan nan nan nan nan nan nan 200 1073741823");
    ASSERT_EQ(lines[1], "nan nan nan nan nan nan nan nan nan nan nan nan nan nan nan nan nan");
    ASSERT_EQ(lines[2], "nan 6 nan nan nan nan nan nan nan nan nan nan nan nan nan nan nan");
}

TEST(ListfileExport, CsvHeaderAndSkipEmpty)
{
    auto model = make_listfile_model(make_test_crate_config(), make_test_db());
    auto sel = resolve_selectors(model, {"ev0.my_mdpp.module_timestamp"}).selection;
    ASSERT_TRUE(sel);

    std::ostringstream out;
    TextRowWriter writer(out, {",", "", true});
    Exporter exporter(model, *sel, writer, {true});

    std::vector<u32> data = {(0b11u << 30) | 1234};
    std::vector<u32> empty;
    std::vector<mvlc::readout_parser::ModuleData> mds0 = {make_module_data(data)};
    std::vector<mvlc::readout_parser::ModuleData> mds1 = {make_module_data(empty)};

    exporter.begin();
    exporter.processEvent(0, mds0.data(), mds0.size());
    exporter.processEvent(0, mds1.data(), mds1.size());
    exporter.end();

    ASSERT_EQ(out.str(), "ev0.my_mdpp.module_timestamp[0]\n1234\n");
    ASSERT_EQ(exporter.counters().emptyRows, 1u);
    ASSERT_EQ(exporter.counters().rowsWritten, 1u);
}

TEST(ListfileExport, ModuleTypeHints)
{
    auto cc = make_test_crate_config();
    cc.stacks[0].getGroup(1).name = "renamed";

    ModuleTypeHints hints = {{"", "mdpp16_scp", "mdpp16_scp"}};
    auto model = make_listfile_model(cc, make_test_db(), hints);

    ASSERT_EQ(model.events[0].modules[0].match, ModuleMatch::ByMeta);
    ASSERT_EQ(model.events[0].modules[1].match, ModuleMatch::ByHint);
    ASSERT_EQ(model.events[0].modules[1].arrays.size(), 3u);
    ASSERT_EQ(model.events[0].modules[2].match, ModuleMatch::UnknownType); // meta takes precedence
}
