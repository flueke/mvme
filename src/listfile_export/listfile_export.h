#ifndef MVME_LISTFILE_EXPORT_H
#define MVME_LISTFILE_EXPORT_H

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Extraction of module data arrays from MVLC listfiles using data filters.
// Independent of Qt and the mvme analysis. Filters are supplied via a FilterDb.

namespace mesytec::mvme::listfile_export
{

using mvlc::u32;
using mvlc::u64;

// Filter database: module type name -> named data filters.

struct DataSourceDef
{
    std::string name;
    std::string filter;
};

struct ModuleDef
{
    std::string typeName;
    std::string vendorName;
    std::string displayName;
    std::vector<DataSourceDef> dataSources;
};

struct FilterDb
{
    std::vector<ModuleDef> modules;
};

const ModuleDef *find_module_def(const FilterDb &db, const std::string &typeName);

// Model of the events, modules and arrays available in a listfile.

struct ArrayInfo
{
    std::string name;
    std::string filterString;
    size_t size = 0;
    mvlc::util::DataFilter filter;
    mvlc::util::CacheEntry fAddress;
    mvlc::util::CacheEntry fValue;
};

enum class ModuleMatch
{
    ByMeta,      // matched via the 'vme_module_type' stack group meta info
    ByHint,      // type taken from external hints, e.g. the MVMEConfig stored in the listfile
    ByName,      // fallback: module name contains the module type name
    UnknownType, // type from meta info not found in the filter db
    NoMatch,     // no meta info and no name match
};

const char *to_string(ModuleMatch m);

struct ModuleInfo
{
    std::string name;
    std::string typeName; // empty if unknown
    ModuleMatch match = ModuleMatch::NoMatch;
    bool producesOutput = false;
    std::map<std::string, std::string> meta;
    std::vector<ArrayInfo> arrays;
};

struct EventInfo
{
    std::string name;
    std::vector<ModuleInfo> modules;
};

struct ListfileModel
{
    std::vector<EventInfo> events;
};

// Optional module type names indexed by [stackIndex][groupIndex]. Used for groups without
// 'vme_module_type' meta info. Empty strings mean no hint.
using ModuleTypeHints = std::vector<std::vector<std::string>>;

ListfileModel make_listfile_model(const mvlc::CrateConfig &crateConfig, const FilterDb &db,
                                  const ModuleTypeHints &hints = {});

// Full path of an array: "<event>.<module>.<array>"
std::string array_path(const ListfileModel &model, size_t ei, size_t mi, size_t ai);

// Resolved array selectors. All selected arrays must belong to the same event.
//
// Selector syntax: "<event>.<module>.<array>" selects the whole array,
// "<event>.<module>.<array>[<indices>]" selects single elements. <indices> is a comma separated
// list of element indices and inclusive ranges, e.g. "[3]", "[0,2,5]", "[0-3,8,12-15]".
// Columns are written in the given order.

struct ParsedSelector
{
    std::string path;
    std::optional<std::vector<size_t>> indices; // unset: whole array
};

struct ParseSelectorResult
{
    std::optional<ParsedSelector> selector;
    std::string error;
};

ParseSelectorResult parse_selector(const std::string &selector);

struct SelectedArray
{
    size_t moduleIndex;
    size_t arrayIndex;
    // Row column for each array address, -1 if the address is not selected.
    std::vector<int> addressToColumn;
};

struct Selection
{
    size_t eventIndex = 0;
    std::vector<SelectedArray> arrays;
    std::vector<std::string> columnNames;
};

struct SelectionResult
{
    std::optional<Selection> selection;
    std::string error;
};

SelectionResult resolve_selectors(const ListfileModel &model, const std::vector<std::string> &selectors);

// Output: one row per event, missing values are NaN.

struct IRowWriter
{
    virtual ~IRowWriter() = default;
    virtual void begin(const std::vector<std::string> &columnNames) = 0;
    virtual void writeRow(const std::vector<double> &row) = 0;
    virtual void end() = 0;
};

struct TextWriterOptions
{
    std::string separator = ",";
    std::string missing = "";
    bool header = true;
};

// CSV and other separator based plain text formats.
class TextRowWriter: public IRowWriter
{
  public:
    TextRowWriter(std::ostream &out, const TextWriterOptions &opts);
    void begin(const std::vector<std::string> &columnNames) override;
    void writeRow(const std::vector<double> &row) override;
    void end() override;

  private:
    std::ostream &out_;
    TextWriterOptions opts_;
    std::string line_;
};

struct ExportCounters
{
    size_t eventsSeen = 0;      // events matching the selected event index
    size_t rowsWritten = 0;
    size_t emptyRows = 0;       // rows where all values are NaN
    size_t overwrittenHits = 0; // same array address hit more than once in one event
};

struct ExportOptions
{
    bool skipEmptyRows = false;
};

class Exporter
{
  public:
    Exporter(const ListfileModel &model, const Selection &sel, IRowWriter &writer, const ExportOptions &opts = {});

    void begin();
    void processEvent(int eventIndex, const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount);
    void end();

    const ExportCounters &counters() const { return counters_; }

  private:
    const ListfileModel &model_;
    Selection sel_;
    IRowWriter &writer_;
    ExportOptions opts_;
    ExportCounters counters_;
    std::vector<double> row_;
};

} // namespace mesytec::mvme::listfile_export

#endif /* MVME_LISTFILE_EXPORT_H */
