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

#include "Settings/CodeSnippetsPage.hpp"
#include "Editor/CodeEditor.hpp"
#include "Settings/DefaultPathManager.hpp"
#include "Util/FileUtil.hpp"
#include "generated/SettingsHelper.hpp"
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

CodeSnippetsPage::CodeSnippetsPage(QString language, QWidget *parent)
    : PreferencesPage(parent), lang(std::move(language))
{
    auto *splitter = new QSplitter();
    addWidget(splitter);

    auto *leftWidget = new QWidget();
    splitter->addWidget(leftWidget);

    auto *leftLayout = new QVBoxLayout(leftWidget);

    searchEdit = new QLineEdit();
    searchEdit->setPlaceholderText(tr("Search..."));
    connect(searchEdit, &QLineEdit::textChanged, this, &CodeSnippetsPage::updateSearch);
    leftLayout->addWidget(searchEdit);

    snippetsList = new QListWidget();
    snippetsList->setSortingEnabled(true);
    connect(snippetsList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { switchToSnippet(item); });
    connect(snippetsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { switchToSnippet(item); });
    leftLayout->addWidget(snippetsList);

    auto *buttonsLayout = new QHBoxLayout();
    leftLayout->addLayout(buttonsLayout);

    auto *addButton = new QPushButton(tr("Add"));
    addButton->setShortcut({"Ctrl+N"});
    connect(addButton, &QPushButton::clicked, this, [this] { addSnippet(); });
    buttonsLayout->addWidget(addButton);

    deleteButton = new QPushButton(tr("Del"));
    deleteButton->setShortcut({"Ctrl+W"});
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteSnippet(); });
    buttonsLayout->addWidget(deleteButton);

    renameAction = new QAction(tr("Rename Snippet"));
    renameAction->setShortcut({"F2"});
    connect(renameAction, &QAction::triggered, this, &CodeSnippetsPage::renameSnippet);

    loadSnippetsFromFilesAction = new QAction(tr("Load Snippets From Files"));
    connect(loadSnippetsFromFilesAction, &QAction::triggered, this, &CodeSnippetsPage::loadSnippetsFromFiles);

    extractSnippetsToFilesAction = new QAction(tr("Extract Snippets To Files"));
    connect(extractSnippetsToFilesAction, &QAction::triggered, this, &CodeSnippetsPage::extractSnippetsToFiles);

    updateActions();

    moreMenu = new QMenu();
    moreMenu->addAction(renameAction);
    moreMenu->addAction(loadSnippetsFromFilesAction);
    moreMenu->addAction(extractSnippetsToFilesAction);

    auto *moreButton = new QPushButton(tr("More"));
    moreButton->setMenu(moreMenu);
    buttonsLayout->addWidget(moreButton);

    rightWidget = new QStackedWidget();
    splitter->addWidget(rightWidget);

    snippetWidget = new QWidget();
    rightWidget->addWidget(snippetWidget);

    auto *snippetLayout = new QVBoxLayout(snippetWidget);

    snippetNameLabel = new QLabel();
    snippetLayout->addWidget(snippetNameLabel);

    editor = new Editor::CodeEditor();
    connect(editor, &Editor::CodeEditor::textChanged, this, &CodeSnippetsPage::updateButtons);
    snippetLayout->addWidget(editor);

    noSnippetWidget = new QWidget();
    rightWidget->addWidget(noSnippetWidget);
    rightWidget->setCurrentWidget(noSnippetWidget);

    auto *VStretchLayout = new QVBoxLayout(noSnippetWidget);
    VStretchLayout->addStretch();

    auto *HStretchLayout = new QHBoxLayout();
    VStretchLayout->addLayout(HStretchLayout);
    HStretchLayout->addStretch();

    noSnippetLabel = new QLabel(tr("No Snippet Selected"));
    HStretchLayout->addWidget(noSnippetLabel);

    HStretchLayout->addStretch();

    VStretchLayout->addStretch();

    splitter->setSizes({10000, 30000});
}

bool CodeSnippetsPage::areSettingsChanged()
{
    auto settingsKeys = SettingsHelper::getLanguageConfig(lang).getSnippets();
    auto UIkeys = snippetItem.keys();
    if (settingsKeys != UIkeys)
        return true;
    if (currentItem == nullptr)
        return false;
    return editor->toPlainText() != SettingsHelper::getLanguageConfig(lang).getSnippet(currentItem->text());
}

void CodeSnippetsPage::makeUITheSameAsDefault()
{
    if (switchToSnippet(nullptr))
    {
        snippetsList->clear();
        snippetItem.clear();

        currentItem = nullptr;
        updateActions();
        updateButtons();
    }
}

