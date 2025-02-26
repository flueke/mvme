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
#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "libmvme_export.h"

#include "globals.h"
#include "template_system.h"
#include "vme_controller.h"
#include "vme_script_variables.h"

#include <QObject>
#include <QUuid>
#include <QDebug>

class QJsonObject;

class LIBMVME_EXPORT ConfigObject: public QObject
{
    Q_OBJECT
    signals:
        void modifiedChanged(bool);
        void modified(bool);
        void enabledChanged(bool);

    public:
        explicit ConfigObject(QObject *parent = 0);
        ~ConfigObject()
        {
        }

        QUuid getId() const { return m_id; }
        // Generates a new Uuid for the object.
        // Note: this method does not set the objects modified flag!
        void generateNewId();
        // Allows to manually set a new id. Also does not set the modified flag.
        void setId(const QUuid &newId) { m_id = newId; }

        bool isModified() const { return m_modified; }
        bool isEnabled() const { return m_enabled; }

        QString getObjectPath() const;

        std::error_code read(const QJsonObject &json);
        std::error_code write(QJsonObject &json) const;

        const vme_script::SymbolTable &getVariables() const { return m_variables; }

        // Replaces the objects symboltable.
        void setVariables(const vme_script::SymbolTable &variables);

        // Adds a new or overwrites an existing variable.
        void setVariable(const QString &name, const vme_script::Variable &var);

        // Udpates the named variables value. This leaves an existing comment and
        // other variable attributes intact.
        void setVariableValue(const QString &name, const QString &value);

        template<typename T, typename Predicate>
        T findChildByPredicate(Predicate p, bool recurse=true) const
        {
            for (auto child: children())
            {
                auto asT = qobject_cast<T>(child);

                if (asT && p(asT))
                    return asT;

                if (recurse)
                {
                    if (auto cfg = qobject_cast<ConfigObject *>(child))
                    {
                        if (auto obj = cfg->findChildByPredicate<T>(p, recurse))
                            return obj;
                    }
                }
            }
            return {};
        }

        template<typename T>
        T findChildByName(const QString &name, bool recurse = true) const
        {
            auto pred = [&name] (const ConfigObject *obj)
            {
                return obj->objectName() == name;
            };

            return findChildByPredicate<T>(pred, recurse);
        }

        ConfigObject *findChildByName(const QString &name, bool recurse = true) const
        {
            return findChildByName<ConfigObject *>(name, recurse);
        }

        template<typename T>
        T findChildById(const QUuid &id, bool recurse = true) const
        {
            auto pred = [&id] (const ConfigObject *obj)
            {
                return obj->getId() == id;
            };

            return findChildByPredicate<T>(pred, recurse);
        }

        ConfigObject *findChildById(const QUuid &id, bool recurse = true) const
        {
            return findChildById<ConfigObject *>(id, recurse);
        }

        std::string objectNameStdString() const;

    public slots:
        void setModified(bool b = true);
        void setEnabled(bool b);

    protected:
        // Note: the watchDynamicProperties flag and
        // setWatchDynamicProperties() make it so that changes to dynamic
        // QObject properties mark this object as being modified.
        //ConfigObject(QObject *parent, bool watchDynamicProperties);
        bool eventFilter(QObject *obj, QEvent *event) override;
        void setWatchDynamicProperties(bool doWatch);

        virtual std::error_code read_impl(const QJsonObject &json) = 0;
        virtual std::error_code write_impl(QJsonObject &json) const = 0;


        QUuid m_id;
        bool m_modified = false;
        bool m_enabled = true;
        bool m_eventFilterInstalled = false;

    private:
        vme_script::SymbolTable m_variables;
};

