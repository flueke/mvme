#ifndef A767D065_00E7_42EF_BC55_0E55962EA4D3
#define A767D065_00E7_42EF_BC55_0E55962EA4D3

#include <cstddef>
#include <graphviz/gvc.h>
#include <graphviz/graphviz_version.h>

#if defined(GRAPHVIZ_VERSION_MAJOR) && GRAPHVIZ_VERSION_MAJOR >= 13

// Compatible with the old agstrfree signature. Assumes the strings are html by
// default. No idea and no time to look for the info and motivation for the
// change.
inline int agstrfree(Agraph_t *g, const char *str) { return agstrfree(g, str, true); }

#endif

// gvRenderData() 'length' changed from unsigned int* to size_t*. The version
// macros are not available in all versions, so the length type
// is deduced from the declared function signature instead.
namespace graphviz_compat_detail
{
template <typename LengthT>
int render_data(int (*fn)(GVC_t *, graph_t *, const char *, char **, LengthT *), GVC_t *gvc,
                graph_t *g, const char *format, char **result, size_t *length)
{
    LengthT len = 0;
    int ret = fn(gvc, g, format, result, &len);
    if (length)
        *length = static_cast<size_t>(len);
    return ret;
}
} // namespace graphviz_compat_detail

inline int gv_render_data(GVC_t *gvc, graph_t *g, const char *format, char **result,
                          size_t *length)
{
    return graphviz_compat_detail::render_data(gvRenderData, gvc, g, format, result, length);
}

#endif /* A767D065_00E7_42EF_BC55_0E55962EA4D3 */
