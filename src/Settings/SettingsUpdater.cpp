/*
 * Copyright (C) 2019-2021 Ashar Khan <ashar786khan@gmail.com>
 *
 * This file is part of CP Editor.
 *
 * CP Editor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * I will not be responsible if CP Editor behaves in unexpected way and
 * causes your ratings to go down and or lose any important contest.
 *
 * Believe Software is "Software" and it isn't immune to bugs.
 *
 */

#include "Settings/SettingsUpdater.hpp"
#include "Core/SessionManager.hpp"
#include "Settings/SettingsManager.hpp"
#include "generated/SettingsHelper.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

void SettingsUpdater::updateSetting(QSettings &setting)
{
#ifdef QT_DEBUG
    // Check for key conflicts
    QSet<QString> keys;

    auto addKey = [&](const QString &key) {
        if (keys.contains(key))
            qFatal("Duplicate key in the settings: %s", // NOLINT: C-Style varargs should not be called.
                   key.toStdString().c_str());
        keys.insert(key);
    };

    for (const auto &si : qAsConst(SettingsInfo::settings))
    {
        addKey(si.key());
        std::for_each(si.old.begin(), si.old.end(), addKey);
    }
#endif

    for (const auto &si : qAsConst(SettingsInfo::settings))
    {
        if (!SettingsManager::contains(si.name))
        {
            for (const auto &old : qAsConst(si.old))
            {
                if (setting.contains(old))
                {
                    SettingsManager::set(si.name, setting.value(old));
                    break;
                }
            }
        }
    }

    if (setting.childGroups().contains("snippets"))
    {
        setting.beginGroup("snippets");
        auto langs = setting.childGroups();
        for (const QString &lang : langs)
        {
            setting.beginGroup(lang);
            auto obj = SettingsHelper::getLanguageConfig(lang == "Cpp" ? "C++" : lang);
            QStringList used = obj.getSnippets();
            for (const QString &key : setting.childKeys())
            {
                if (!used.contains(key))
                    obj.setSnippet(key, setting.value(key).toString());
            }
            setting.endGroup();
        }
        setting.endGroup();
    }

    if (setting.childGroups().contains("editor_status") && Core::SessionManager::lastSessionPath().isEmpty())
    {
        QJsonObject json;
        json.insert("currentIndex", setting.contains("hot_exit/current_index")
                                        ? setting.value("hot_exit/current_index").toInt()
                                        : setting.value("current_index").toInt());

        QJsonArray arr;

        setting.beginGroup("editor_status");
        auto indices = setting.childKeys();
        for (const auto &index : indices)
        {
            arr.push_back(QJsonDocument::fromVariant(setting.value(index).toMap()).object());
        }
        setting.endGroup();

        json.insert("tabs", arr);

        Core::SessionManager::saveSession(QJsonDocument(json).toJson());
    }

    if (setting.contains("save_path"))
    {
        if (SettingsHelper::getDefaultPathNamesAndPaths().isEmpty())
        {
            SettingsHelper::setDefaultPathNamesAndPaths(
                QVariantList{QStringList{"file", setting.value("save_path").toString()}});
        }
    }
}
