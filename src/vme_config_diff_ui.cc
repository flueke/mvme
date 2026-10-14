#include "vme_config_diff_ui.h"
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QSplitter>
#include <QStandardItem>
#include <QTextEdit>
#include <QVBoxLayout>

namespace mesytec::mvme::vme_config
{

struct VMEConfigDiffItemModel::Private
{
    ConfigDiff diff;
};

VMEConfigDiffItemModel::VMEConfigDiffItemModel(QObject *parent)
    : QStandardItemModel(parent)
    , d(std::make_unique<Private>())
{
    setHorizontalHeaderLabels({"Object", "Status", "Changes"});
}

VMEConfigDiffItemModel::~VMEConfigDiffItemModel() = default;

namespace
{

QString format_variant_value(const QVariant &value)
{
    if (value.userType() == QMetaType::QVariantMap)
    {
        QVariantMap map = value.toMap();
        if (map.isEmpty())
            return "{}";

        QStringList items;
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            items << QString("%1: %2").arg(it.key(), it.value().toString());
        }
        return QString("{ %1 }").arg(items.join(", "));
    }
    return value.toString();
}

QString format_variant_map_diff(const QVariantMap &oldMap, const QVariantMap &newMap)
{
    QString result;
    QTextStream out(&result);

    QSet<QString> allKeys;
    for (auto it = oldMap.begin(); it != oldMap.end(); ++it)
        allKeys.insert(it.key());
    for (auto it = newMap.begin(); it != newMap.end(); ++it)
        allKeys.insert(it.key());

    QStringList sortedKeys = allKeys.values();
    std::sort(sortedKeys.begin(), sortedKeys.end());

    for (const QString &key: sortedKeys)
    {
        bool inOld = oldMap.contains(key);
        bool inNew = newMap.contains(key);

        if (inOld && !inNew)
        {
            // Removed key
            out << "    - " << key << ": " << oldMap[key].toString() << "\n";
        }
        else if (!inOld && inNew)
        {
            // Added key
            out << "    + " << key << ": " << newMap[key].toString() << "\n";
        }
        else if (oldMap[key] != newMap[key])
        {
            // Modified key
            out << "    ~ " << key << ":\n";
            out << "      - " << oldMap[key].toString() << "\n";
            out << "      + " << newMap[key].toString() << "\n";
        }
    }

    return result;
}

QColor get_status_color(DiffNode::Status status)
{
    switch (status)
    {
    case DiffNode::Status::Added:
        return QColor(144, 238, 144); // Light green
    case DiffNode::Status::Removed:
        return QColor(255, 182, 193); // Light red
    case DiffNode::Status::Modified:
        return QColor(255, 255, 153); // Light yellow
    case DiffNode::Status::Unchanged:
        return QColor(Qt::white);
    }
    return QColor(Qt::white);
}

QString get_status_text(DiffNode::Status status)
{
    switch (status)
    {
    case DiffNode::Status::Added:
        return "Added";
    case DiffNode::Status::Removed:
        return "Removed";
    case DiffNode::Status::Modified:
        return "Modified";
    case DiffNode::Status::Unchanged:
        return "Unchanged";
    }
    return "Unknown";
}

// This is the summary text shown in the 'Changes' column.
QString get_changes_text(const DiffNode *node)
{
    assert(node);
    if (!node)
        return QString();

    QStringList changes;

    // Add property changes
    for (auto it = node->propertyChanges.begin(); it != node->propertyChanges.end(); ++it)
    {
        const auto &key = it.key();
        const auto &values = it.value();

        // Format based on property type
        if (key.startsWith("var:"))
        {
            QString varName = key.mid(4);
            changes << QString("%1: %2 → %3")
                           .arg(varName)
                           .arg(values.first.toString())
                           .arg(values.second.toString());
        }
        else if (key == "scriptContent")
        {
            // Count changed lines from the unified diff
            int addedLines = 0;
            int removedLines = 0;

            if (!node->scriptDiff.isEmpty())
            {
                QStringList diffLines = node->scriptDiff.split('\n');
                for (const QString &line: diffLines)
                {
                    if (line.startsWith('+') && !line.startsWith("+++"))
                        addedLines++;
                    else if (line.startsWith('-') && !line.startsWith("---"))
                        removedLines++;
                }
            }

            if (addedLines > 0 || removedLines > 0)
            {
                if (addedLines == removedLines)
                    changes << QString("Script: %1 lines changed").arg(addedLines);
                else
                    changes << QString("Script: +%1/-%2 lines").arg(addedLines).arg(removedLines);
            }
            else
            {
                changes << "Script: modified";
            }
        }
        else
        {
            // Check if both values are QVariantMaps for a summary
            if (values.first.userType() == QMetaType::QVariantMap &&
                values.second.userType() == QMetaType::QVariantMap)
            {
                QVariantMap oldMap = values.first.toMap();
                QVariantMap newMap = values.second.toMap();

                changes << format_variant_map_diff(oldMap, newMap);
            }
            else
            {
                // For simple types, just show the property name
                changes << key;

                changes << QString("%1: %2 → %3")
                               .arg(key)
                               .arg(values.first.toString())
                               .arg(values.second.toString());
            }
        }
    }

    return changes.join("; ");
}

QString get_object_display_name(const DiffNode *node)
{
    if (!node)
        return QString();

    const ConfigObject *obj = node->getObject();
    if (!obj)
        return "<null>";

    QString name = obj->objectName();
    QString type = node->getTypeName();

    if (name.isEmpty())
        name = QString("<%1>").arg(type);

    return QString("%1 (%2)").arg(name, type);
}

void populate_model_recursive(QStandardItem *parentItem, const DiffNode *node)
{
    if (!node)
        return;

    if (node->status != DiffNode::Status::Unchanged)
    {
        // Create items for this node
        auto *nameItem = new QStandardItem(get_object_display_name(node));
        auto *statusItem = new QStandardItem(get_status_text(node->status));
        auto *changesItem = new QStandardItem(get_changes_text(node));

        qDebug() << "changesItem.text(): " << changesItem->text();

        // Set background colors based on status
        QColor bgColor = get_status_color(node->status);
        nameItem->setBackground(QBrush(bgColor));
        statusItem->setBackground(QBrush(bgColor));
        changesItem->setBackground(QBrush(bgColor));

        // Make items non-editable
        nameItem->setEditable(false);
        statusItem->setEditable(false);
        changesItem->setEditable(false);

        // Store pointer to DiffNode in the item for later access
        nameItem->setData(QVariant::fromValue(reinterpret_cast<quintptr>(node)), Qt::UserRole);

        // Add row to parent
        if (parentItem)
        {
            parentItem->appendRow({nameItem, statusItem, changesItem});
        }
        else
        {
            // This shouldn't happen in our case, but handle it anyway
            assert(false);
            return;
        }

        // Recursively add children
        for (const auto &child: node->children)
        {
            populate_model_recursive(nameItem, child.get());
        }
    }
}

} // anonymous namespace

