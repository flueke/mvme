#ifndef __MVME_GRAPHVIZ_UTIL_H__
#define __MVME_GRAPHVIZ_UTIL_H__

#include <QByteArray>
#include <QString>

namespace mesytec
{
namespace graphviz_util
{
    // Relevant documentation:
    // - https://graphviz.org/pdf/libguide.pdf
    // - https://graphviz.org/pdf/cgraph.pdf

    // Does the gvLayout and gvRenderData steps. Not optimized at all as it
    // creates and destroys a gvContext. Uses a global mutex to ensure the
    // function is not invoked concurrently. Uses a global error buffer to store
    // graphviz generated warnings and errors.
    std::string layout_and_render_dot(
        const char *dotCode,
        const char *layoutEngine = "dot",
        const char *outputFormat = "svg");

    std::string get_error_buffer();

    // Qt container versions of the above functions. Even slower as internally
    // the non-qt versions are used and the results converted.
    QByteArray layout_and_render_dot_q(
        const QString &dotCode,
        const char *layoutEngine = "dot",
        const char *outputFormat = "svg");

    QString get_error_buffer_q();
}
}

#endif /* __MVME_GRAPHVIZ_UTIL_H__ */
