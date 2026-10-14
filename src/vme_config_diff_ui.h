#ifndef A6A5380A_7819_493C_B897_FABABE4FBB37
#define A6A5380A_7819_493C_B897_FABABE4FBB37

#include "vme_config_diff.h"
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTreeView>
#include <QWidget>

class QTextEdit;

namespace mesytec::mvme::vme_config
{

class LIBMVME_EXPORT VMEConfigDiffItemModel: public QStandardItemModel
{
    Q_OBJECT
  public:
    using QStandardItemModel::QStandardItemModel;
    explicit VMEConfigDiffItemModel(QObject *parent = nullptr);
    ~VMEConfigDiffItemModel() override;

    void setDiff(ConfigDiff &&diff);
    const ConfigDiff &getDiff() const;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT VMEConfigDiffTreeView: public QTreeView
{
    Q_OBJECT
  public:
    VMEConfigDiffTreeView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
        setEditTriggers(QAbstractItemView::EditKeyPressed);
        setDefaultDropAction(Qt::MoveAction);         // internal DnD
        setDragDropMode(QAbstractItemView::DragDrop); // external DnD
        setDragDropOverwriteMode(false);
        setDragEnabled(false);
        show();
        resize(500, 700);
        resizeColumnToContents(0);
    }
};

class LIBMVME_EXPORT VMEConfigDiffWidget: public QWidget
{
    Q_OBJECT
  public:
    explicit VMEConfigDiffWidget(QWidget *parent = nullptr);
    void setDiff(ConfigDiff &&diff);

  private:
    VMEConfigDiffItemModel *model_;
    VMEConfigDiffTreeView *treeView_;
    QTextEdit *textEdit_;
};

class DiffSyntaxHighlighter: public QSyntaxHighlighter
{
    using QSyntaxHighlighter::QSyntaxHighlighter;

  protected:
    void highlightBlock(const QString &text) override;
};

} // namespace mesytec::mvme::vme_config

#endif /* A6A5380A_7819_493C_B897_FABABE4FBB37 */