void VMEConfigDiffItemModel::setDiff(ConfigDiff &&diff)
{
    beginResetModel();

    clear();
    setHorizontalHeaderLabels({"Object", "Status", "Changes"});

    d->diff = std::move(diff);

    // Populate the model from the diff tree
    if (const DiffNode *root = d->diff.getDiffTree())
    {
        // Create a top-level item for the root
        auto *nameItem = new QStandardItem(get_object_display_name(root));
        auto *statusItem = new QStandardItem(get_status_text(root->status));
        auto *changesItem = new QStandardItem(get_changes_text(root));

        QColor bgColor = get_status_color(root->status);
        nameItem->setBackground(QBrush(bgColor));
        statusItem->setBackground(QBrush(bgColor));
        changesItem->setBackground(QBrush(bgColor));

        nameItem->setEditable(false);
        statusItem->setEditable(false);
        changesItem->setEditable(false);

        nameItem->setData(QVariant::fromValue(reinterpret_cast<quintptr>(root)), Qt::UserRole);

        appendRow({nameItem, statusItem, changesItem});

        // Add children recursively
        for (const auto &child: root->children)
        {
            populate_model_recursive(nameItem, child.get());
        }
    }

    endResetModel();
}

const ConfigDiff &VMEConfigDiffItemModel::getDiff() const { return d->diff; }

// VMEConfigDiffWidget implementation

