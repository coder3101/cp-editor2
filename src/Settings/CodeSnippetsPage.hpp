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

#ifndef CODESNIPPETSPAGE_HPP
#define CODESNIPPETSPAGE_HPP

#include "Settings/PreferencesPage.hpp"
#include <QMap>

namespace Editor
{
class CodeEditor;
}
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QSplitter;
class QStackedWidget;

class CodeSnippetsPage : public PreferencesPage
{
    Q_OBJECT

  public:
    explicit CodeSnippetsPage(QString language, QWidget *parent = nullptr);

  private:
    bool areSettingsChanged() override;

    void makeUITheSameAsDefault() override;

    void makeUITheSameAsSettings() override;

    void makeSettingsTheSameAsUI() override;

  private slots:
    /**
     * @brief select the first snippet with a name starts with text
     */
    void updateSearch(const QString &text);

    /**
     * @brief switch to snippet and warn if there are unsaved changes
     * @param item the item to switch to, nullptr for switching to the "empty"
     * @param force if true, won't ask for conformation
     * @returns succeeded or not, it may fail because of unsaved changes
     */
    bool switchToSnippet(QListWidgetItem *item, bool force = false);

    /**
     * @brief ask for name and add a snippet
     * @note may ask to change a name if it's duplicated
     */
    void addSnippet();

    /**
     * @brief ask for confirmation and delete the current snippet
     */
    void deleteSnippet();

    /**
     * @brief ask for new name and rename the current snippet
     * @note may ask to change a name if it's duplicated
     */
    void renameSnippet();

    /**
     * @brief ask for files and load them from files
     * @note may ask to change a name if it's duplicated
     */
    void loadSnippetsFromFiles();

    /**
     * @brief ask for directory and extract to files
     * @note may ask to change a file name if it's duplicated
     */
    void extractSnippetsToFiles();

  private:
    /**
     * @brief check it's able to switch to another snippet, warn if there are unsaved changes
     * @returns able to switch or not
     */
    bool aboutToSwitchToSnippet();

    /**
     * @brief get a non-empty unused snippet name
     * @param oldName the old name used for the first-time place holder
     * @param avoidConflictWithSettings avoid used names in the settings as well as in the UI
     * @param askOverride when the name is conflicted, ask whether to override the old snippet or not
     * @returns the name if succeeded, null QString if failed
     */
    QString getNewSnippetName(const QString &oldName = QString(), bool avoidConflictWithSettings = false,
                              bool askOverride = true);

    /**
     * @brief add a new snippet without asking for confirmation
     * @param name the name of the new snippet
     * @note updateButtons() is called in this
     */
    void addSnippet(const QString &name);

    /**
     * @brief delete a snippet without asking for confirmation
     * @param item the item to the snippet
     * @note updateButtons() is called in this
     */
    void deleteSnippet(QListWidgetItem *item);

    /**
     * @brief set enabled for deleleButton, renameAction and extractSnippetsToFilesAction
     */
    void updateActions();

  private:
    /**
     * splitter:
     *   - leftWidget
     *     - leftLayout
     *       - searchEdit
     *       - snippetsList
     *       - buttonsLayout
     *         - addButton
     *         - deleteButton
     *         - moreButton
     *   - rightWidget
     *     - snippetWidget
     *       - snippetLayout
     *         - snippetNameLabel
     *         - editor
     *     - noSnippetWidget
     *       - VStretchLayout
     *         - (stretch)
     *         - HStretchLayout
     *           - (stretch)
     *           - noSnippetLabel
     *           - (stretch)
     *         - (stretch)
     */

    QLineEdit *searchEdit = nullptr;
    QListWidget *snippetsList = nullptr;
    QPushButton *deleteButton = nullptr;
    QMenu *moreMenu = nullptr;
    QAction *renameAction = nullptr;
    QAction *loadSnippetsFromFilesAction = nullptr;
    QAction *extractSnippetsToFilesAction = nullptr;
    QStackedWidget *rightWidget = nullptr;
    QWidget *snippetWidget = nullptr;
    QLabel *snippetNameLabel = nullptr;
    Editor::CodeEditor *editor = nullptr;
    QWidget *noSnippetWidget = nullptr;
    QLabel *noSnippetLabel = nullptr;

    QString lang;
    QString currentSnippetName;
    QListWidgetItem *currentItem = nullptr;
    QMap<QString, QListWidgetItem *> snippetItem;
};

#endif // CODESNIPPETSPAGE_HPP