// A generic container object used to hold more specific child objects or other
// containers. This can be used by the UI to structure the object tree.
//
// Note: when a child object is deleted it will automatically be removed from
// the list of children of this object but the childAboutToBeRemoved() signal
// will not be emitted (because at that point the child has already been
// destroyed and only the QObject* part still exists).
class LIBMVME_EXPORT ContainerObject: public ConfigObject
{
    Q_OBJECT
    signals:
        void childAdded(ConfigObject *co, int index);
        void childAboutToBeRemoved(ConfigObject *co);

    public:
        Q_INVOKABLE explicit ContainerObject(QObject *parent = nullptr);

        ContainerObject(const QString &name, const QString &displayName,
                        const QString &icon, QObject *parent = nullptr);

        // Append a child
        void addChild(ConfigObject *obj)
        {
            addChild(obj, m_children.size());
        }

        // Insert a child at the specified index
        void addChild(ConfigObject *obj, int index)
        {
            index = std::min(m_children.size(), index);
            m_children.insert(index, obj);
            obj->setParent(this);
            connect(obj, &QObject::destroyed,
                    this, &ContainerObject::onChildDestroyed);

            //qDebug() << __PRETTY_FUNCTION__
            //    << "emit childAdded() this=" << this
            //    << "obj=" << obj
            //    << "index=" << index;

            emit childAdded(obj, index);
            setModified();
        }

        // Note: the child is neither deleted nor reparented. It is only
        // removed from this objects list of children.
        bool removeChild(ConfigObject *obj)
        {
            if (m_children.contains(obj))
            {
                //qDebug() << __PRETTY_FUNCTION__ << "emit childAboutToBeRemoved() this=" << this << "obj=" << obj;
                emit childAboutToBeRemoved(obj);

                m_children.removeOne(obj);

                disconnect(obj, &QObject::destroyed,
                           this, &ContainerObject::onChildDestroyed);

                setModified();
                return true;
            }
            return false;
        }

        // Returns the list of direct child objects.
        QVector<ConfigObject *> getChildren() const
        {
            return m_children;
        }

        // Returns the direct child with the supplied name.
        ConfigObject *getChild(const QString &name) const
        {
            return findChildByName(name, false);
        }

        // Returns true if obj is a direct child of this container.
        bool contains(ConfigObject *obj) const
        {
            return m_children.indexOf(obj) >= 0;
        }

        // Returns true if the container has a direct child with the supplied
        // name.
        bool contains(const QString &name) const
        {
            return getChild(name) != nullptr;
        }

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private slots:
        // This should work even if this QObject had non-ConfigObject children
        // via setParent() which get destroyed. It's just a pointer comparison
        // after all.
        void onChildDestroyed(QObject *child)
        {
            m_children.removeAll(reinterpret_cast<ConfigObject *>(child));
        }

    private:
        QVector<ConfigObject *> m_children;
};

inline std::unique_ptr<ContainerObject> make_directory_container(
    const QString &name)
{
    return std::make_unique<ContainerObject>(
        name, QString{}, ":/folder_orange.png");
}

class LIBMVME_EXPORT VMEScriptConfig: public ConfigObject
{
    Q_OBJECT
    public:
        explicit Q_INVOKABLE VMEScriptConfig(QObject *parent = 0);
        VMEScriptConfig(const QString &name, const QString &contents, QObject *parent = 0);

        QString getScriptContents() const
        { return m_script; }

        void setScriptContents(const QString &);
        void addToScript(const QString &str);

        QString getVerboseTitle() const;

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private:
        QString m_script;
};

class VMEConfig;
class EventConfig;

class LIBMVME_EXPORT ModuleConfig: public ConfigObject
{
    Q_OBJECT
    public:
        explicit Q_INVOKABLE ModuleConfig(QObject *parent = 0);

        uint32_t getBaseAddress() const { return m_baseAddress; }
        void setBaseAddress(uint32_t address);

        const vats::VMEModuleMeta getModuleMeta() const { return m_meta; }
        void setModuleMeta(const vats::VMEModuleMeta &meta);

        VMEScriptConfig *getResetScript() const { return m_resetScript; }
        VMEScriptConfig *getReadoutScript() const { return m_readoutScript; }

