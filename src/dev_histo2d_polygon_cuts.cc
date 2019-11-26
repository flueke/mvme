#include <iostream>
#include <list>

/* Circumvent compile errors related to the 'Q' numeric literal suffix.
 * See https://svn.boost.org/trac10/ticket/9240 and
 * https://www.boost.org/doc/libs/1_68_0/libs/math/doc/html/math_toolkit/config_macros.html
 * for details. */
#define BOOST_MATH_DISABLE_FLOAT128
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

#include <QApplication>
#include "histo2d_widget.h"
#include "qt_util.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto histo = std::make_shared<Histo2D>(
        100, 0.0, 100.0,
        100, 0.0, 100.0);

    Histo2DWidget histoWidget(histo);

    add_widget_close_action(&histoWidget);
    histoWidget.resize(1000, 800);
    histoWidget.show();

    return app.exec();
}

#if 0
int main()
{
    typedef boost::geometry::model::d2::point_xy<double> point_type;
    typedef boost::geometry::model::polygon<point_type> polygon_type;

    polygon_type poly;
    boost::geometry::read_wkt(
        "POLYGON((2 1.3,2.4 1.7,2.8 1.8,3.4 1.2,3.7 1.6,3.4 2,4.1 3,5.3 2.6,5.4 1.2,4.9 0.8,2.9 0.7,2 1.3)"
            "(4.0 2.0, 4.2 1.4, 4.8 1.9, 4.4 2.2, 4.0 2.0))", poly);

    point_type p(4, 1);

    std::cout << "within: " << (boost::geometry::within(p, poly) ? "yes" : "no") << std::endl;


    return 0;
}
#endif
