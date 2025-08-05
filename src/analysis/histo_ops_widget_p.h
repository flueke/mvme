#ifndef DB567E97_7518_4A0A_8B37_02478E96BC7C
#define DB567E97_7518_4A0A_8B37_02478E96BC7C

#include <QDialog>
#include <QTableWidget>

class HistoOpsEditDialog: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        explicit HistoOpsEditDialog(HistogramOperationsWidget *histoOpsWidget);
        ~HistoOpsEditDialog() override = default;

        bool eventFilter(QObject *watched, QEvent *event) override;
        void updateDialogPosition();
        void refresh();

    private:
        HistogramOperationsWidget *histoOpsWidget_ = nullptr;
        QComboBox *combo_operationType_ = nullptr;
        QTableWidget *tw_entries_ = nullptr;
};

class HistoOpsWidgetPlaceHolder: public QWidget
{
    Q_OBJECT
    public:
        HistoOpsWidgetPlaceHolder(QWidget *parent = nullptr)
            : QWidget(parent)
            , toolBar_(new QToolBar)
        {
            toolBar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            toolBar_->setIconSize(QSize(16, 16));
            set_widget_font_pointsize(toolBar_, 7);

            auto toolBarFrame = new QFrame;
            toolBarFrame->setFrameStyle(QFrame::StyledPanel);
            {
                auto fl = make_hbox<0, 0>(toolBarFrame);
                fl->addWidget(toolBar_);
            }

            auto label = new QLabel(QSL("<h2>Drag & Drop 1D or 2D histograms here to sum them up</h2>"));
            label->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
            auto layout = make_vbox<0, 0>(this);
            layout->addWidget(toolBarFrame);
            layout->addWidget(label);
            layout->setStretch(1, 1);
        }

        ~HistoOpsWidgetPlaceHolder() override = default;

        QToolBar *getToolBar()
        {
            return toolBar_;
        }

    private:
        QToolBar *toolBar_ = nullptr;

};

template <typename ...Args>
QAction *insert_action_at_front(QToolBar *toolBar, Args &&...args)
{
    auto newAction = toolBar->addAction(std::forward<Args>(args)...);

    if (auto actions = toolBar->actions(); actions.size() > 1 && actions.first() != newAction)
    {
        toolBar->removeAction(newAction);
        toolBar->insertAction(actions.first(), newAction);
    }

    return newAction;
}

#endif /* DB567E97_7518_4A0A_8B37_02478E96BC7C */
