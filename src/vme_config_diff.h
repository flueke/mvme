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

#ifndef __MVME_VME_CONFIG_DIFF_H__
#define __MVME_VME_CONFIG_DIFF_H__

#include "libmvme_export.h"
#include "vme_config.h"

#include <QMap>
#include <QString>
#include <QVector>
#include <QVariant>
#include <memory>
#include <vector>

namespace mesytec::mvme::vme_config
{

// VMEConfig tree diffing tools.
//
// Limitations: does currently not compare the VMEScriptVariable comments, only
// the variable values.
//
// Important: pointers to the objects being diffed are stored in the DiffNode
// structures. These objects have to outlive the DiffNodes! The root ConfigDiff
// has a takeOwnership() method which can be used to transfer ownership of
// ConfigObjects to the diff. This is currently used when comparing to the
// template baseline.

// Represents the difference between two config objects at a single tree node
struct LIBMVME_EXPORT DiffNode
{
    enum class Status
    {
        Unchanged,  // Object exists in both trees and has no changes
        Added,      // Object only exists in 'modified' tree
        Removed,    // Object only exists in 'original' tree
        Modified    // Object exists in both but has property changes
    };

    Status status = Status::Unchanged;

    const ConfigObject* original = nullptr;  // nullptr if Added
    const ConfigObject* modified = nullptr;  // nullptr if Removed

    // For Modified nodes: details about what changed
    // Key is property name, value is pair of (old_value, new_value)
    QMap<QString, QPair<QVariant, QVariant>> propertyChanges;

    // Script content changes (for VMEScriptConfig)
    QString oldScriptContent;
    QString newScriptContent;
    QString scriptDiff; // Line-by-line diff of the script contents.

    // Child node differences
    std::vector<std::unique_ptr<DiffNode>> children;

    // Helper to get the relevant object for display
    const ConfigObject* getObject() const
    {
        return modified ? modified : original;
    }

    // Get the object path (e.g., "Events/Event0/Module1")
    QString getPath() const
    {
        if (auto obj = getObject())
            return obj->getObjectPath();
        return QString();
    }

    // Get object type name
    QString getTypeName() const;

    // Check if this node or any children have changes
    bool hasChanges() const;
};

// Configuration difference result with query and reporting capabilities
class LIBMVME_EXPORT ConfigDiff
{
public:
    ConfigDiff();
    ~ConfigDiff();

    // Move semantics only
    ConfigDiff(ConfigDiff&&) = default;
    ConfigDiff& operator=(ConfigDiff&&) = default;

    // No copying (contains unique_ptr)
    ConfigDiff(const ConfigDiff&) = delete;
    ConfigDiff& operator=(const ConfigDiff&) = delete;

    // Query methods
    bool hasChanges() const;
    bool isObjectModified(const QUuid& id) const;
    QVector<QUuid> getAddedObjects() const;
    QVector<QUuid> getRemovedObjects() const;
    QVector<QUuid> getModifiedObjects() const;

    // Get the diff tree root
    const DiffNode* getDiffTree() const { return m_root.get(); }

    // Find a specific node in the diff tree by object ID
    const DiffNode* findNode(const QUuid& id) const;

    // Text-based output
    QString toText(bool showUnchanged = false) const;
    QString toCompactSummary() const;

    // HTML output (for future GUI display)
    QString toHtml() const;

    // Export to JSON for serialization/storage
    QJsonObject toJson() const;

    // Set the root diff node (used by diff_configs)
    void setDiffRoot(std::unique_ptr<DiffNode> root);

    // Take ownership of a ConfigObject (used for template baseline diffing)
    void takeOwnership(std::unique_ptr<ConfigObject> &&obj)
    {
        m_ownedObjects.push_back(std::move(obj));
    }

private:
    void buildObjectMap();
    void buildObjectMapRecursive(const DiffNode* node);

    std::unique_ptr<DiffNode> m_root;
    QMap<QUuid, const DiffNode*> m_objectMap;  // Fast lookup by ID
    // Need to keep objects alive when diffing against the template baseline.
    // Otherwise DiffNode::original and DiffNode::modified would dangle.
    std::vector<std::unique_ptr<ConfigObject>> m_ownedObjects;
};

// Main diffing function - compares two config object trees
LIBMVME_EXPORT ConfigDiff diff_configs(const ConfigObject* a, const ConfigObject* b);

// Helper functions for specific config types
LIBMVME_EXPORT ConfigDiff diff_vme_configs(const VMEConfig* a, const VMEConfig* b);
LIBMVME_EXPORT ConfigDiff diff_event_configs(const EventConfig* a, const EventConfig* b);
LIBMVME_EXPORT ConfigDiff diff_module_configs(const ModuleConfig* a, const ModuleConfig* b);

// Helper function to diff against the template baseline.
// These return a pair of objects: the diff and the new object, created form the
// vast system. The object has to outlive the Diff structures, otherwise thing
// wil crash and burn.
LIBMVME_EXPORT ConfigDiff diff_module_against_template(const ModuleConfig* mod);
LIBMVME_EXPORT ConfigDiff diff_event_against_template(const EventConfig* event);


} // namespace mesytec::mvme::vme_config

#endif // __MVME_VME_CONFIG_DIFF_H__
