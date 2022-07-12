#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstdlib>

static const char *inputFilename = "foo.dot";
static const char *outputFilename = "foo_rendered.svg";
static const char *layoutEngine = "dot";
static const char *outputFormat = "svg";

static std::stringstream g_errorBuffer;

int my_gv_error_function(char *msg)
{
    g_errorBuffer << msg;
    return 0;
}

void dump_gv_error_buffer(const char *prefix, bool clearBuffer = true)
{
    auto str = g_errorBuffer.str();

    if (!str.empty())
        std::cerr << prefix << str << std::endl;

    if (clearBuffer)
        g_errorBuffer.str({});
}

int main(int argc, char *argv[])
{
    std::ifstream dotIn(inputFilename);
    dotIn.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    std::stringstream dotBuf;
    dotBuf << dotIn.rdbuf();

    agseterrf(my_gv_error_function);
    GVC_t *gvc = gvContext();
    assert(gvc);

    Agraph_t *g = agmemread(dotBuf.str().c_str());
    dump_gv_error_buffer("gv error buffer after agmemread(): ");
    assert(g);

    gvLayout(gvc, g, layoutEngine);
    dump_gv_error_buffer("gv error buffer after gvLayout(): ");

    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, outputFormat, &renderDest, &renderSize);
    dump_gv_error_buffer("gv error buffer after gvRenderData(): ");
    assert(renderDest);

    std::ofstream svgOut(outputFilename);
    svgOut.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    svgOut << renderDest;

    gvFreeRenderData(renderDest);
    gvFreeLayout(gvc, g);
    agclose(g);
    gvFreeContext(gvc);


    return 0;
}