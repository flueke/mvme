#ifndef __GUI_UTIL_H__
#define __GUI_UTIL_H__

#include <QPixmap>

class QWidget;

QWidget *make_vme_script_ref_widget();

/** Paints the pixmap created from embellishment_source onto the pixmap
 * obtained from original_source. The embellishment is painted into the
 * bottom-right corner of the source pixmap.
 * Returns the resulting pixmap.
 */
QPixmap embellish_pixmap(const QString &original_source, const QString &embellishment_source);

#endif /* __GUI_UTIL_H__ */
