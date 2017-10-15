#ifndef __MULTIWORD_DATAFILTER_H__
#define __MULTIWORD_DATAFILTER_H__

namespace data_filter
{

static const s32 MaxFilters = 16;

struct MultiWordFilter
{
    std::array<DataFilter, MaxFilters> filters;
    std::array<u32, MaxFilters> results;
    std::array<cacheEntry, MaxFilters> cacheA;
    std::array<cacheEntry, MaxFilters> cacheD;
    u32 filterCount;
    u16 completionMask = 0;

    static_assert(sizeof(completionMask) * 8 >= MaxFilters, "completionMask type too small");
};

inline void clear_completion(MultiWordFilter *filter)
{
    filter->completionMask = 0;
}

u32 add_subfilter(MultiWordFilter *filter, DataFilter subfilter)
{
    if (filter->filterCount >= MaxFilters)
        throw std::out_of_range("filter count exceeded");

    filter->filters[filter->filterCount] = subfilter;
    filter->cacheA[filter->filterCount] = make_cache_entry(subfilter, 'A');
    filter->cacheD[filter->filterCount] = make_cache_entry(subfilter, 'D');
    filter->filterCount++;
    clear_completion(filter);

    return filter->filterCount;
}

inline bool is_complete(MultiWordFilter *filter)
{
    return number_of_set_bits(filter->completionMask) == filter->filterCount;
}

};

#endif /* __MULTIWORD_DATAFILTER_H__ */