namespace
{

QString format_diff_text(const DiffNode *node)
{
    if (!node)
        return QString();

    QString result;
    QTextStream out(&result);

    const ConfigObject *obj = node->getObject();
    if (obj)
    {
        out << "Object: " << obj->objectName() << " (" << node->getTypeName() << ")\n";
        out << "Status: " << get_status_text(node->status) << "\n";
        out << "Path: " << node->getPath() << "\n\n";
    }

    // Show property changes

    // hack: remove 'scriptContent' if present and we do have a scriptDiff. The
    // number of chars in each script is not really useful to anyone.

    auto propertyChanges = node->propertyChanges;

    if (!node->scriptDiff.isEmpty() && propertyChanges.contains("scriptContent"))
    {
        propertyChanges.remove("scriptContent");
    }

    if (!propertyChanges.isEmpty())
    {
        out << "Property Changes:\n";
        out << "-----------------\n";
        for (auto it = propertyChanges.begin(); it != propertyChanges.end(); ++it)
        {
            const auto &key = it.key();
            const auto &values = it.value();

            if (key.startsWith("var:"))
            {
                QString varName = key.mid(4);
                out << "  Variable '" << varName << "':\n";
                out << "    - " << values.first.toString() << "\n";
                out << "    + " << values.second.toString() << "\n";
            }
            else if (key == "scriptContent")
            {
                out << "  Script Content:\n";
                out << "    Old: " << values.first.toString() << "\n";
                out << "    New: " << values.second.toString() << "\n";
            }
            else
            {
                out << "  " << key << ":\n";

                // Check if both values are QVariantMaps
                if (values.first.userType() == QMetaType::QVariantMap &&
                    values.second.userType() == QMetaType::QVariantMap)
                {
                    QVariantMap oldMap = values.first.toMap();
                    QVariantMap newMap = values.second.toMap();
                    out << format_variant_map_diff(oldMap, newMap);
                }
                else
                {
                    out << "    - " << format_variant_value(values.first) << "\n";
                    out << "    + " << format_variant_value(values.second) << "\n";
                }
            }
        }
        out << "\n";
    }

    // Show script diff if available
    if (!node->scriptDiff.isEmpty())
    {
        out << "Script Diff:\n";
        out << "------------\n";
        out << node->scriptDiff << "\n";
    }

    return result;
}

QString format_diff_text_recursive(const DiffNode *node, bool includeUnchanged = false)
{
    if (!node)
        return QString();

    QString result;
    QTextStream out(&result);

    // Format this node
    if (includeUnchanged || node->status != DiffNode::Status::Unchanged)
    {
        out << format_diff_text(node);

        if (!node->children.empty())
            out << "\n" << QString(80, '-') << "\n\n";
    }

    // Format children
    for (const auto &child: node->children)
    {
        if (includeUnchanged || child->hasChanges())
        {
            QString childText = format_diff_text_recursive(child.get(), includeUnchanged);
            if (!childText.isEmpty())
                out << childText;
        }
    }

    return result;
}

} // anonymous namespace

// DiffSyntaxHighlighter implementation

void DiffSyntaxHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat format;

    if (text.startsWith("+++") || text.startsWith("---"))
    {
        // File headers - bold cyan
        format.setForeground(QColor(0, 139, 139));
        format.setFontWeight(QFont::Bold);
        setFormat(0, text.length(), format);
    }
    else if (text.startsWith("@@"))
    {
        // Hunk headers - cyan
        format.setForeground(QColor(0, 139, 139));
        setFormat(0, text.length(), format);
    }
    else if (text.startsWith('+'))
    {
        // Added lines - green
        format.setForeground(QColor(34, 139, 34));
        setFormat(0, text.length(), format);
    }
    else if (text.startsWith('-'))
    {
        // Removed lines - red
        format.setForeground(QColor(178, 34, 34));
        setFormat(0, text.length(), format);
    }
}

// VMEConfigDiffWidget implementation

VMEConfigDiffWidget::VMEConfigDiffWidget(QWidget *parent)
    : QWidget(parent)
    , model_(new VMEConfigDiffItemModel(this))
    , treeView_(new VMEConfigDiffTreeView(this))
    , textEdit_(new QTextEdit(this))
{
    treeView_->setModel(model_);
    treeView_->header()->setStretchLastSection(false);
    treeView_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    treeView_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    treeView_->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    textEdit_->setReadOnly(true);
    textEdit_->setFontFamily("monospace");
    textEdit_->setLineWrapMode(QTextEdit::NoWrap);

    // Apply diff syntax highlighting
    new DiffSyntaxHighlighter(textEdit_->document());

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(treeView_);
    splitter->addWidget(textEdit_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    // Connect selection change to update diff text
    connect(treeView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &)
            {
                if (!current.isValid())
                {
                    textEdit_->clear();
                    return;
                }

                // Get the first column index (where we store the DiffNode pointer)
                QModelIndex nameIndex = current.sibling(current.row(), 0);
                QVariant data = model_->data(nameIndex, Qt::UserRole);

                if (data.isValid())
                {
                    const DiffNode *node =
                        reinterpret_cast<const DiffNode *>(data.value<quintptr>());
                    if (node)
                    {
                        QString diffText = format_diff_text_recursive(node, false);
                        textEdit_->setPlainText(diffText);
                    }
                }
            });

    resize(1000, 800);
}

void VMEConfigDiffWidget::setDiff(ConfigDiff &&diff)
{
    model_->setDiff(std::move(diff));
    treeView_->expandToDepth(1);
    textEdit_->clear();
    auto colCount = model_->columnCount();
    for (int col = 0; col < colCount; ++col)
    {
        treeView_->resizeColumnToContents(col);
    }
}

} // namespace mesytec::mvme::vme_config