        QVector<VMEScriptConfig *>  getInitScripts() const { return m_initScripts; }
        VMEScriptConfig *getInitScript(const QString &scriptName) const;
        VMEScriptConfig *getInitScript(s32 scriptIndex) const;

        void addInitScript(VMEScriptConfig *script);
        bool removeInitScript(VMEScriptConfig *script);

        const EventConfig *getEventConfig() const;
        EventConfig *getEventConfig();
        QUuid getEventId() const;

        const VMEConfig *getVMEConfig() const;
        VMEConfig *getVMEConfig();

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private:
        uint32_t m_baseAddress = 0;
        VMEScriptConfig *m_resetScript;
        VMEScriptConfig *m_readoutScript;
        QVector<VMEScriptConfig *> m_initScripts;
        vats::VMEModuleMeta m_meta;
};

class LIBMVME_EXPORT EventConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void moduleAdded(ModuleConfig *module, int parentIndex);
        void moduleAboutToBeRemoved(ModuleConfig *module);

    public:
        explicit Q_INVOKABLE EventConfig(QObject *parent = nullptr);

        void addModuleConfig(ModuleConfig *config)
        {
            addModuleConfig(config, modules.size());
        }

        void addModuleConfig(ModuleConfig *config, int index)
        {
            index = std::min(modules.size(), index);
            config->setParent(this);
            modules.insert(index, config);
            emit moduleAdded(config, index);
            setModified();
        }

        bool removeModuleConfig(ModuleConfig *config)
        {
            bool ret = modules.removeOne(config);
            if (ret)
            {
                emit moduleAboutToBeRemoved(config);
                setModified();
            }

            return ret;
        }

        int moduleCount() const
        {
            return modules.size();
        }

        QList<ModuleConfig *> getModuleConfigs() const { return modules; }

        ModuleConfig *getModuleConfig(int moduleIndex) const { return modules.value(moduleIndex); }

        TriggerCondition triggerCondition = TriggerCondition::Interrupt;
        QVariantMap triggerOptions = QVariantMap();

        uint8_t irqLevel = 0;
        uint8_t irqVector = 0;

        // Maximum time between scaler stack executions in units of 0.5s (VMUSB)
        uint8_t scalerReadoutPeriod = 2;
        // Maximum number of events between scaler stack executions (VMUSB)
        uint16_t scalerReadoutFrequency = 0;

        /** Known keys for an event:
         * "daq_start", "daq_stop", "readout_start", "readout_end"
         */
        QMap<QString, VMEScriptConfig *> vmeScripts;

        /* Set by the readout worker and then used by the buffer
         * processor to map from stack ids to event configs. */
        // Maybe should move this elsewhere as it is vmusb specific
        uint8_t stackID = 0u; // FIXME: vmusb only

        const VMEConfig *getVMEConfig() const;
        VMEConfig *getVMEConfig();

        // Returns the timer period value and the TimerBaseUnit string in a
        // pair. Only valid for periodic events.
        // See EventConfigDialog for how the information is stored in
        // triggerOptions.
        std::pair<unsigned, QString> getMVLCTimerPeriod() const
        {
            return std::make_pair(triggerOptions["mvlc.timer_period"].toUInt(),
                                  triggerOptions["mvlc.timer_base"].toString());
        }

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private:
        QList<ModuleConfig *> modules;
};

enum class VMEConfigReadResult
{
    NoError,
    VersionTooOld, // User should never see this as we do schema updates.
    VersionTooNew  // User can see this and needs to ugprade mvme.
};

LIBMVME_EXPORT std::error_code make_error_code(VMEConfigReadResult r);

namespace std
{
    template<> struct is_error_code_enum<VMEConfigReadResult>: true_type {};
} // end namespace std