void CodeSnippetsPage::makeUITheSameAsSettings()
{
    editor->applySettings(lang);
    auto settingsKeys = SettingsHelper::getLanguageConfig(lang).getSnippets();
    for (auto const &key : settingsKeys)
    {
        if (!snippetItem.contains(key))
        {
            addSnippet(key);
        }
    }
    auto UIKeys = snippetItem.keys();
    for (auto const &key : UIKeys)
    {
        if (!settingsKeys.contains(key))
        {
            deleteSnippet(snippetItem[key]);
        }
    }
    if (currentItem != nullptr)
    {
        auto cursor = editor->textCursor();
        cursor.select(QTextCursor::Document);
        cursor.insertText(SettingsHelper::getLanguageConfig(lang).getSnippet(currentItem->text()));
    }
}

void CodeSnippetsPage::makeSettingsTheSameAsUI()
{
    auto settingsKeys = SettingsHelper::getLanguageConfig(lang).getSnippets();
    for (auto const &key : settingsKeys)
    {
        if (!snippetItem.contains(key))
        {
            SettingsHelper::getLanguageConfig(lang).removeSnippet(key);
        }
    }
    auto UIKeys = snippetItem.keys();
    for (auto const &key : UIKeys)
    {
        if (!settingsKeys.contains(key))
        {
            SettingsHelper::getLanguageConfig(lang).setSnippet(key, QString());
        }
    }
    if (currentItem != nullptr)
        SettingsHelper::getLanguageConfig(lang).setSnippet(currentItem->text(), editor->toPlainText());
    updateButtons();
}

void CodeSnippetsPage::updateSearch(const QString &text)
{
    if (text.isEmpty())
        return;
    for (int i = 0; i < snippetsList->count(); ++i)
    {
        auto *item = snippetsList->item(i);
        if (item->text().startsWith(text, Qt::CaseInsensitive))
        {
            snippetsList->setCurrentItem(item);
            break;
        }
    }
}

bool CodeSnippetsPage::switchToSnippet(QListWidgetItem *item, bool force)
{
    if (item == currentItem)
        return true;

    if (force || aboutToSwitchToSnippet())
    {
        snippetsList->setCurrentItem(item);
        currentItem = item;
        if (item == nullptr)
        {
            rightWidget->setCurrentWidget(noSnippetWidget);
        }
        else
        {
            rightWidget->setCurrentWidget(snippetWidget);
            snippetNameLabel->setText(item->text());
            editor->setPlainText(SettingsHelper::getLanguageConfig(lang).getSnippet(item->text()));
        }
        updateActions();
        updateButtons();
        return true;
    }

    return false;
}

