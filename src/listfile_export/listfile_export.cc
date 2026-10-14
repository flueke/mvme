#include "listfile_export/listfile_export.h"

#include <cmath>
#include <limits>

namespace mesytec::mvme::listfile_export
{

const ModuleDef *find_module_def(const FilterDb &db, const std::string &typeName)
{
    for (const auto &md: db.modules)
    {
        if (md.typeName == typeName)
            return &md;
    }
    return nullptr;
}

const char *to_string(ModuleMatch m)
{
    switch (m)
    {
    case ModuleMatch::ByMeta:
        return "matched by vme_module_type";
    case ModuleMatch::ByHint:
        return "matched by mvme config";
    case ModuleMatch::ByName:
        return "matched by module name";
    case ModuleMatch::UnknownType:
        return "unknown module type";
    case ModuleMatch::NoMatch:
        return "no module type info";
    }
    return "";
}

namespace
{

// Longest type name contained in the module name, so that 'mdpp16_scp' wins over 'mdpp16'.
const ModuleDef *match_by_name(const FilterDb &db, const std::string &moduleName)
{
    const ModuleDef *result = nullptr;

    for (const auto &md: db.modules)
    {
        if (!md.typeName.empty() && moduleName.find(md.typeName) != std::string::npos &&
            (!result || md.typeName.size() > result->typeName.size()))
        {
            result = &md;
        }
    }

    return result;
}

ArrayInfo make_array_info(const DataSourceDef &src)
{
    ArrayInfo ai;
    ai.name = src.name;
    ai.filterString = src.filter;
    ai.filter = mvlc::util::make_filter(src.filter);
    ai.fAddress = mvlc::util::make_cache_entry(ai.filter, 'A');
    ai.fValue = mvlc::util::make_cache_entry(ai.filter, 'D');
    ai.size = static_cast<size_t>(1u) << ai.fAddress.extractBits;
    return ai;
}

} // namespace

ListfileModel make_listfile_model(const mvlc::CrateConfig &crateConfig, const FilterDb &db,
                                  const ModuleTypeHints &hints)
{
    ListfileModel model;

    auto get_hint = [&hints](size_t si, size_t gi) -> std::string
    { return si < hints.size() && gi < hints[si].size() ? hints[si][gi] : std::string{}; };

    for (size_t si = 0; si < crateConfig.stacks.size(); ++si)
    {
        const auto &stack = crateConfig.stacks[si];
        EventInfo eventInfo;
        eventInfo.name = stack.getName();
        const auto groups = stack.getGroups();

        for (size_t gi = 0; gi < groups.size(); ++gi)
        {
            const auto &group = groups[gi];
            ModuleInfo mi;
            mi.name = group.name;
            mi.meta = group.meta;
            mi.producesOutput = mvlc::produces_output(group);

            const ModuleDef *def = nullptr;

            if (auto it = group.meta.find("vme_module_type"); it != group.meta.end())
            {
                mi.typeName = it->second;
                def = find_module_def(db, mi.typeName);
                mi.match = def ? ModuleMatch::ByMeta : ModuleMatch::UnknownType;
            }
            else if (auto hint = get_hint(si, gi); !hint.empty())
            {
                mi.typeName = hint;
                def = find_module_def(db, mi.typeName);
                mi.match = def ? ModuleMatch::ByHint : ModuleMatch::UnknownType;
            }
            else if ((def = match_by_name(db, group.name)))
            {
                mi.typeName = def->typeName;
                mi.match = ModuleMatch::ByName;
            }

            if (def && mi.producesOutput)
            {
                for (const auto &src: def->dataSources)
                    mi.arrays.emplace_back(make_array_info(src));
            }

            eventInfo.modules.emplace_back(std::move(mi));
        }

        model.events.emplace_back(std::move(eventInfo));
    }

    return model;
}

std::string array_path(const ListfileModel &model, size_t ei, size_t mi, size_t ai)
{
    const auto &event = model.events.at(ei);
    const auto &module_ = event.modules.at(mi);
    return event.name + "." + module_.name + "." + module_.arrays.at(ai).name;
}

SelectionResult resolve_selectors(const ListfileModel &model, const std::vector<std::string> &selectors)
{
    SelectionResult result;

    if (selectors.empty())
    {
        result.error = "no arrays selected";
        return result;
    }

    Selection sel;
    std::optional<size_t> eventIndex;
    size_t columnOffset = 0;

    for (const auto &selector: selectors)
    {
        bool found = false;

        for (size_t ei = 0; ei < model.events.size() && !found; ++ei)
        {
            for (size_t mi = 0; mi < model.events[ei].modules.size() && !found; ++mi)
            {
                const auto &arrays = model.events[ei].modules[mi].arrays;

                for (size_t ai = 0; ai < arrays.size() && !found; ++ai)
                {
                    if (array_path(model, ei, mi, ai) != selector)
                        continue;

                    found = true;

                    if (eventIndex && *eventIndex != ei)
                    {
                        result.error = "selected arrays belong to different events: '" + selectors.front() +
                                       "' and '" + selector + "'";
                        return result;
                    }

                    eventIndex = ei;
                    sel.arrays.push_back({mi, ai, columnOffset});
                    columnOffset += arrays[ai].size;

                    for (size_t i = 0; i < arrays[ai].size; ++i)
                        sel.columnNames.emplace_back(selector + "[" + std::to_string(i) + "]");
                }
            }
        }

        if (!found)
        {
            result.error = "no such array: '" + selector + "'";
            return result;
        }
    }

    sel.eventIndex = *eventIndex;
    result.selection = std::move(sel);
    return result;
}

TextRowWriter::TextRowWriter(std::ostream &out, const TextWriterOptions &opts)
    : out_(out)
    , opts_(opts)
{
}

void TextRowWriter::begin(const std::vector<std::string> &columnNames)
{
    if (!opts_.header)
        return;

    line_.clear();

    for (size_t i = 0; i < columnNames.size(); ++i)
    {
        if (i > 0)
            line_ += opts_.separator;
        line_ += columnNames[i];
    }

    line_ += '\n';
    out_ << line_;
}

void TextRowWriter::writeRow(const std::vector<double> &row)
{
    line_.clear();

    for (size_t i = 0; i < row.size(); ++i)
    {
        if (i > 0)
            line_ += opts_.separator;

        if (std::isnan(row[i]))
            line_ += opts_.missing;
        else
            line_ += std::to_string(static_cast<u64>(row[i]));
    }

    line_ += '\n';
    out_ << line_;
}

void TextRowWriter::end()
{
    out_.flush();
}

Exporter::Exporter(const ListfileModel &model, const Selection &sel, IRowWriter &writer, const ExportOptions &opts)
    : model_(model)
    , sel_(sel)
    , writer_(writer)
    , opts_(opts)
    , row_(sel.columnNames.size())
{
}

void Exporter::begin()
{
    counters_ = {};
    writer_.begin(sel_.columnNames);
}

void Exporter::processEvent(int eventIndex, const mvlc::readout_parser::ModuleData *moduleDataList,
                            unsigned moduleCount)
{
    if (eventIndex < 0 || static_cast<size_t>(eventIndex) != sel_.eventIndex)
        return;

    ++counters_.eventsSeen;
    std::fill(std::begin(row_), std::end(row_), std::numeric_limits<double>::quiet_NaN());
    bool hasValues = false;

    const auto &eventInfo = model_.events.at(sel_.eventIndex);

    for (const auto &sa: sel_.arrays)
    {
        if (sa.moduleIndex >= moduleCount)
            continue;

        const auto &md = moduleDataList[sa.moduleIndex];
        const auto data = md.hasDynamic ? mvlc::readout_parser::dynamic_span(md)
                                        : mvlc::readout_parser::prefix_span(md);
        const auto &ai = eventInfo.modules.at(sa.moduleIndex).arrays.at(sa.arrayIndex);
        double *dest = row_.data() + sa.columnOffset;

        for (const u32 *word = data.data, *end = data.data + data.size; word < end; ++word)
        {
            if (!mvlc::util::matches(ai.filter, *word))
                continue;

            u32 address = mvlc::util::extract(ai.fAddress, *word);
            u32 value = mvlc::util::extract(ai.fValue, *word);

            if (!std::isnan(dest[address]))
                ++counters_.overwrittenHits;

            dest[address] = value;
            hasValues = true;
        }
    }

    if (!hasValues)
    {
        ++counters_.emptyRows;
        if (opts_.skipEmptyRows)
            return;
    }

    writer_.writeRow(row_);
    ++counters_.rowsWritten;
}

void Exporter::end()
{
    writer_.end();
}

} // namespace mesytec::mvme::listfile_export
