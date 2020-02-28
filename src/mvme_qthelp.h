#ifndef __MVME_QTHELP_H__
#define __MVME_QTHELP_H__

#include "qt_assistant_remote_control.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QString>

namespace mesytec
{
namespace mvme
{

QString get_mvme_qthelp_index_url();

inline auto make_help_keyword_handler(QDialogButtonBox *bb, const QString &keyword)
{
    auto handler = [bb, keyword] (QAbstractButton *button)
    {
        if (button == bb->button(QDialogButtonBox::Help))
            mvme::QtAssistantRemoteControl::instance().activateKeyword(keyword);
    };

    return handler;
}

inline auto make_help_keyword_handler(const QString &keyword)
{
    auto handler = [keyword] ()
    {
        mvme::QtAssistantRemoteControl::instance().activateKeyword(keyword);
    };

    return handler;
}

}
}

#endif /* __MVME_QTHELP_H__ */
