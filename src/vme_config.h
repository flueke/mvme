/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "globals.h"
#include "libmvme_export.h"
#include "template_system.h"
#include "vme_controller.h"

#include <QObject>
#include <QUuid>

class QJsonObject;

class LIBMVME_EXPORT ConfigObject: public QObject
{
    Q_OBJECT
    signals:
        void modifiedChanged(bool);
        void modified(bool);
        void enabledChanged(bool);

    public:
        ConfigObject(QObject *parent = 0);

        QUuid getId() const { return m_id; }

        virtual void setModified(bool b = true);
        bool isModified() const { return m_modified; }

        void setEnabled(bool b);
        bool isEnabled() const { return m_enabled; }

        QString getObjectPath() const;

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

#if 0
        ConfigObject *findChildById(const QUuid &id, bool recurse=true) const
        {
            return findChildById<ConfigObject *>(id, recurse);
        }

        template<typename T> T findChildById(const QUuid &id, bool recurse=true) const
        {
            for (auto child: children())
            {
                auto configObject = qobject_cast<ConfigObject *>(child);

                if (configObject)
                {
                    if (configObject->getId() == id)
                        return qobject_cast<T>(configObject);
                }

                if (recurse)
                {
                    if (auto obj = configObject->findChildById<T>(id, recurse))
                        return obj;
                }
            }

            return {};
        }
#endif

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

        ConfigObject * findChildByName(const QString &name, bool recurse = true) const
        {
            return findChildByName<ConfigObject *>(name, recurse);
        }

    protected:
        // Note: the watchDynamicProperties flag and
        // setWatchDynamicProperties() make it so that changes to dynamic
        // QObject properties mark this object as being modified.
        ConfigObject(QObject *parent, bool watchDynamicProperties);
        bool eventFilter(QObject *obj, QEvent *event) override;
        void setWatchDynamicProperties(bool doWatch);

        virtual void read_impl(const QJsonObject &json) = 0;
        virtual void write_impl(QJsonObject &json) const = 0;


        QUuid m_id;
        bool m_modified = false;
        bool m_enabled = true;
        bool m_eventFilterInstalled = false;
};

// A generic container object used to hold more specific child objects or other
// containers. This can be used by the UI to structure the object tree.
//
// Note: when a child object is deleted it will automatically be removed from
// the list of children of this object.
class LIBMVME_EXPORT ContainerObject: public ConfigObject
{
    Q_OBJECT
    public:
        Q_INVOKABLE explicit ContainerObject(QObject *parent = nullptr);

        ContainerObject(const QString &name, const QString &displayName,
                        const QString &icon, QObject *parent = nullptr);

        void addChild(ConfigObject *obj)
        {
            m_children.push_back(obj);
            obj->setParent(this);
            connect(obj, &QObject::destroyed,
                    this, &ContainerObject::onChildDestroyed);
            setModified();
        }

        // Note: the child is neither deleted nor reparented. It is only
        // removed from this objects list of children.
        bool removeChild(ConfigObject *obj)
        {
            if (m_children.removeOne(obj))
            {
                disconnect(obj, &QObject::destroyed,
                           this, &ContainerObject::onChildDestroyed);
                setModified();
                return true;
            }
            return false;
        }

        bool containsChild(ConfigObject *obj)
        {
            return m_children.indexOf(obj) >= 0;
        }

        QVector<ConfigObject *> getChildren() const
        {
            return m_children;
        }

    protected:
        void read_impl(const QJsonObject &json) override;
        void write_impl(QJsonObject &json) const override;

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

class LIBMVME_EXPORT VMEScriptConfig: public ConfigObject
{
    Q_OBJECT
    public:
        Q_INVOKABLE VMEScriptConfig(QObject *parent = 0);
        VMEScriptConfig(const QString &name, const QString &contents, QObject *parent = 0);

        QString getScriptContents() const
        { return m_script; }

        void setScriptContents(const QString &);
        void addToScript(const QString &str);

        QString getVerboseTitle() const;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QString m_script;
};

class EventConfig;

class LIBMVME_EXPORT ModuleConfig: public ConfigObject
{
    Q_OBJECT
    public:
        Q_INVOKABLE ModuleConfig(QObject *parent = 0);

        uint32_t getBaseAddress() const { return m_baseAddress; }
        void setBaseAddress(uint32_t address);

        const vats::VMEModuleMeta getModuleMeta() const { return m_meta; }
        void setModuleMeta(const vats::VMEModuleMeta &meta);

        bool raisesIRQ() const { return m_raisesIRQ; }
        void setRaisesIRQ(bool b);

        VMEScriptConfig *getResetScript() const { return m_resetScript; }
        VMEScriptConfig *getReadoutScript() const { return m_readoutScript; }

