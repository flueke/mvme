/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "vme_config_diff.h"

#include "dtl/dtl.hpp"
#include "vme_config_util.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

namespace mesytec::mvme::vme_config
{

namespace
{

QString status_to_string(DiffNode::Status status)
{
    switch (status)
    {
    case DiffNode::Status::Unchanged:
        return "unchanged";
    case DiffNode::Status::Added:
        return "added";
    case DiffNode::Status::Removed:
        return "removed";
    case DiffNode::Status::Modified:
        return "modified";
    }
    return "unknown";
}

QString status_to_symbol(DiffNode::Status status)
{
    switch (status)
    {
    case DiffNode::Status::Unchanged:
        return " ";
    case DiffNode::Status::Added:
        return "+";
    case DiffNode::Status::Removed:
        return "-";
    case DiffNode::Status::Modified:
        return "M";
    }
    return "?";
}

// Helper to compare two symbol tables
bool compare_variables(const vme_script::SymbolTable &a, const vme_script::SymbolTable &b)
{
    if (a.size() != b.size())
        return false;

    for (auto it = a.symbols.begin(); it != a.symbols.end(); ++it)
    {
        auto bIt = b.symbols.find(it.key());
        if (bIt == b.symbols.end())
            return false;
        if (it.value().value != bIt.value().value)
            return false;
    }

    return true;
}

// Get variable differences
QMap<QString, QPair<QString, QString>> get_variable_differences(const vme_script::SymbolTable &a,
                                                                const vme_script::SymbolTable &b)
{
    QMap<QString, QPair<QString, QString>> diffs;

    QSet<QString> allKeys;
    for (auto it = a.symbols.begin(); it != a.symbols.end(); ++it)
        allKeys.insert(it.key());
    for (auto it = b.symbols.begin(); it != b.symbols.end(); ++it)
        allKeys.insert(it.key());

    for (const auto &key: allKeys)
    {
        QString oldValue = a.contains(key) ? a[key].value : QString();
        QString newValue = b.contains(key) ? b[key].value : QString();

        if (oldValue != newValue)
            diffs[key] = qMakePair(oldValue, newValue);
    }

    return diffs;
}

// Forward declarations for recursive diffing
std::unique_ptr<DiffNode> diff_objects(const ConfigObject *a, const ConfigObject *b);
void diff_children(const ConfigObject *a, const ConfigObject *b, DiffNode *parent);

// Compare base ConfigObject properties
void compare_base_properties(const ConfigObject *a, const ConfigObject *b, DiffNode *node)
{
    if (a->objectName() != b->objectName())
        node->propertyChanges["name"] = qMakePair(a->objectName(), b->objectName());

    if (a->isEnabled() != b->isEnabled())
        node->propertyChanges["enabled"] = qMakePair(a->isEnabled(), b->isEnabled());

    // Compare variables
    if (!compare_variables(a->getVariables(), b->getVariables()))
    {
        auto varDiffs = get_variable_differences(a->getVariables(), b->getVariables());
        for (auto it = varDiffs.begin(); it != varDiffs.end(); ++it)
        {
            node->propertyChanges[QString("var:%1").arg(it.key())] = it.value();
        }
    }
}

// Compare VMEScriptConfig specific properties
void compare_script_properties(const VMEScriptConfig *a, const VMEScriptConfig *b, DiffNode *node)
{
    if (a->getScriptContents() != b->getScriptContents())
    {
        node->oldScriptContent = a->getScriptContents();
        node->newScriptContent = b->getScriptContents();
        node->propertyChanges["scriptContent"] =
            qMakePair(QString("(%1 chars)").arg(a->getScriptContents().length()),
                      QString("(%1 chars)").arg(b->getScriptContents().length()));

        // Split into lines for line-by-line diff
        auto splitLines = [](const QString &text) -> std::vector<std::string>
        {
            QStringList lines = text.split('\n');
            std::vector<std::string> result;
            result.reserve(lines.size());
            for (const auto &line: lines)
                result.push_back(line.toStdString());
            return result;
        };

        auto oldLines = splitLines(node->oldScriptContent);
        auto newLines = splitLines(node->newScriptContent);

        // Line-based diff (like git)
        dtl::Diff<std::string, std::vector<std::string>> d(oldLines, newLines);
        d.compose();             // construct an edit distance and LCS and SES
        d.composeUnifiedHunks(); // construct a difference as Unified Format with SES.
        std::ostringstream diffStream;
        d.printUnifiedFormat(diffStream); // print a difference as Unified Format.
        node->scriptDiff = QString::fromStdString(diffStream.str());
    }
}

// Compare ModuleConfig specific properties
void compare_module_properties(const ModuleConfig *a, const ModuleConfig *b, DiffNode *node)
{
    if (a->getBaseAddress() != b->getBaseAddress())
        node->propertyChanges["baseAddress"] =
            qMakePair(QString("0x%1").arg(a->getBaseAddress(), 8, 16, QChar('0')),
                      QString("0x%1").arg(b->getBaseAddress(), 8, 16, QChar('0')));

    if (a->getModuleMeta().typeName != b->getModuleMeta().typeName)
        node->propertyChanges["moduleType"] =
            qMakePair(a->getModuleMeta().typeName, b->getModuleMeta().typeName);
}

// Compare EventConfig specific properties
void compare_event_properties(const EventConfig *a, const EventConfig *b, DiffNode *node)
{
    if (a->triggerCondition != b->triggerCondition)
        node->propertyChanges["triggerCondition"] =
            qMakePair(static_cast<int>(a->triggerCondition), static_cast<int>(b->triggerCondition));

    if (a->triggerOptions != b->triggerOptions)
    {
        // Store copies of the triggerOptions maps.
        node->propertyChanges["triggerOptions"] = qMakePair(a->triggerOptions, b->triggerOptions);
    }

    if (a->irqLevel != b->irqLevel)
        node->propertyChanges["irqLevel"] = qMakePair(a->irqLevel, b->irqLevel);

    if (a->irqVector != b->irqVector)
        node->propertyChanges["irqVector"] = qMakePair(a->irqVector, b->irqVector);

    // vmusb
    if (a->scalerReadoutPeriod != b->scalerReadoutPeriod)
        node->propertyChanges["scalerReadoutPeriod"] = qMakePair(a->scalerReadoutPeriod, b->scalerReadoutPeriod);

    if (a->scalerReadoutFrequency != b->scalerReadoutFrequency)
        node->propertyChanges["scalerReadoutFrequency"] = qMakePair(a->scalerReadoutFrequency, b->scalerReadoutFrequency);

    // child vme init scripts sorted and compared by key
    const auto allKeysSet = a->vmeScripts.keys().toSet().unite(b->vmeScripts.keys().toSet());
    auto allKeys = allKeysSet.toList();
    std::sort(allKeys.begin(), allKeys.end());

    for (const auto &key: allKeys)
    {
        auto scriptA = a->vmeScripts.value(key, nullptr);
        auto scriptB = b->vmeScripts.value(key, nullptr);

        if (scriptA && !scriptB)
            node->propertyChanges[QString("vmeScript:%1").arg(key)] = qMakePair(QString("exists"), QString("removed"));
        else if (!scriptA && scriptB)
            node->propertyChanges[QString("vmeScript:%1").arg(key)] = qMakePair(QString("added"), QString("exists"));
        else if (scriptA && scriptB)
        {
            // Compare the scripts and store any differences in a child node
            auto childNode = diff_objects(scriptA, scriptB);
            if (childNode->hasChanges())
                node->children.push_back(std::move(childNode));
        }
    }
}

// Compare VMEConfig specific properties
void compare_vmeconfig_properties(const VMEConfig *a, const VMEConfig *b, DiffNode *node)
{
    if (a->getControllerType() != b->getControllerType())
        node->propertyChanges["controllerType"] = qMakePair(
            static_cast<int>(a->getControllerType()), static_cast<int>(b->getControllerType()));
}

// Main object comparison function
std::unique_ptr<DiffNode> diff_objects(const ConfigObject *a, const ConfigObject *b)
{
    auto node = std::make_unique<DiffNode>();

    // Handle null cases
    if (!a && !b)
    {
        node->status = DiffNode::Status::Unchanged;
        return node;
    }

    if (!a && b)
    {
        node->status = DiffNode::Status::Added;
        node->modified = b;
        return node;
    }

    if (a && !b)
    {
        node->status = DiffNode::Status::Removed;
        node->original = a;
        return node;
    }

    // Both exist - check if they're the same object
    node->original = a;
    node->modified = b;

    // Same object (or script matched by role/name) - compare properties
    node->status = DiffNode::Status::Unchanged;

    // Compare base properties
    compare_base_properties(a, b, node.get());

    // Compare type-specific properties
    if (auto scriptA = qobject_cast<const VMEScriptConfig *>(a))
    {
        auto scriptB = qobject_cast<const VMEScriptConfig *>(b);
        if (scriptB)
            compare_script_properties(scriptA, scriptB, node.get());
    }
    else if (auto moduleA = qobject_cast<const ModuleConfig *>(a))
    {
        auto moduleB = qobject_cast<const ModuleConfig *>(b);
        if (moduleB)
            compare_module_properties(moduleA, moduleB, node.get());
    }
    else if (auto eventA = qobject_cast<const EventConfig *>(a))
    {
        auto eventB = qobject_cast<const EventConfig *>(b);
        if (eventB)
            compare_event_properties(eventA, eventB, node.get());
    }
    else if (auto vmeA = qobject_cast<const VMEConfig *>(a))
    {
        auto vmeB = qobject_cast<const VMEConfig *>(b);
        if (vmeB)
            compare_vmeconfig_properties(vmeA, vmeB, node.get());
    }

    // If we found any property changes, mark as modified
    if (!node->propertyChanges.isEmpty())
        node->status = DiffNode::Status::Modified;

    // Recursively diff children
    diff_children(a, b, node.get());

    // If this node has no changes but children do, mark as modified
    if (node->status == DiffNode::Status::Unchanged && !node->children.empty())
    {
        for (const auto &child: node->children)
        {
            if (child->hasChanges())
            {
                node->status = DiffNode::Status::Modified;
                break;
            }
        }
    }

    return node;
}

// Helper to get ConfigObject children from various container types
QVector<const ConfigObject *> get_config_children(const ConfigObject *obj)
{
    QVector<const ConfigObject *> result;

    if (auto container = qobject_cast<const ContainerObject *>(obj))
    {
        for (auto child: container->getChildren())
            result.append(child);
    }
    else if (auto event = qobject_cast<const EventConfig *>(obj))
    {
        for (auto module: event->getModuleConfigs())
            result.append(module);
    }
    else if (auto vme = qobject_cast<const VMEConfig *>(obj))
    {
        for (auto event: vme->getEventConfigs())
            result.append(event);
    }
    else if (auto module = qobject_cast<const ModuleConfig *>(obj))
    {
        // Modules have init scripts, reset script, readout script
        if (module->getResetScript())
            result.append(module->getResetScript());
        if (module->getReadoutScript())
            result.append(module->getReadoutScript());
        for (auto script: module->getInitScripts())
            result.append(script);
    }

    return result;
}

// Diff children of two objects
void diff_children(const ConfigObject *a, const ConfigObject *b, DiffNode *parent)
{
    // Special handling for ModuleConfig: compare scripts by role/name, not by ID
    if (auto moduleA = qobject_cast<const ModuleConfig *>(a))
    {
        auto moduleB = qobject_cast<const ModuleConfig *>(b);
        if (!moduleB)
            return;

        // Compare reset scripts directly (match by role)
        auto resetDiff = diff_objects(moduleA->getResetScript(), moduleB->getResetScript());
        parent->children.push_back(std::move(resetDiff));

        // Compare readout scripts directly (match by role)
        auto readoutDiff = diff_objects(moduleA->getReadoutScript(), moduleB->getReadoutScript());
        parent->children.push_back(std::move(readoutDiff));

        // Compare init scripts by name or position
        auto initScriptsA = moduleA->getInitScripts();
        auto initScriptsB = moduleB->getInitScripts();

        // Separate named and unnamed scripts, maintaining order
        QVector<VMEScriptConfig *> unnamedA, unnamedB;
        QMap<QString, VMEScriptConfig *> namedA, namedB;

        for (auto script: initScriptsA)
        {
            QString name = script->objectName();
            if (name.isEmpty())
                unnamedA.append(script);
            else
                namedA[name] = script;
        }

        for (auto script: initScriptsB)
        {
            QString name = script->objectName();
            if (name.isEmpty())
                unnamedB.append(script);
            else
                namedB[name] = script;
        }

        // Compare named scripts by name
        QSet<QString> allNames;
        for (const auto &name: namedA.keys())
            allNames.insert(name);
        for (const auto &name: namedB.keys())
            allNames.insert(name);

        for (const auto &name: allNames)
        {
            auto initDiff = diff_objects(namedA.value(name), namedB.value(name));
            parent->children.push_back(std::move(initDiff));
        }

        // Compare unnamed scripts by position
        int maxUnnamed = qMax(unnamedA.size(), unnamedB.size());
        for (int i = 0; i < maxUnnamed; ++i)
        {
            auto scriptA = i < unnamedA.size() ? unnamedA[i] : nullptr;
            auto scriptB = i < unnamedB.size() ? unnamedB[i] : nullptr;
            auto initDiff = diff_objects(scriptA, scriptB);
            parent->children.push_back(std::move(initDiff));
        }

        return;
    }

    // Default behavior for other object types: match by ID
    QMap<QUuid, const ConfigObject *> aChildren, bChildren;

    for (auto child: get_config_children(a))
        aChildren[child->getId()] = child;

    for (auto child: get_config_children(b))
        bChildren[child->getId()] = child;

    // Find all IDs
    QSet<QUuid> allIds;
    for (auto id: aChildren.keys())
        allIds.insert(id);
    for (auto id: bChildren.keys())
        allIds.insert(id);

    // Diff each child
    for (const auto &id: allIds)
    {
        auto childDiff = diff_objects(aChildren.value(id), bChildren.value(id));
        parent->children.push_back(std::move(childDiff));
    }
}

// Recursive text output helper
void node_to_text(const DiffNode *node, QTextStream &out, int indent, bool showUnchanged)
{
    if (!node)
        return;

    if (!showUnchanged && !node->hasChanges())
        return;

    QString indentStr = QString(indent * 2, ' ');
    QString symbol = status_to_symbol(node->status);

    auto obj = node->getObject();
    if (!obj)
        return;

    QString typeName = node->getTypeName();
    QString objName = obj->objectName();

    out << indentStr << "[" << symbol << "] " << typeName;
    if (!objName.isEmpty())
        out << " \"" << objName << "\"";

    if (node->status != DiffNode::Status::Unchanged)
        out << " (" << status_to_string(node->status) << ")";

    out << "\n";

    // Show property changes
    if (!node->propertyChanges.isEmpty())
    {
        for (auto it = node->propertyChanges.begin(); it != node->propertyChanges.end(); ++it)
        {
            out << indentStr << "  " << it.key() << ": ";
            out << "\"" << it.value().first.toString() << "\" -> ";
            out << "\"" << it.value().second.toString() << "\"\n";
        }

        if (!node->scriptDiff.isEmpty())
        {
            out << indentStr << "  Script Diff:\n";
            for (const auto &line: node->scriptDiff.split('\n'))
                out << indentStr << "    " << line << "\n";
        }
    }

    // Recursively process children
    for (const auto &child: node->children)
        node_to_text(child.get(), out, indent + 1, showUnchanged);
}

// Creates a new ModuleConfig instance with the same type as the original but
// using the mvme template default values. Some base settings are copied from
// the original as they're not interesting to have in the diff output.
std::unique_ptr<ModuleConfig> make_module_for_diff(const ModuleConfig *original)
{
    auto ret = vme_config::make_module_config(original->getModuleMeta().typeName);
    ret->setId(original->getId());
    ret->setObjectName(original->objectName());
    ret->setBaseAddress(original->getBaseAddress());
    ret->setModuleMeta(original->getModuleMeta());
    return ret;
}

// Creates a new EventConfig using the template defaults. Additionally creates
// default child ModuleConfigs for all modules present in the orignal event.
// This way both the event itself and the child modules can be compared against
// the defaults.
std::unique_ptr<EventConfig> make_event_for_diff(const EventConfig *original)
{
    auto templateEvent = vme_config::make_new_event_config();

    for (const auto &mod: original->getModuleConfigs())
    {
        auto templateMod = make_module_for_diff(mod);
        templateEvent->addModuleConfig(templateMod.release());
    }

    return templateEvent;
}

} // anonymous namespace

//
// DiffNode implementation
//

QString DiffNode::getTypeName() const
{
    auto obj = getObject();
    if (!obj)
        return "Unknown";

    if (qobject_cast<const VMEConfig *>(obj))
        return "VMEConfig";
    if (qobject_cast<const EventConfig *>(obj))
        return "EventConfig";
    if (qobject_cast<const ModuleConfig *>(obj))
        return "ModuleConfig";
    if (qobject_cast<const VMEScriptConfig *>(obj))
        return "VMEScriptConfig";
    if (qobject_cast<const ContainerObject *>(obj))
        return "Container";

    return "ConfigObject";
}

bool DiffNode::hasChanges() const
{
    if (status != Status::Unchanged)
        return true;

    if (!propertyChanges.isEmpty())
        return true;

    for (const auto &child: children)
    {
        if (child->hasChanges())
            return true;
    }

    return false;
}

//
// ConfigDiff implementation
//

ConfigDiff::ConfigDiff() = default;
ConfigDiff::~ConfigDiff() = default;

void ConfigDiff::setDiffRoot(std::unique_ptr<DiffNode> root)
{
    m_root = std::move(root);
    buildObjectMap();
}

void ConfigDiff::buildObjectMap()
{
    m_objectMap.clear();
    if (m_root)
        buildObjectMapRecursive(m_root.get());
}

void ConfigDiff::buildObjectMapRecursive(const DiffNode *node)
{
    if (!node)
        return;

    auto obj = node->getObject();
    if (obj)
        m_objectMap[obj->getId()] = node;

    for (const auto &child: node->children)
        buildObjectMapRecursive(child.get());
}

bool ConfigDiff::hasChanges() const { return m_root && m_root->hasChanges(); }

bool ConfigDiff::isObjectModified(const QUuid &id) const
{
    auto node = findNode(id);
    return node && node->hasChanges();
}

QVector<QUuid> ConfigDiff::getAddedObjects() const
{
    QVector<QUuid> result;

    for (auto it = m_objectMap.begin(); it != m_objectMap.end(); ++it)
    {
        if (it.value()->status == DiffNode::Status::Added)
            result.append(it.key());
    }

    return result;
}

QVector<QUuid> ConfigDiff::getRemovedObjects() const
{
    QVector<QUuid> result;

    for (auto it = m_objectMap.begin(); it != m_objectMap.end(); ++it)
    {
        if (it.value()->status == DiffNode::Status::Removed)
            result.append(it.key());
    }

    return result;
}

QVector<QUuid> ConfigDiff::getModifiedObjects() const
{
    QVector<QUuid> result;

    for (auto it = m_objectMap.begin(); it != m_objectMap.end(); ++it)
    {
        if (it.value()->status == DiffNode::Status::Modified)
            result.append(it.key());
    }

    return result;
}

const DiffNode *ConfigDiff::findNode(const QUuid &id) const
{
    return m_objectMap.value(id, nullptr);
}

QString ConfigDiff::toText(bool showUnchanged) const
{
    QString result;
    QTextStream out(&result);

    if (!m_root)
    {
        out << "No diff data available\n";
        return result;
    }

    if (!hasChanges())
    {
        out << "No changes detected\n";
        return result;
    }

    out << "Configuration Diff:\n";
    out << "==================\n\n";

    node_to_text(m_root.get(), out, 0, showUnchanged);

    return result;
}

QString ConfigDiff::toCompactSummary() const
{
    QString result;
    QTextStream out(&result);

    if (!hasChanges())
    {
        out << "No changes";
        return result;
    }

    auto added = getAddedObjects();
    auto removed = getRemovedObjects();
    auto modified = getModifiedObjects();

    out << "Summary: ";

    QStringList parts;
    if (!added.isEmpty())
        parts << QString("%1 added").arg(added.size());
    if (!removed.isEmpty())
        parts << QString("%1 removed").arg(removed.size());
    if (!modified.isEmpty())
        parts << QString("%1 modified").arg(modified.size());

    out << parts.join(", ");

    return result;
}

QString ConfigDiff::toHtml() const
{
    // TODO: Implement HTML output for GUI display
    return "<pre>" + toText().toHtmlEscaped() + "</pre>";
}

QJsonObject ConfigDiff::toJson() const
{
    // TODO: Implement JSON serialization
    QJsonObject obj;
    obj["hasChanges"] = hasChanges();
    obj["summary"] = toCompactSummary();
    return obj;
}

//
// Public API functions
//

ConfigDiff diff_configs(const ConfigObject *a, const ConfigObject *b)
{
    ConfigDiff result;
    result.setDiffRoot(diff_objects(a, b));
    return result;
}

ConfigDiff diff_vme_configs(const VMEConfig *a, const VMEConfig *b)
{
    return diff_configs(static_cast<const ConfigObject *>(a), static_cast<const ConfigObject *>(b));
}

ConfigDiff diff_event_configs(const EventConfig *a, const EventConfig *b)
{
    return diff_configs(static_cast<const ConfigObject *>(a), static_cast<const ConfigObject *>(b));
}

ConfigDiff diff_module_configs(const ModuleConfig *a, const ModuleConfig *b)
{
    return diff_configs(static_cast<const ConfigObject *>(a), static_cast<const ConfigObject *>(b));
}

ConfigDiff diff_module_against_template(const ModuleConfig *mod)
{
    auto templateMod = make_module_for_diff(mod);
    auto ret = diff_configs(templateMod.get(), mod);
    ret.takeOwnership(std::move(templateMod));
    return ret;
}

ConfigDiff diff_event_against_template(const EventConfig *event)
{
    auto templateEvent = make_event_for_diff(event);
    auto ret = diff_configs(templateEvent.get(), event);
    ret.takeOwnership(std::move(templateEvent));
    return ret;
}

} // namespace mesytec::mvme::vme_config
