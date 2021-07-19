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

#ifndef APPWINDOW_HPP
#define APPWINDOW_HPP

#include "Widgets/ContestDialog.hpp"
#include "mainwindow.hpp"
#include <QMainWindow>
#include <QSystemTrayIcon>

class FindReplaceDialog;
class MessageLogger;
class PreferencesWindow;
class QMenu;
class QShortcut;
class QSplitter;
class QSystemTrayIcon;

namespace Ui
{
class AppWindow;
}

namespace Extensions
{
class CompanionServer;
struct CompanionData;
class LanguageServer;
} // namespace Extensions

namespace Telemetry
{
class UpdateChecker;
}

namespace Core
{
class SessionManager;
class FakeVimCommand;
class FakeVimProxy;
} // namespace Core

class AppWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit AppWindow(int depth, bool cpp, bool java, bool python, bool noHotExit, const QStringList &paths,
                       QWidget *parent = nullptr);
    explicit AppWindow(bool cpp, bool java, bool python, bool noHotExit, int number, const QString &path,
                       QWidget *parent = nullptr);
    ~AppWindow() override;

    PreferencesWindow *getPreferencesWindow() const;

  protected:
    void closeEvent(QCloseEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void changeEvent(QEvent *event) override;

  public slots:
    void onReceivedMessage(quint32 instanceId, QByteArray message);

    bool forceClose();

    void showOnTop();

    bool closeTab(int index, bool noConfirmQuit = false);

    bool closeWindow(MainWindow *window, bool noConfirmQuit = false);

  private slots:
    // UI Slots
    void on_actionSupportUs_triggered();

    void on_actionManual_triggered();

    void on_actionReportIssues_triggered();

    void on_actionAbout_triggered();

    void on_actionAboutQt_triggered();

    void on_actionBuildInfo_triggered();

    void on_actionQuit_triggered();

    void on_actionNewTab_triggered();

    void on_actionOpen_triggered();

    void on_actionOpenContest_triggered();

    void on_actionResetSettings_triggered();

    void on_actionExportSettings_triggered();

    void on_actionImportSettings_triggered();

    void on_actionExportSession_triggered();

    void on_actionLoadSession_triggered();

    void on_actionSettings_triggered();

    void on_actionSave_triggered();

    void on_actionSaveAs_triggered();

    void on_actionSaveAll_triggered();

    void on_actionCloseCurrent_triggered();

    void on_actionCloseAll_triggered();

    void on_actionCloseSaved_triggered();

    void on_actionCheckForUpdates_triggered();

    void on_actionCompile_triggered();

    void on_actionCompileRun_triggered();

    void on_actionRun_triggered();

    void on_actionFindReplace_triggered();

    void on_actionFormatCode_triggered();

    void on_actionRunDetached_triggered();

    void on_actionKillProcesses_triggered();

    void on_actionUseSnippets_triggered();

    void on_actionEditorMode_triggered();

    void on_actionIOMode_triggered();

    void on_actionSplitMode_triggered();

    void on_actionFullScreen_toggled(bool checked);

    void on_actionIndent_triggered();

    void on_actionUnindent_triggered();

    void on_actionSwapLineUp_triggered();

    void on_actionSwapLineDown_triggered();

    void on_actionDuplicateLine_triggered();

    void on_actionDeleteLine_triggered();

    void on_actionToggleComment_triggered();

    void on_actionToggleBlockComment_triggered();

    void on_actionShowLogs_triggered();

    void on_actionClearLogs_triggered();

    // Non-UI Slots

    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

    void onCompileOrRunTriggered();

    void onEditorFileChanged();

    void onConfirmTriggered(MainWindow *widget);

    void onTabContextMenuRequested(const QPoint &pos);

    void onEditorTextChanged(MainWindow *window);

    void updateLanguageServerFilePath(MainWindow *window, const QString &path);

    void onEditorLanguageChanged(MainWindow *window);

    void onTabChanged(int);

    void onLSPTimerElapsedCpp();

    void onLSPTimerElapsedPython();

    void onLSPTimerElapsedJava();

    void onSettingsApplied(const QString &pagePath);

    void onSplitterMoved();

    void onRightSplitterMoved();

    void onIncomingCompanionRequest(const Extensions::CompanionData &);

    void onViewModeToggle();

  private:
    Ui::AppWindow *ui;
    MessageLogger *activeLogger = nullptr;

    Core::SessionManager *sessionManager = nullptr;

    QTimer *lspTimerCpp = nullptr;
    QTimer *lspTimerPython = nullptr;
    QTimer *lspTimerJava = nullptr;

    QMetaObject::Connection activeSplitterMoveConnection;
    QMetaObject::Connection activeRightSplitterMoveConnection;

    Telemetry::UpdateChecker *updateChecker = nullptr;
    PreferencesWindow *preferencesWindow = nullptr;
    Extensions::CompanionServer *server = nullptr;
    FindReplaceDialog *findReplaceDialog = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMenu *trayIconMenu = nullptr;
    QMenu *tabMenu = nullptr;

    Extensions::LanguageServer *cppServer = nullptr;
    Extensions::LanguageServer *javaServer = nullptr;
    Extensions::LanguageServer *pythonServer = nullptr;

    explicit AppWindow(bool noHotExit, QWidget *parent = nullptr);
    void finishConstruction();

    void setConnections();
    void allocate();
    void applySettings();
    void saveSettings();
    QVector<QShortcut *> hotkeyObjects;
    void maybeSetHotkeys();
    void openTab(MainWindow *window);
    void openTab(const MainWindow::EditorStatus &status, bool duplicate = false);
    void openTab(const QString &path, const QString &lang = "");
    void openTabs(const QStringList &paths);
    void openPaths(const QStringList &paths, bool cpp = true, bool java = true, bool python = true, int depth = -1);
    QStringList openFolder(const QString &path, bool cpp, bool java, bool python, int depth);
    void openContest(Widgets::ContestDialog::ContestData const &data);
    bool quit();
    int getNewUntitledIndex();
    void reAttachLanguageServer(MainWindow *window);

    void setTabAt(int index);
    MainWindow *currentWindow();
    MainWindow *windowAt(int index);
    int indexOfWindow(MainWindow *window);
    int tabCount() const;

    friend class Core::SessionManager;
    friend class Core::FakeVimCommand;
    friend class Core::FakeVimProxy;
};

#endif // APPWINDOW_HPP