class LIBMVME_EXPORT VMEConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void eventAdded(EventConfig *config);
        void eventAboutToBeRemoved(EventConfig *config);

        void vmeControllerTypeSet(const VMEControllerType &t);

        // Emitted on adding a global child at any hierarchy level (not only
        // direct children are considered).
        void globalChildAdded(ConfigObject *child, int parentIndex);

        // Emitted for a global child at any hierarchy level. Only emitted if
        // the child is removed via ContainerObject::removeChild(), not if the
        // child is manually deleted.
        void globalChildAboutToBeRemoved(ConfigObject *child);

    public:
        Q_INVOKABLE explicit VMEConfig(QObject *parent = 0);

        // events
        void addEventConfig(EventConfig *config);
        bool removeEventConfig(EventConfig *config);
        bool contains(EventConfig *config);
        QList<EventConfig *> getEventConfigs() const { return eventConfigs; }
        EventConfig *getEventConfig(int eventIndex) const { return eventConfigs.value(eventIndex); }
        EventConfig *getEventConfig(const QString &name) const;
        EventConfig *getEventConfig(const QUuid &id) const;

        // modules
        ModuleConfig *getModuleConfig(int eventIndex, int moduleIndex) const;
        ModuleConfig *getModuleConfig(const QUuid &moduleId) const;
        QList<ModuleConfig *> getAllModuleConfigs() const;
        QPair<int, int> getEventAndModuleIndices(ModuleConfig *cfg) const;

        // scripts
        bool addGlobalScript(VMEScriptConfig *config, const QString &category);
        bool removeGlobalScript(VMEScriptConfig *config);
        QStringList getGlobalScriptCategories() const;

        // vme controller
        void setVMEController(VMEControllerType type,
                              const QVariantMap &settings = QVariantMap());
        VMEControllerType getControllerType() const { return m_controllerType; }
        QVariantMap getControllerSettings() const { return m_controllerSettings; }
        unsigned getMvlcCrateId() const;

        const ContainerObject &getGlobalObjectRoot() const;
        ContainerObject &getGlobalObjectRoot();

        ContainerObject *getGlobalStartsScripts() { return getGlobalObjectRoot().findChild<ContainerObject *>("daq_start"); }
        ContainerObject *getGlobalStopScripts() { return getGlobalObjectRoot().findChild<ContainerObject *>("daq_stop"); }
        ContainerObject *getGlobalManualScripts() { return getGlobalObjectRoot().findChild<ContainerObject *>("manual"); }

        // Special accessor to find the MVLC Trigger IO config
        VMEScriptConfig *getMVLCTriggerIOScript() const
        {
            return getGlobalObjectRoot().findChild<VMEScriptConfig *>(
                QSL("mvlc_trigger_io"));
        }

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private:
        void onChildObjectAdded(ConfigObject *child, int parentIndex);
        void onChildObjectAboutToBeRemoved(ConfigObject *child);
        void createMissingGlobals();

        QList<EventConfig *> eventConfigs;
        VMEControllerType m_controllerType = VMEControllerType::MVLC_USB;
        QVariantMap m_controllerSettings;
        ContainerObject m_globalObjects;
};

Q_DECLARE_METATYPE(ConfigObject *);
Q_DECLARE_METATYPE(ContainerObject *);
Q_DECLARE_METATYPE(VMEScriptConfig *);
Q_DECLARE_METATYPE(ModuleConfig *);
Q_DECLARE_METATYPE(EventConfig *);
Q_DECLARE_METATYPE(VMEConfig *);

std::pair<std::unique_ptr<VMEConfig>, QString>
LIBMVME_EXPORT read_vme_config_from_file(
    const QString &filename,
    std::function<void (const QString &msg)> logger = {});

QString make_unique_event_name(const QString &prefix, const VMEConfig *vmeConfig);
QString make_unique_event_name(const VMEConfig *vmeConfig);
QString make_unique_module_name(const QString &prefix, const VMEConfig *vmeConfig);
QString make_unique_name(const ConfigObject *co, const ContainerObject *destContainer);

#endif