        QVector<VMEScriptConfig *>  getInitScripts() const { return m_initScripts; }
        VMEScriptConfig *getInitScript(const QString &scriptName) const;
        VMEScriptConfig *getInitScript(s32 scriptIndex) const;

        void addInitScript(VMEScriptConfig *script);

        const EventConfig *getEventConfig() const;
        EventConfig *getEventConfig();
        QUuid getEventId() const;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        uint32_t m_baseAddress = 0;
        VMEScriptConfig *m_resetScript;
        VMEScriptConfig *m_readoutScript;
        QVector<VMEScriptConfig *> m_initScripts;
        vats::VMEModuleMeta m_meta;
        bool m_raisesIRQ = false;
};

class VMEConfig;

class LIBMVME_EXPORT EventConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void moduleAdded(ModuleConfig *module);
        void moduleAboutToBeRemoved(ModuleConfig *module);

    public:
        Q_INVOKABLE EventConfig(QObject *parent = nullptr);

        void addModuleConfig(ModuleConfig *config)
        {
            config->setParent(this);
            modules.push_back(config);
            emit moduleAdded(config);
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

        QList<ModuleConfig *> getModuleConfigs() const { return modules; }
        TriggerCondition triggerCondition = TriggerCondition::Interrupt;
        QVariantMap triggerOptions = QVariantMap();

        uint8_t irqLevel = 0;
        uint8_t irqVector = 0;
        // Maximum time between scaler stack executions in units of 0.5s
        uint8_t scalerReadoutPeriod = 2;
        // Maximum number of events between scaler stack executions
        uint16_t scalerReadoutFrequency = 0;

        // The most significant byte of the events VME multicast address.
        // Available as '${mcst}' is event and module vme scripts.
        u8 getMulticastByte() const { return m_mcst; }
        void setMulticastByte(u8 mcst);

        // The number of events to read out in one readout cycle.
        //
        // This is made available as an event-wide variable
        // ('readout_num_events') and is used by the mesytec module init
        // scripts to set the 'IRQ-FIFO-threshold' and 'max_transfer_data'
        // registers.
        u16 getReadoutNumEvents() const { return m_readoutNumEvents; }
        void setReadoutNumEvents(u16 numEvents);

        /** Known keys for an event:
         * "daq_start", "daq_stop", "readout_start", "readout_end"
         */
        QMap<QString, VMEScriptConfig *> vmeScripts;

        /* Set by the readout worker and then used by the buffer
         * processor to map from stack ids to event configs. */
        // Maybe should move this elsewhere as it is vmusb specific
        uint8_t stackID; // FIXME: vmusb only

        const VMEConfig *getVMEConfig() const;
        VMEConfig *getVMEConfig();

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QList<ModuleConfig *> modules;
        u8 m_mcst = 0;
        u16 m_readoutNumEvents = 1;
};

enum class VMEConfigReadResult
{
    NoError,
    VersionTooNew
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

        void globalScriptAdded(VMEScriptConfig *config, const QString &category);
        void globalScriptAboutToBeRemoved(VMEScriptConfig *config);

        void vmeControllerTypeSet(const VMEControllerType &t);

    public:
        Q_INVOKABLE VMEConfig(QObject *parent = 0);

        std::error_code readVMEConfig(const QJsonObject &json);

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
        QList<ModuleConfig *> getAllModuleConfigs() const;
        QPair<int, int> getEventAndModuleIndices(ModuleConfig *cfg) const;

        // scripts
        bool addGlobalScript(VMEScriptConfig *config, const QString &category);
        bool removeGlobalScript(VMEScriptConfig *config);

        // vme controller
        void setVMEController(VMEControllerType type,
                              const QVariantMap &settings = QVariantMap());
        VMEControllerType getControllerType() const { return m_controllerType; }
        QVariantMap getControllerSettings() const { return m_controllerSettings; }

#if 0
        // Pretty generic interface to hold global config objects.
        // Currently these are global vme scripts run at daq start/stop time or
        // manually and global devices like MVLCs trigger/IO module, mesytec RC
        // Bus <-> VME interface or ISEGS high voltage power supply.
        void addGlobalObject(ConfigObject *obj);
        bool removeGlobalObject(ConfigObject *obj);
        QVector<ConfigObject *> getGlobalObjects() const;
#endif

        const ContainerObject &getGlobalObjectRoot() const;
        ContainerObject &getGlobalObjectRoot();

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
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

LIBMVME_EXPORT std::pair<std::unique_ptr<VMEConfig>, QString>
    read_vme_config_from_file(const QString &filename);

QString make_unique_module_name(const QString &prefix, const VMEConfig *vmeConfig);

#endif
