#include "graphviz_util.h"

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <mutex>
#include <QBoxLayout>
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QSplitter>
#include <QGroupBox>
#include <spdlog/spdlog.h>
#include <sstream>

#include "analysis/code_editor.h"
#include "qt_util.h"
#include "graphicsview_util.h"

namespace mesytec
{
namespace graphviz_util
{

namespace
{
    static std::mutex g_mutex;
    static std::stringstream g_errorBuffer;

    int my_gv_error_function(char *msg)
    {
        g_errorBuffer << msg;
        return 0;
    }
}

std::string layout_and_render_dot(
    const char *dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    std::lock_guard<std::mutex> guard(g_mutex);

    g_errorBuffer.str({}); // clear the error buffer
    agseterrf(my_gv_error_function);

    std::string result;

    if (GVC_t *gvc = gvContext())
    {
        if (Agraph_t *g = agmemread(dotCode))
        {
            if (gvLayout(gvc, g, layoutEngine) >= 0)
            {
                char *renderDest = nullptr;
                unsigned int renderSize = 0;
                gvRenderData(gvc, g, outputFormat, &renderDest, &renderSize);

                result = { renderDest ? renderDest : "" };

                gvFreeRenderData(renderDest);
                gvFreeLayout(gvc, g);
            }

            agclose(g);
        }

        gvFreeContext(gvc);
    }

    auto errors = g_errorBuffer.str();

    if (!errors.empty())
        spdlog::error("graphviz: {}", errors);

    return result;
}

std::string get_error_buffer()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    return g_errorBuffer.str();
}

void clear_error_buffer()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    g_errorBuffer.str({});
}

QByteArray layout_and_render_dot_q(
    const QString &dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    return QByteArray(
        layout_and_render_dot(
            dotCode.toLocal8Bit().data(),
            layoutEngine, outputFormat)
            .c_str());
}

QByteArray layout_and_render_dot_q(
    const std::string &dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    return QByteArray(
        layout_and_render_dot(
            dotCode.c_str(),
            layoutEngine, outputFormat)
            .c_str());
}

QString get_error_buffer_q()
{
    return QString::fromStdString(get_error_buffer());
}

QDomElement find_first_basic_svg_shape_element(const QDomNode &root)
{
    static const std::set<QString> SvgBasicShapes = {
        "circle", "ellipse", "line", "polygon", "polyline", "rect"
    };

    QDomElement result;

    auto f = [&result] (const QDomNode &node, int) -> DomVisitResult
    {
        if (!result.isNull())
            return DomVisitResult::Stop;

        auto e = node.toElement();

        if (!e.isNull() && SvgBasicShapes.count(e.tagName()))
        {
            result = e;
            return DomVisitResult::Stop;
        }

        return DomVisitResult::Continue;
    };

    visit_dom_nodes(root, f);

    return result;
}

QDomElement find_element_by_predicate(
    const QDomNode &root,
    const std::function<bool (const QDomElement &e, int depth)> &predicate)
{
    QDomElement result;

    auto visitor = [&] (const QDomNode &n, int depth) -> DomVisitResult
    {
        if (!result.isNull())
           return DomVisitResult::Stop;

        auto e = n.toElement();

        if (!e.isNull() && predicate(e, depth))
        {
            result = e;
            return DomVisitResult::Stop;
        }

        return DomVisitResult::Continue;
    };

    visit_dom_nodes(root, visitor);

    return result;
}

std::vector<std::unique_ptr<DomElementSvgItem>> create_svg_graphics_items(
    const QByteArray &svgData,
    const DomAndRenderer &dr,
    const std::set<QString> &acceptedElementClasses)
{
    std::vector<std::unique_ptr<DomElementSvgItem>> result;

    QXmlStreamReader xmlReader(svgData);
    while (!xmlReader.atEnd())
    {
        switch (xmlReader.readNext())
        {
            case QXmlStreamReader::TokenType::StartElement:
            {
                auto attributes = xmlReader.attributes();
                auto elementClass = attributes.value("class").toString();

                if (!acceptedElementClasses.count(elementClass))
                    continue;

                if (attributes.hasAttribute("id"))
                {
                    auto elementId = attributes.value("id").toString();
                    auto svgItem = std::make_unique<DomElementSvgItem>(dr, elementId);
                    result.emplace_back(std::move(svgItem));
                }
            } break;

            default:
                break;
        }
    }

    return result;
}

void DotGraphicsSceneManager::setDot(const std::string &dotStr)
{
    m_scene->clear();

    m_dotErrorBuffer = {};
    m_dotStr = dotStr;
    m_svgData = layout_and_render_dot_q(m_dotStr);
    m_dotErrorBuffer = get_error_buffer();
    m_dr = { m_svgData };
    auto items = create_svg_graphics_items(m_svgData, m_dr);

    for (auto &item: items)
        m_scene->addItem(item.release());

    // For the scene to recalculate the scene rect based on the items present.
    // This is the only way to actually shrink the scene rect.
    m_scene->setSceneRect(m_scene->itemsBoundingRect());
}

struct DotWidget::Private
{
    DotGraphicsSceneManager manager_;
    QGraphicsView *view_;
    CodeEditor *dotEditor_;
    CodeEditor *svgEditor_;

    void onDotTextChanged()
    {
        manager_.setDot(dotEditor_->toPlainText());
        svgEditor_->setPlainText(manager_.svgData());
    }
};

DotWidget::DotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->view_ = new QGraphicsView;
    d->dotEditor_ = new CodeEditor;
    d->svgEditor_ = new CodeEditor;
    new MouseWheelZoomer(d->view_, d->view_);

    d->view_->setScene(d->manager_.scene());
    d->view_->setRenderHints(
        QPainter::Antialiasing | QPainter::TextAntialiasing |
        QPainter::SmoothPixmapTransform | QPainter::HighQualityAntialiasing);
    d->view_->setDragMode(QGraphicsView::ScrollHandDrag);
    d->view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    d->dotEditor_->setWordWrapMode(QTextOption::WordWrap);
    d->dotEditor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    d->svgEditor_->setWordWrapMode(QTextOption::WordWrap);
    d->svgEditor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    d->svgEditor_->setReadOnly(true);

    auto gb_dotEditor = new QGroupBox("dot");
    auto l_dotEditor = make_hbox<0, 0>(gb_dotEditor);
    l_dotEditor->addWidget(d->dotEditor_);

    auto gb_svgEditor = new QGroupBox("svg");
    auto l_svgEditor = make_hbox<0, 0>(gb_svgEditor);
    l_svgEditor->addWidget(d->svgEditor_);

    auto gb_gfxView = new QGroupBox("graphviz");
    auto l_gfxView = make_hbox<0, 0>(gb_gfxView);
    l_gfxView->addWidget(d->view_);

    auto vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->addWidget(gb_dotEditor);
    vSplitter->addWidget(gb_svgEditor);

    auto hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->addWidget(vSplitter);
    hSplitter->addWidget(gb_gfxView);

    auto l = make_hbox<2, 2>(this);
    l->addWidget(hSplitter);

    connect(d->dotEditor_, &CodeEditor::textChanged,
            this, [this] () { d->onDotTextChanged(); });
}

DotWidget::~DotWidget()
{
}

}
}