bool CodeSnippetsPage::aboutToSwitchToSnippet()
{
    if (currentItem == nullptr)
        return true;

    if (SettingsHelper::getLanguageConfig(lang).getSnippets().contains(currentItem->text()) &&
        editor->toPlainText() == SettingsHelper::getLanguageConfig(lang).getSnippet(currentItem->text()))
        return true;

    if (!SettingsHelper::getLanguageConfig(lang).getSnippets().contains(currentItem->text()) &&
        editor->toPlainText().isEmpty())
        return true;

    auto res =
        QMessageBox::question(this, tr("Unsaved Snippets"),
                              tr("The snippet [%1] has been changed. Do you want to save it or discard the changes?")
                                  .arg(currentItem->text()),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (res)
    {
    case QMessageBox::Save:
        SettingsHelper::getLanguageConfig(lang).setSnippet(currentItem->text(), editor->toPlainText());
        updateButtons();
        emit settingsApplied(path());
        return true;
    case QMessageBox::Discard:
        return true;
    case QMessageBox::Cancel:
        return false;
    default:
        Q_UNREACHABLE();
        break;
    }
}

void CodeSnippetsPage::addSnippet()
{
    auto name = getNewSnippetName(QString(), false, false);
    if (!name.isEmpty())
        addSnippet(name);
}

void CodeSnippetsPage::deleteSnippet()
{
    if (currentItem == nullptr)
        return;
    auto res = QMessageBox::question(this, tr("Delete Snippet"),
                                     tr("Do you really want to delete the snippet [%1]?").arg(currentItem->text()),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
        deleteSnippet(currentItem);
}

void CodeSnippetsPage::renameSnippet()
{
    if (currentItem == nullptr)
        return;

    auto oldName = currentItem->text();
    snippetItem.remove(oldName); // Remove from the keys so that you can rename to the old name
    auto newName = getNewSnippetName(oldName);
    if (newName.isEmpty() || newName == oldName)
    {
        snippetItem[oldName] = currentItem;
        return;
    }
    delete currentItem;
    currentItem = new QListWidgetItem(newName);
    snippetsList->addItem(currentItem);
    snippetsList->setCurrentItem(currentItem);
    snippetItem[newName] = currentItem;
    updateButtons();
}

void CodeSnippetsPage::loadSnippetsFromFiles()
{
    auto files =
        DefaultPathManager::getOpenFileNames("Extract And Load Snippets", this, tr("Load Snippets"),
                                             Util::fileNameFilter(lang == "C++", lang == "Java", lang == "Python"));

    for (auto const &file : files)
    {
        auto content = Util::readFile(file, "CodeSnippetsPage::loadSnippetsFromFiles");
        if (content.isNull())
        {
            QMessageBox::warning(this, tr("Load Snippets"),
                                 tr("Failed to open [%1]. Do I have read permission?").arg(file));
            break;
        }
        auto name = QFileInfo(file).completeBaseName();
        if (snippetItem.contains(name) || SettingsHelper::getLanguageConfig(lang).getSnippets().contains(name))
            name = getNewSnippetName(name, true);
        if (name.isEmpty())
            break;
        addSnippet(name);
        SettingsHelper::getLanguageConfig(lang).setSnippet(name, content);
    }
}

void CodeSnippetsPage::extractSnippetsToFiles()
{
    auto dirPath = DefaultPathManager::getExistingDirectory("Extract And Load Snippets", this, tr("Extract Snippets"));

    if (dirPath.isEmpty())
        return;

    auto dir = QDir(dirPath);

    auto names = SettingsHelper::getLanguageConfig(lang).getSnippets();

    QString suffix;
    if (lang == "C++")
        suffix = "." + Util::cppSuffix.front();
    else if (lang == "Java")
        suffix = "." + Util::javaSuffix.front();
    else if (lang == "Python")
        suffix = "." + Util::pythonSuffix.front();

    for (auto const &name : names)
    {
        auto filePath = dir.filePath(name + suffix);
        if (QFile::exists(filePath))
        {
            filePath =
                QFileDialog::getSaveFileName(this, tr("Extract Snippets: %1").arg(name), filePath,
                                             Util::fileNameFilter(lang == "C++", lang == "Java", lang == "Python"));
        }
        if (filePath.isEmpty())
            break;
        if (!Util::saveFile(filePath, SettingsHelper::getLanguageConfig(lang).getSnippet(name), "Extract Snippets"))
        {
            QMessageBox::warning(this, tr("Extract Snippets"),
                                 tr("Failed to write to [%1]. Do I have write permission?").arg(filePath));
            break;
        }
    }
}

QString CodeSnippetsPage::getNewSnippetName(const QString &oldName, bool avoidConflictWithSettings, bool askOverride)
{
    QString name;
    bool firstTime = true;
    while (name.isEmpty() || snippetItem.contains(name) ||
           (avoidConflictWithSettings && SettingsHelper::getLanguageConfig(lang).getSnippets().contains(name)))
    {
        bool ok = false;
        QString head;
        if (firstTime)
        {
            firstTime = false;
            name = oldName;
        }
        else if (name.isEmpty())
            head = tr("Snippet name can't be empty.\n");
        else
        {
            if (askOverride)
            {
                auto res = QMessageBox::question(this, tr("Snippet Name Conflict"),
                                                 tr("The name \"%1\" is already in use. Do you want to override it? "
                                                    "(The old snippet with this name will be deleted.)")
                                                     .arg(name),
                                                 QMessageBox::Yes | QMessageBox::No);
                if (res == QMessageBox::Yes)
                {
                    auto *item = snippetItem[name];
                    if (item)
                        deleteSnippet(item);
                    return name;
                }
            }
            head = tr("The name \"%1\" is already in use.\n").arg(name);
        }
        name = QInputDialog::getText(this, tr("Add Snippet"), head + tr("New Snippet Name:"), QLineEdit::Normal, name,
                                     &ok);
        if (!ok)
            return QString();
    }
    return name;
}

void CodeSnippetsPage::addSnippet(const QString &name)
{
    auto *item = new QListWidgetItem(name);
    snippetsList->addItem(item);
    snippetItem[name] = item;

    updateActions();
    updateButtons();
}

void CodeSnippetsPage::deleteSnippet(QListWidgetItem *item)
{
    if (item)
    {
        if (item == currentItem)
        {
            int row = snippetsList->row(item);
            if (row + 1 < snippetsList->count())
                switchToSnippet(snippetsList->item(row + 1), true);
            else if (row >= 1)
                switchToSnippet(snippetsList->item(0), true);
            else
                switchToSnippet(nullptr, true);
        }
        snippetItem.remove(item->text());
        delete item;

        updateActions();
        updateButtons();
    }
}

void CodeSnippetsPage::updateActions()
{
    deleteButton->setEnabled(currentItem != nullptr);
    renameAction->setEnabled(currentItem != nullptr);
    extractSnippetsToFilesAction->setEnabled(!snippetItem.isEmpty());
}
