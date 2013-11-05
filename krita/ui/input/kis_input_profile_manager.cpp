/*
 * This file is part of the KDE project
 * Copyright (C) 2013 Arjen Hiemstra <ahiemstra@heimr.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_input_profile_manager.h"
#include "kis_input_profile.h"

#include <QMap>
#include <QStringList>
#include <QDir>

#include <kglobal.h>
#include <kstandarddirs.h>
#include <kconfig.h>
#include <kconfiggroup.h>
#include <kcomponentdata.h>

#include "kis_config.h"
#include "kis_alternate_invocation_action.h"
#include "kis_change_primary_setting_action.h"
#include "kis_pan_action.h"
#include "kis_rotate_canvas_action.h"
#include "kis_show_palette_action.h"
#include "kis_tool_invocation_action.h"
#include "kis_zoom_action.h"
#include "kis_shortcut_configuration.h"
#include "kis_select_layer_action.h"


class KisInputProfileManager::Private
{
public:
    Private() : currentProfile(0) { }

    void createActions();
    QString profileFileName(const QString &profileName);

    KisInputProfile *currentProfile;

    QMap<QString, KisInputProfile *> profiles;

    QList<KisAbstractInputAction *> actions;
};

K_GLOBAL_STATIC(KisInputProfileManager, inputProfileManager)

KisInputProfileManager *KisInputProfileManager::instance()
{
    return inputProfileManager;
}

QList< KisInputProfile * > KisInputProfileManager::profiles() const
{
    return d->profiles.values();
}

QStringList KisInputProfileManager::profileNames() const
{
    return d->profiles.keys();
}

KisInputProfile *KisInputProfileManager::profile(const QString &name) const
{
    if (d->profiles.contains(name)) {
        return d->profiles.value(name);
    }

    return 0;
}

KisInputProfile *KisInputProfileManager::currentProfile() const
{
    return d->currentProfile;
}

void KisInputProfileManager::setCurrentProfile(KisInputProfile *profile)
{
    if (profile && profile != d->currentProfile) {
        d->currentProfile = profile;
        emit currentProfileChanged();
    }
}

KisInputProfile *KisInputProfileManager::addProfile(const QString &name)
{
    if (d->profiles.contains(name)) {
        return d->profiles.value(name);
    }

    KisInputProfile *profile = new KisInputProfile(this);
    profile->setName(name);
    d->profiles.insert(name, profile);

    emit profilesChanged();

    return profile;
}

void KisInputProfileManager::removeProfile(const QString &name)
{
    if (d->profiles.contains(name)) {
        QString currentProfileName = d->currentProfile->name();

        delete d->profiles.value(name);
        d->profiles.remove(name);

        //Delete the settings file for the removed profile, if it exists
        QDir userDir(KGlobal::dirs()->saveLocation("appdata", "input/"));

        if (userDir.exists(d->profileFileName(name))) {
            userDir.remove(d->profileFileName(name));
        }

        if (currentProfileName == name) {
            d->currentProfile = d->profiles.begin().value();
            emit currentProfileChanged();
        }

        emit profilesChanged();
    }
}

bool KisInputProfileManager::renameProfile(const QString &oldName, const QString &newName)
{
    if (!d->profiles.contains(oldName)) {
        return false;
    }

    KisInputProfile *profile = d->profiles.value(oldName);
    d->profiles.remove(oldName);
    profile->setName(newName);
    d->profiles.insert(newName, profile);

    emit profilesChanged();

    return true;
}

void KisInputProfileManager::duplicateProfile(const QString &name, const QString &newName)
{
    if (!d->profiles.contains(name) || d->profiles.contains(newName)) {
        return;
    }

    KisInputProfile *newProfile = new KisInputProfile(this);
    newProfile->setName(newName);
    d->profiles.insert(newName, newProfile);

    KisInputProfile *profile = d->profiles.value(name);
    QList<KisShortcutConfiguration *> shortcuts = profile->allShortcuts();
    Q_FOREACH(KisShortcutConfiguration * shortcut, shortcuts) {
        newProfile->addShortcut(new KisShortcutConfiguration(*shortcut));
    }

    emit profilesChanged();
}

QList< KisAbstractInputAction * > KisInputProfileManager::actions()
{
    return d->actions;
}

void KisInputProfileManager::loadProfiles()
{
    //Remove any profiles that already exist
    d->currentProfile = 0;
    qDeleteAll(d->profiles);
    d->profiles.clear();

    //Look up all profiles (this includes those installed to $prefix as well as the user's local data dir)
    QStringList profiles = KGlobal::mainComponent().dirs()->findAllResources("appdata", "input/*", KStandardDirs::NoDuplicates | KStandardDirs::Recursive);
    Q_FOREACH(const QString & p, profiles) {
        //Open the file
        KConfig config(p, KConfig::SimpleConfig);

        if (!config.hasGroup("General") || !config.group("General").hasKey("name")) {
            //Skip if we don't have the proper settings.
            continue;
        }

        KisInputProfile *newProfile = addProfile(config.group("General").readEntry("name"));
        Q_FOREACH(KisAbstractInputAction * action, d->actions) {
            if (!config.hasGroup(action->name())) {
                continue;
            }

            KConfigGroup grp = config.group(action->name());
            //Read the settings for the action and create the appropriate shortcuts.
            Q_FOREACH(const QString & entry, grp.entryMap()) {
                KisShortcutConfiguration *shortcut = new KisShortcutConfiguration;
                shortcut->setAction(action);

                if (shortcut->unserialize(entry)) {
                    newProfile->addShortcut(shortcut);
                }
                else {
                    delete shortcut;
                }
            }
        }
    }

    KisConfig cfg;
    QString currentProfile = cfg.currentInputProfile();
    if (d->profiles.size() > 0) {
        if (currentProfile.isEmpty() || !d->profiles.contains(currentProfile)) {
            d->currentProfile = d->profiles.begin().value();
        }
        else {
            d->currentProfile = d->profiles.value(currentProfile);
        }
    }
    if (d->currentProfile) {
        emit currentProfileChanged();
    }
}

void KisInputProfileManager::saveProfiles()
{
    QString storagePath = KGlobal::dirs()->saveLocation("appdata", "input/");
    Q_FOREACH(KisInputProfile * p, d->profiles) {
        QString fileName = d->profileFileName(p->name());
        KConfig config(storagePath + fileName, KConfig::SimpleConfig);

        config.group("General").writeEntry("name", p->name());

        Q_FOREACH(KisAbstractInputAction * action, d->actions) {
            KConfigGroup grp = config.group(action->name());
            grp.deleteGroup(); //Clear the group of any existing shortcuts.

            int index = 0;
            QList<KisShortcutConfiguration *> shortcuts = p->shortcutsForAction(action);
            Q_FOREACH(KisShortcutConfiguration * shortcut, shortcuts) {
                grp.writeEntry(QString("%1").arg(index++), shortcut->serialize());
            }
        }

        config.sync();
    }

    KisConfig config;
    config.setCurrentInputProfile(d->currentProfile->name());

    //Force a reload of the current profile in input manager and whatever else uses the profile.
    emit currentProfileChanged();
}

KisInputProfileManager::KisInputProfileManager(QObject *parent)
    : QObject(parent), d(new Private())
{
    d->createActions();
}

KisInputProfileManager::~KisInputProfileManager()
{
    qDeleteAll(d->profiles);
    qDeleteAll(d->actions);
    delete d;
}

void KisInputProfileManager::Private::createActions()
{
    //TODO: Make this plugin based
    //Note that the ordering here determines how things show up in the UI
    actions.append(new KisToolInvocationAction());
    actions.append(new KisAlternateInvocationAction());
    actions.append(new KisChangePrimarySettingAction());
    actions.append(new KisPanAction());
    actions.append(new KisRotateCanvasAction());
    actions.append(new KisZoomAction());
    actions.append(new KisShowPaletteAction());
    actions.append(new KisSelectLayerAction());
}

QString KisInputProfileManager::Private::profileFileName(const QString &profileName)
{
    return profileName.toLower().remove(QRegExp("[^a-z0-9]")).append(".profile");
}