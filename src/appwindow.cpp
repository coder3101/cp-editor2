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

#include "appwindow.hpp"
#include "../ui/ui_appwindow.h"
#include "Core/Compiler.hpp"
#include "Core/EventLogger.hpp"
#include "Core/MessageLogger.hpp"
#include "Core/SessionManager.hpp"
#include "Core/StyleManager.hpp"
#include "Core/Translator.hpp"
#include "Extensions/CFTool.hpp"
#include "Extensions/CompanionServer.hpp"
#include "Extensions/LanguageServer.hpp"
#include "Settings/DefaultPathManager.hpp"
#include "Settings/FileProblemBinder.hpp"
#include "Settings/PreferencesWindow.hpp"
#include "Telemetry/UpdateChecker.hpp"
#include "Util/FileUtil.hpp"
#include "Util/Util.hpp"
#include "Widgets/SupportUsDialog.hpp"
#include "application.hpp"
#include "generated/SettingsHelper.hpp"
#include "generated/portable.hpp"
#include "generated/version.hpp"
#include <QClipboard>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFontDatabase>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QShortcut>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <findreplacedialog.h>

AppWindow::AppWindow(bool noHotExit, QWidget *parent) : QMainWindow(parent), ui(new Ui::AppWindow)
{
    LOG_INFO(BOOL_INFO_OF(noHotExit))
    ui->setupUi(this);
    setAcceptDrops(true);
    allocate();
    setConnections();

#ifdef Q_OS_MACOS
    ui->actionSwapLineUp->setShortcut({"Ctrl+Meta+Up"});
    ui->actionSwapLineDown->setShortcut({"Ctrl+Meta+Down"});
#endif

    auto *separator = ui->menuFile->insertSeparator(ui->actionSave); // used to insert openRecentFilesMenu
    auto *openRecentFilesMenu = new QMenu(tr("Open Recent Files"), ui->menuFile);
    ui->menuFile->insertMenu(separator, openRecentFilesMenu);
    connect(openRecentFilesMenu, &QMenu::aboutToShow, [this, openRecentFilesMenu] {
        openRecentFilesMenu->clear();
        if (SettingsHelper::getRecentFiles().isEmpty())
        {
            openRecentFilesMenu->addAction(tr("No file is opened recently"))->setDisabled(true);
        }
        else
        {
            for (const auto &recentFile : SettingsHelper::getRecentFiles())
            {
                openRecentFilesMenu->addAction(recentFile, [this, recentFile] { openTab(recentFile); });
            }
        }
        openRecentFilesMenu->addSeparator();
        openRecentFilesMenu->addAction(tr("Clear Recent Files"), [] { SettingsHelper::setRecentFiles({}); });
    });

    Core::StyleManager::setDefault();

    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#ifdef Q_OS_MACOS
    setWindowIcon(QIcon(":/macos-icon.png"));
#else
    setWindowIcon(QIcon(":/icon.png"));
#endif

#ifdef Q_OS_WIN
    // setWindowOpacity(0.99) when opacity should be 100 is a workaround for a strange issue on Windows
    // The behavior: If the opacity is 100, and the window is maximized, it will resize to smaller than maximized
    if (SettingsHelper::getOpacity() < 100)
        setWindowOpacity(SettingsHelper::getOpacity() / 100.0);
    else
        setWindowOpacity(0.99);
#else
    setWindowOpacity(SettingsHelper::getOpacity() / 100.0);
#endif

    applySettings();
    onSettingsApplied("");

    if (SettingsHelper::isCheckUpdate())
        updateChecker->checkUpdate(true);

    if (noHotExit || (!SettingsHelper::isForceClose() && !SettingsHelper::isHotExitEnable()))
        return;

    SettingsHelper::setForceClose(false);

    auto lastSessionPath = Core::SessionManager::lastSessionPath();

    if (lastSessionPath.isEmpty())
        return;

    if (!SettingsHelper::isHotExitEnable())
    {
        auto res = QMessageBox::question(
            this, tr("Hot Exit"),
            tr("In the last session, CP Editor was abnormally killed, do you want to restore the last session?"),
            QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::No)
            return;
    }

    sessionManager->restoreSession(lastSessionPath);
}

AppWindow::AppWindow(int depth, bool cpp, bool java, bool python, bool noHotExit, const QStringList &paths,
                     QWidget *parent)
    : AppWindow(noHotExit, parent)
{
    openPaths(paths, cpp, java, python, depth);
    finishConstruction();
}

AppWindow::AppWindow(bool cpp, bool java, bool python, bool noHotExit, int number, const QString &path, QWidget *parent)
    : AppWindow(noHotExit, parent)
{
    QString lang = SettingsHelper::getDefaultLanguage();
    if (cpp)
        lang = "C++";
    else if (java)
        lang = "Java";
    else if (python)
        lang = "Python";
    openContest({path, number, lang});

    finishConstruction();
}

void AppWindow::finishConstruction()
{
    if (tabCount() == 0)
        openTab("");

#ifdef Q_OS_WIN
    // This is necessary because of setWindowOpacity(0.99) earlier
    if (SettingsHelper::getOpacity() == 100)
        setWindowOpacity(1);
#endif

    // The window needs time to make its geometry stable. We wait it to display the new dialogs in correct positions
    QTimer::singleShot(200, [this] {
        if (SettingsHelper::isFirstTimeUser())
        {
            LOG_INFO("Is first-time user");
            preferencesWindow->display();
            SettingsHelper::setFirstTimeUser(false);
        }
        else if (!SettingsHelper::isPromotionDialogShown() &&
                 SettingsHelper::getTotalUsageTime() >= 10 * 60 * 60) // 10 hours or above
        {
            LOG_INFO("Show promotion dialog");
            auto *dialog = new SupportUsDialog(this);
            dialog->open();
            dialog->move(geometry().center().x() - dialog->width() / 2, geometry().center().y() - dialog->height() / 2);
            SettingsHelper::setPromotionDialogShown(true);
        }
    });
}

AppWindow::~AppWindow()
{
    LOG_INFO("Destruction started");
    saveSettings();
    while (tabCount())
    {
        auto *tmp = ui->tabWidget->widget(0);
        ui->tabWidget->removeTab(0);
        delete tmp;
    }
    delete ui;
    delete preferencesWindow;
    delete lspTimerCpp;
    delete lspTimerJava;
    delete lspTimerPython;
    delete cppServer;
    delete pythonServer;
    delete javaServer;
    delete updateChecker;
    delete server;
    delete findReplaceDialog;
    delete sessionManager;

    SettingsManager::deinit();

    LOG_INFO("Destruction finished");
}

/******************* PUBLIC METHODS ***********************/

void AppWindow::closeEvent(QCloseEvent *event)
{
    if (quit())
        event->accept();
    else
        event->ignore();
}

void AppWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

PreferencesWindow *AppWindow::getPreferencesWindow() const
{
    return preferencesWindow;
}

void AppWindow::dropEvent(QDropEvent *event)
{
    LOG_INFO("Files are being dropped to editor");
    auto urls = event->mimeData()->urls();
    QStringList paths;
    for (auto &e : urls)
        paths.append(e.toLocalFile());
    openPaths(paths);
}

void AppWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        ui->actionFullScreen->setChecked(windowState().testFlag(Qt::WindowFullScreen));
    }
    QMainWindow::changeEvent(event);
}

/******************** PRIVATE METHODS ********************/
void AppWindow::setConnections()
{
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &AppWindow::onTabChanged);
    ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this, &AppWindow::onTabContextMenuRequested);

    connect(lspTimerCpp, &QTimer::timeout, this, &AppWindow::onLSPTimerElapsedCpp);
    connect(lspTimerJava, &QTimer::timeout, this, &AppWindow::onLSPTimerElapsedJava);
    connect(lspTimerPython, &QTimer::timeout, this, &AppWindow::onLSPTimerElapsedPython);

    connect(preferencesWindow, &PreferencesWindow::settingsApplied, this, &AppWindow::onSettingsApplied);

    connect(server, &Extensions::CompanionServer::onRequestArrived, this, &AppWindow::onIncomingCompanionRequest);

    connect(trayIcon, &QSystemTrayIcon::activated, this, &AppWindow::onTrayIconActivated);
    connect(trayIcon, &QSystemTrayIcon::messageClicked, this, &AppWindow::showOnTop);

    connect(qobject_cast<Application *>(qApp), &Application::requestOpenFile, this,
            qOverload<const QString &>(&AppWindow::openTab));
}

void AppWindow::allocate()
{
    lspTimerCpp = new QTimer();
    lspTimerJava = new QTimer();
    lspTimerPython = new QTimer();
    updateChecker = new Telemetry::UpdateChecker();
    preferencesWindow = new PreferencesWindow(this);

    server = new Extensions::CompanionServer(SettingsHelper::getCompetitiveCompanionConnectionPort());

    cppServer = new Extensions::LanguageServer("C++");
    javaServer = new Extensions::LanguageServer("Java");
    pythonServer = new Extensions::LanguageServer("Python");

    findReplaceDialog = new FindReplaceDialog(this);
    findReplaceDialog->setModal(false);
    findReplaceDialog->setWindowFlags(Qt::Dialog | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                                      Qt::WindowCloseButtonHint);

    lspTimerCpp->setInterval(SettingsHelper::getLSPDelayCpp());
    lspTimerJava->setInterval(SettingsHelper::getLSPDelayJava());
    lspTimerPython->setInterval(SettingsHelper::getLSPDelayPython());

    trayIconMenu = new QMenu();
    trayIconMenu->addAction(tr("Show Main Window"), this, &AppWindow::showOnTop);
    trayIconMenu->addAction(tr("About"), this, &AppWindow::on_actionAbout_triggered);
    trayIconMenu->addAction(tr("Quit"), this, &AppWindow::on_actionQuit_triggered);
    trayIcon = new QSystemTrayIcon();
    trayIcon->setIcon(QIcon(":/icon.png"));
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->show();

    sessionManager = new Core::SessionManager(this);
}

void AppWindow::applySettings()
{
    LOG_INFO("Applying settings to Application from Settings");
    QString mode = SettingsHelper::getViewMode();

    if (mode == "code")
        on_actionEditorMode_triggered();
    else if (mode == "io")
        on_actionIOMode_triggered();
    else
        on_actionSplitMode_triggered();

    if (!SettingsHelper::getGeometry().isEmpty() && !SettingsHelper::getGeometry().isNull() &&
        SettingsHelper::getGeometry().isValid() && !SettingsHelper::isMaximizedWindow())
    {
        setGeometry(SettingsHelper::getGeometry());
    }

    if (SettingsHelper::isMaximizedWindow())
    {
        this->showMaximized();
    }

    maybeSetHotkeys();

    // FindReplaceDialog->readSettings(*SettingsHelper::settings()); FIX IT!!!
}

void AppWindow::maybeSetHotkeys()
{
    for (auto *e : hotkeyObjects)
        delete e;
    hotkeyObjects.clear();

    if (!SettingsHelper::getHotkeyRun().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyRun(), this, [this] { on_actionRun_triggered(); }));
    }
    if (!SettingsHelper::getHotkeyCompile().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyCompile(), this, [this] { on_actionCompile_triggered(); }));
    }
    if (!SettingsHelper::getHotkeyCompileRun().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyCompileRun(), this, [this] { on_actionCompileRun_triggered(); }));
    }
    if (!SettingsHelper::getHotkeyFormat().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyFormat(), this, [this] { on_actionFormatCode_triggered(); }));
    }
    if (!SettingsHelper::getHotkeyKill().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyKill(), this, [this] { on_actionKillProcesses_triggered(); }));
    }
    if (!SettingsHelper::getHotkeyChangeViewMode().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeyChangeViewMode(), this, [this] { onViewModeToggle(); }));
    }
    if (!SettingsHelper::getHotkeySnippets().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(SettingsHelper::getHotkeySnippets(), this, [this] { on_actionUseSnippets_triggered(); }));
    }
}

bool AppWindow::closeTab(int index, bool noConfirmQuit)
{
    LOG_INFO(INFO_OF(index));
    auto *tmp = windowAt(index);
    if (!tmp)
    {
        LOG_WARN("Current window is nullptr");
        return false;
    }
    if (noConfirmQuit || tmp->closeConfirm())
    {
        ui->tabWidget->removeTab(index);
        onEditorFileChanged();
        delete tmp;
        return true;
    }
    return false;
}

bool AppWindow::closeWindow(MainWindow *window, bool noConfirmQuit)
{
    return closeTab(indexOfWindow(window), noConfirmQuit);
}

void AppWindow::saveSettings()
{
    if (!this->isMaximized())
        SettingsHelper::setGeometry(this->geometry());
    SettingsHelper::setMaximizedWindow(this->isMaximized());
    // findReplaceDialog->writeSettings(*SettingsHelper::settings()); FIX IT!!!
}

void AppWindow::openTab(MainWindow *window)
{
    connect(window, &MainWindow::confirmTriggered, this, &AppWindow::onConfirmTriggered);
    connect(window, &MainWindow::editorFileChanged, this, &AppWindow::onEditorFileChanged);
    connect(window, &MainWindow::requestUpdateLanguageServerFilePath, this, &AppWindow::updateLanguageServerFilePath);
    connect(window, &MainWindow::editorLanguageChanged, this, &AppWindow::onEditorLanguageChanged);
    connect(window, &MainWindow::editorTextChanged, this, &AppWindow::onEditorTextChanged);
    connect(window, &MainWindow::editorFontChanged, this, [this] { onSettingsApplied("Appearance/Font"); });
    connect(window, &MainWindow::requestToastMessage, trayIcon,
            [this](QString const &head, QString const &body) { trayIcon->showMessage(head, body); });
    connect(window, &MainWindow::compileOrRunTriggered, this, &AppWindow::onCompileOrRunTriggered);

    setTabAt(ui->tabWidget->addTab(window, window->getTabTitle(false, true)));

    window->getEditor()->setFocus();
    onEditorFileChanged();
}

void AppWindow::openTab(const MainWindow::EditorStatus &status, bool duplicate)
{
    auto *newWindow = new MainWindow(status, duplicate, getNewUntitledIndex(), this);
    openTab(newWindow);
}

void AppWindow::openTabs(const QStringList &paths)
{
    LOG_INFO("Paths " << paths.join(" "));
    int length = paths.length();

    QProgressDialog progress(this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setWindowTitle(tr("Opening Files"));
    progress.setMaximum(length);
    progress.setValue(0);

    auto oldSize = size();
    setUpdatesEnabled(false);

    for (int i = 0; i < length; ++i)
    {
        if (progress.wasCanceled())
            break;
        progress.setValue(i);
        openTab(paths[i]);
        progress.setLabelText(currentWindow()->getTabTitle(true, false));
    }

    setUpdatesEnabled(true);
    repaint();
    resize(oldSize);

    progress.setValue(length);
}

void AppWindow::openPaths(const QStringList &paths, bool cpp, bool java, bool python, int depth)
{
    LOG_INFO("Open Path with arguments " << BOOL_INFO_OF(cpp) << BOOL_INFO_OF(java) << BOOL_INFO_OF(python)
                                         << INFO_OF(depth) << INFO_OF(paths.join(" ")));
    QStringList res;
    for (auto const &path : paths)
    {
        if (QDir(path).exists())
            res.append(openFolder(path, cpp, java, python, depth));
        else
            res.append(path);
    }
    openTabs(res);
}

QStringList AppWindow::openFolder(const QString &path, bool cpp, bool java, bool python, int depth)
{
    LOG_INFO("Open folder with arguments " << BOOL_INFO_OF(cpp) << BOOL_INFO_OF(java) << BOOL_INFO_OF(python)
                                           << INFO_OF(depth) << INFO_OF(path));
    auto entries = QDir(path).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    QStringList res;
    for (auto &entry : entries)
    {
        if (entry.isDir())
        {
            if (depth > 0)
                res.append(openFolder(entry.canonicalFilePath(), cpp, java, python, depth - 1));
            else if (depth == -1)
                res.append(openFolder(entry.canonicalFilePath(), cpp, java, python, -1));
        }
        else if ((cpp && Util::cppSuffix.contains(entry.suffix())) ||
                 (java && Util::javaSuffix.contains(entry.suffix())) ||
                 (python && Util::pythonSuffix.contains(entry.suffix())))
        {
            res.append(entry.canonicalFilePath());
        }
    }
    return res;
}

void AppWindow::openContest(Widgets::ContestDialog::ContestData const &data)
{
    const QString &path = data.path;
    const QString &lang = data.language;
    int number = data.number;

    QDir dir(path);
    QDir parent(path);
    parent.cdUp();
    if (!dir.exists() && parent.exists())
        parent.mkdir(dir.dirName());

    auto language = lang.isEmpty() ? SettingsHelper::getDefaultLanguage() : lang;

    QStringList tabs;

    for (int i = 0; i < number; ++i)
        tabs.append(QDir(path).filePath(Util::fileNameWithSuffix(QChar('A' + i), language)));

    openTabs(tabs);
}

bool AppWindow::quit()
{
    if (preferencesWindow->isVisible() && !preferencesWindow->close())
        return false;
    if (SettingsHelper::isHotExitEnable() || SettingsHelper::isForceClose())
    {
        LOG_INFO("quit() with hotexit");
        sessionManager->updateSession();
    }
    else
    {
        LOG_INFO("quit() called without hotExit");
        on_actionCloseAll_triggered();
        if (tabCount() >= 1)
        {
            LOG_INFO("Closing is cancelled");
            return false;
        }
    }
    updateChecker->closeAll();
    // The tray icon is considered as a visible window, if it is not hidden, even if the app window is closed,
    // the application won't exit.
    trayIcon->hide();
    LOG_INFO("All preparations for closing are finished");
    return true;
}

int AppWindow::getNewUntitledIndex()
{
    int index = 0;
    QSet<int> vis;
    for (int t = 0; t < tabCount(); ++t)
    {
        auto *tmp = windowAt(t);
        if (tmp->isUntitled() && tmp->getProblemURL().isEmpty())
        {
            vis.insert(tmp->getUntitledIndex());
        }
    }
    for (index = 1; vis.contains(index); ++index)
        ;
    return index;
}

/***************** ABOUT SECTION ***************************/

void AppWindow::on_actionSupportUs_triggered() // NOLINT: It can be made static
{
    auto *dialog = new SupportUsDialog(this);
    dialog->show();
}

void AppWindow::on_actionManual_triggered() // NOLINT: method can be made static
{
    QDesktopServices::openUrl(Util::websiteLink("docs"));
}

void AppWindow::on_actionReportIssues_triggered() // NOLINT: method can be made static
{
    QDesktopServices::openUrl(QUrl("https://github.com/cpeditor/cpeditor/issues"));
}

void AppWindow::on_actionAbout_triggered()
{
    QMessageBox::about(
        this, tr("About CP Editor %1").arg(APP_VERSION),
        tr("<p><b>CP Editor</b> is a native Qt-based code editor. It's specially designed for competitive "
           "programming, unlike other editors/IDEs which are mainly for developers. It helps you focus on "
           "your algorithm and automates the compilation, executing and testing. It even fetches test "
           "cases for you from different platforms and submits solutions to Codeforces!</p>"
           "<p>Copyright (C) 2019-2021 Ashar Khan &lt;ashar786khan@gmail.com&gt;</p>"
           "<p>This is free software; see the source for copying conditions. There is NO warranty; not "
           "even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The source code for CP Editor is "
           "available at <a href=\"https://github.com/cpeditor/cpeditor\"> "
           "https://github.com/cpeditor/cpeditor</a>.</p>"));
}

void AppWindow::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt(this);
}

void AppWindow::on_actionBuildInfo_triggered()
{
    QMessageBox::about(this, tr("Build Info"),
                       tr("App version: %1\nBuild type: %2\nGit commit hash: %3\nBuild time: %4\nOS: %5")
                           .arg(APP_VERSION)
#ifdef PORTABLE_VERSION
                           .arg(tr("Portable Version"))
#else
                           .arg(tr("Setup Version"))
#endif
                           .arg(GIT_COMMIT_HASH)
                           .arg(__DATE__ " " __TIME__)
#if defined(Q_OS_LINUX)
                           .arg("Linux")
#elif defined(Q_OS_WIN)
                           .arg("Windows")
#elif defined(Q_OS_MACOS)
                           .arg("macOS")
#elif defined(Q_OS_FREEBSD)
                           .arg("FreeBSD")
#elif defined(Q_OS_UNIX)
                           .arg("UNIX")
#else
                           .arg("Unknown")
#endif
    );
}

/******************* FILES SECTION *************************/

void AppWindow::on_actionQuit_triggered()
{
    if (quit())
    {
        LOG_INFO("Exiting application");
        QApplication::exit();
    }
}

void AppWindow::on_actionNewTab_triggered()
{
    openTab("");
}

void AppWindow::on_actionOpen_triggered()
{
    auto fileNames = DefaultPathManager::getOpenFileNames("Open File", this, tr("Open Files"),
                                                          Util::fileNameFilter(true, true, true));
    LOG_INFO("Filename " << fileNames.join(", "));
    openTabs(fileNames);
}

void AppWindow::on_actionOpenContest_triggered()
{
    Widgets::ContestDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        openContest(dialog.contestData());
}

void AppWindow::on_actionSave_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->save(true, tr("Save"));
}

void AppWindow::on_actionSaveAs_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->saveAs();
}

void AppWindow::on_actionSaveAll_triggered()
{
    for (int t = 0; t < tabCount(); ++t)
    {
        auto *tmp = windowAt(t);
        if (!tmp->save(true, tr("Save All")))
            break;
    }
}

void AppWindow::on_actionCloseCurrent_triggered()
{
    int index = ui->tabWidget->currentIndex();
    if (index != -1)
        closeTab(index);
}

void AppWindow::on_actionCloseAll_triggered()
{
    for (int t = 0; t < tabCount(); t++)
    {
        if (closeTab(t))
            --t;
        else
            break;
    }
}

void AppWindow::on_actionCloseSaved_triggered()
{
    for (int t = 0; t < ui->tabWidget->count(); t++)
        if (!windowAt(t)->isTextChanged() && closeTab(t))
            --t;
}

/************************ PREFERENCES SECTION **********************/

void AppWindow::on_actionResetSettings_triggered()
{
    auto res = QMessageBox::question(this, tr("Reset preferences"),
                                     tr("Are you sure you want to reset the"
                                        " all preferences to default?"),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
    {
        SettingsManager::reset();
        onSettingsApplied("");
        LOG_INFO("Reset success");
    }
}

void AppWindow::on_actionExportSettings_triggered()
{
    auto path = DefaultPathManager::getSaveFileName("Export And Import Settings", this, tr("Export settings to a file"),
                                                    tr("CP Editor Settings File") + " (*.cpeditor)");
    if (!path.isEmpty())
        SettingsManager::saveSettings(path);
}

void AppWindow::on_actionImportSettings_triggered()
{
    auto res = QMessageBox::question(
        this, tr("Import Settings"),
        tr("All current settings will lose after importing settings from a file. Are you sure to continue?"),
        QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::No)
        return;

    auto path =
        DefaultPathManager::getOpenFileName("Export And Import Settings", this, tr("Import settings from a file"),
                                            tr("CP Editor Settings File") + " (*.cpeditor)");
    if (!path.isEmpty())
    {
        SettingsManager::loadSettings(path);
        onSettingsApplied("");
    }
}

void AppWindow::on_actionExportSession_triggered()
{
    auto path =
        DefaultPathManager::getSaveFileName("Export And Load Session", this, tr("Export current session to a file"),
                                            tr("CP Editor Session File") + " (*.json)");
    if (!path.isEmpty())
    {
        if (!Util::saveFile(path, sessionManager->currentSessionText(), "Export Session"))
        {
            QMessageBox::warning(this, tr("Export Session"),
                                 tr("Failed to export the current session to [%1]").arg(path));
        }
    }
}

void AppWindow::on_actionLoadSession_triggered()
{
    auto res = QMessageBox::question(this, tr("Load Session"),
                                     tr("Loading a session from a file will close all tabs in the current session "
                                        "without saving the files. Are you sure to continue?"),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::No)
        return;

    auto path = DefaultPathManager::getOpenFileName("Export And Load Session", this, tr("Load session from a file"),
                                                    tr("CP Editor Session File") + " (*.json)");
    if (!path.isEmpty())
        sessionManager->restoreSession(path);
}

void AppWindow::on_actionSettings_triggered()
{
    LOG_INFO("Launching settings window");
    preferencesWindow->display();
}

/************************** SLOTS *********************************/

#define FROMJSON(x) auto x = json[#x] // NOLINT: macro argument should be enclosed in parentheses

void AppWindow::onReceivedMessage(quint32 instanceId, QByteArray message)
{
    showOnTop();

    message = message.mid(message.indexOf("NOLOSTDATA") + 10);
    auto json = QJsonDocument::fromJson(message);
    FROMJSON(cpp).toBool();
    FROMJSON(java).toBool();
    FROMJSON(python).toBool();

    LOG_INFO("Received a request from secondary instances" << json["type"].toString());
    if (json["type"] == "normal")
    {
        FROMJSON(depth).toInt();
        FROMJSON(paths).toVariant().toStringList();
        openPaths(paths, cpp, java, python, depth);
    }
    else if (json["type"] == "contest")
    {
        FROMJSON(number).toInt();
        FROMJSON(path).toString();
        QString lang = SettingsHelper::getDefaultLanguage();
        if (cpp)
            lang = "C++";
        else if (java)
            lang = "Java";
        else if (python)
            lang = "Python";
        openContest({path, number, lang});
    }
}

#undef FROMJSON

bool AppWindow::forceClose()
{
    SettingsHelper::setForceClose(true);
    return close();
}

void AppWindow::onTabChanged(int index)
{
    LOG_INFO(INFO_OF(index));
    if (index == -1)
    {
        activeLogger = nullptr;
        server->setMessageLogger(nullptr);
        findReplaceDialog->setTextEdit(nullptr);
        setWindowTitle(tr("CP Editor: An editor specially designed for competitive programming"));

        if (cppServer->isDocumentOpen())
            cppServer->closeDocument();
        if (pythonServer->isDocumentOpen())
            pythonServer->closeDocument();
        if (javaServer->isDocumentOpen())
            javaServer->closeDocument();

        return;
    }

    disconnect(activeSplitterMoveConnection);
    disconnect(activeRightSplitterMoveConnection);

    auto *tmp = windowAt(index);

    reAttachLanguageServer(tmp);

    findReplaceDialog->setTextEdit(tmp->getEditor());

    setWindowTitle(tmp->getCompleteTitle() + " - CP Editor");

    activeLogger = tmp->getLogger();
    server->setMessageLogger(activeLogger);

    if (ui->actionEditorMode->isChecked())
        on_actionEditorMode_triggered();
    else if (ui->actionIOMode->isChecked())
        on_actionIOMode_triggered();
    else if (ui->actionSplitMode->isChecked())
        on_actionSplitMode_triggered();

    tmp->getRightSplitter()->restoreState(SettingsHelper::getRightSplitterSize());

    activeSplitterMoveConnection =
        connect(tmp->getSplitter(), &QSplitter::splitterMoved, this, &AppWindow::onSplitterMoved);
    activeRightSplitterMoveConnection =
        connect(tmp->getRightSplitter(), &QSplitter::splitterMoved, this, &AppWindow::onRightSplitterMoved);
    tmp->getEditor()->setFocus();
}

void AppWindow::onEditorFileChanged()
{
    if (currentWindow() != nullptr)
    {
        QMap<QString, QVector<int>> tabsByName;

        for (int t = 0; t < tabCount(); ++t)
        {
            tabsByName[windowAt(t)->getTabTitle(false, false)].push_back(t);
        }

        for (auto tabs : tabsByName)
        {
            QString longestCommonPrefix;
            if (tabs.size() > 1)
            {
                longestCommonPrefix = windowAt(tabs.front())->getCompleteTitle();
                for (int t = 1; t < tabs.length(); ++t)
                {
                    auto current = windowAt(tabs[t])->getCompleteTitle();
                    longestCommonPrefix = longestCommonPrefix.left(current.length());
                    for (int i = 0; i < longestCommonPrefix.length(); ++i)
                    {
                        if (longestCommonPrefix[i] != current[i])
                        {
                            longestCommonPrefix = longestCommonPrefix.left(i);
                            break;
                        }
                    }
                }
            }
            int removeLength = longestCommonPrefix.lastIndexOf('/') + 1;
            for (auto index : tabs)
            {
                ui->tabWidget->setTabText(index, windowAt(index)->getTabTitle(tabs.size() > 1, true, removeLength));
            }
        }

        setWindowTitle(currentWindow()->getCompleteTitle() + " - CP Editor");
    }
}

void AppWindow::onEditorTextChanged(MainWindow *window)
{
    int index = indexOfWindow(window);
    if (index != -1)
    {
        auto title = ui->tabWidget->tabText(index);
        // assume the clean title doesn't end with " *"
        if (title.endsWith(" *"))
            title.chop(2);
        if (windowAt(index)->isTextChanged())
            title += " *";
        ui->tabWidget->setTabText(index, title);

        if (window->getLanguage() == "C++")
            lspTimerCpp->start();
        else if (window->getLanguage() == "Java")
            lspTimerJava->start();
        else
            lspTimerPython->start();
    }
}

void AppWindow::updateLanguageServerFilePath(MainWindow *window, const QString &path)
{
    if (currentWindow() == window)
    {
        if (window->getLanguage() == "C++" && cppServer->isDocumentOpen())
            cppServer->updatePath(path);
        else if (window->getLanguage() == "Java" && javaServer->isDocumentOpen())
            javaServer->updatePath(path);
        else if (window->getLanguage() == "Python" && pythonServer->isDocumentOpen())
            pythonServer->updatePath(path);
    }
}

void AppWindow::onEditorLanguageChanged(MainWindow *window)
{
    if (currentWindow() == window)
        reAttachLanguageServer(window);
}

void AppWindow::onLSPTimerElapsedCpp()
{
    auto *tab = currentWindow();
    if (tab == nullptr)
        return;

    if (SettingsHelper::isLSPUseLintingCpp() && tab->getLanguage() == "C++")
        cppServer->requestLinting();

    lspTimerCpp->stop();
}

void AppWindow::onLSPTimerElapsedJava()
{
    auto *tab = currentWindow();
    if (tab == nullptr)
        return;

    if (SettingsHelper::isLSPUseLintingJava() && tab->getLanguage() == "Java")
        javaServer->requestLinting();

    lspTimerJava->stop();
}

void AppWindow::onLSPTimerElapsedPython()
{
    auto *tab = currentWindow();
    if (tab == nullptr)
        return;

    if (SettingsHelper::isLSPUseLintingPython() && tab->getLanguage() == "Python")
        pythonServer->requestLinting();

    lspTimerPython->stop();
}

void AppWindow::onSettingsApplied(const QString &pagePath)
{
    LOG_INFO("Apply settings for " << INFO_OF(pagePath));

    for (int i = 0; i < tabCount(); ++i)
    {
        windowAt(i)->applySettings(pagePath);
        onEditorTextChanged(windowAt(i));
    }

    auto pageChanged = [this, pagePath](const QString &page) {
        if (!preferencesWindow->pathExists(page))
            LOG_DEV("Unknown path: " << page);
        return pagePath.isEmpty() || pagePath == page;
    };

    if (pageChanged("Key Bindings"))
        maybeSetHotkeys();

    if (pageChanged("Extensions/Competitive Companion"))
    {
        if (SettingsHelper::isCompetitiveCompanionEnable())
            server->updatePort(SettingsHelper::getCompetitiveCompanionConnectionPort());
        else
            server->updatePort(0);
    }

    if (pageChanged("Appearance/General"))
    {
        setWindowOpacity(SettingsHelper::getOpacity() / 100.0);
        Core::StyleManager::setStyle(SettingsHelper::getUIStyle());
    }

    if (pageChanged("Appearance/Font"))
    {
        if (SettingsHelper::isUseCustomApplicationFont())
            qApp->setFont(SettingsHelper::getCustomApplicationFont());
        else
            qApp->setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    }

    if (pageChanged("Extensions/Language Server/C++ Server"))
    {
        cppServer->updateSettings();
        lspTimerCpp->setInterval(SettingsHelper::getLSPDelayCpp());
    }

    if (pageChanged("Extensions/Language Server/Java Server"))
    {
        javaServer->updateSettings();
        lspTimerJava->setInterval(SettingsHelper::getLSPDelayJava());
    }

    if (pageChanged("Extensions/Language Server/Python Server"))
    {
        pythonServer->updateSettings();
        lspTimerPython->setInterval(SettingsHelper::getLSPDelayPython());
    }

    if (pageChanged("Actions/Save Session"))
    {
        sessionManager->setAutoUpdateDuration(SettingsHelper::getHotExitAutoSaveInterval());
        sessionManager->setAutoUpdateSession(SettingsHelper::isHotExitEnable() && SettingsHelper::isHotExitAutoSave());
    }

    if (pageChanged("File Path/Default Paths"))
    {
        DefaultPathManager::fromVariantList(SettingsHelper::getDefaultPathNamesAndPaths());
    }

    SettingsManager::saveSettings(QString());
}

void AppWindow::onIncomingCompanionRequest(const Extensions::CompanionData &data)
{
    LOG_INFO("Request from competitive companion arrived");

    for (int i = 0; i < tabCount(); ++i)
    {
        if (windowAt(i)->getProblemURL() == data.url)
        {
            setTabAt(i);
            currentWindow()->applyCompanion(data);
            return;
        }
    }

    do
    {
        if (SettingsHelper::isOpenOldFileForOldProblemUrl() && FileProblemBinder::containsProblem(data.url))
        {
            auto oldFile = FileProblemBinder::getFileForProblem(data.url);
            if (QFileInfo(oldFile).isReadable())
            {
                openTab(oldFile);
                break;
            }
        }
        if (SettingsHelper::isCompetitiveCompanionOpenNewTab() || currentWindow() == nullptr)
            openTab("");
    } while (false);

    currentWindow()->applyCompanion(data);
}

void AppWindow::onViewModeToggle()
{
    LOG_INFO("Switching view mode");
    if (ui->actionEditorMode->isChecked())
    {
        on_actionIOMode_triggered();
        return;
    }
    if (ui->actionSplitMode->isChecked())
    {
        on_actionEditorMode_triggered();
        return;
    }
    if (ui->actionIOMode->isChecked())
    {
        on_actionSplitMode_triggered();
        return;
    }
}

void AppWindow::onSplitterMoved()
{
    auto *splitter = currentWindow()->getSplitter();
    SettingsHelper::setSplitterSize(splitter->saveState());
}

void AppWindow::onRightSplitterMoved()
{
    auto *splitter = currentWindow()->getRightSplitter();
    SettingsHelper::setRightSplitterSize(splitter->saveState());
}

void AppWindow::openTab(const QString &path, const QString &lang)
{
    LOG_INFO("OpenTab Path is " << path);
    if (!path.isEmpty())
    {
        auto fileInfo = QFileInfo(path);
        for (int t = 0; t < tabCount(); t++)
        {
            auto tPath = qobject_cast<MainWindow *>(ui->tabWidget->widget(t))->getFilePath();
            if (path == tPath || (fileInfo.exists() && fileInfo == QFileInfo(tPath)))
            {
                setTabAt(t);
                return;
            }
        }
    }

    auto *newWindow = new MainWindow(path, getNewUntitledIndex(), this);

    if (!lang.isEmpty())
    {
        newWindow->setLanguage(lang);
    }
    else
    {
        QString langFromFile = SettingsHelper::getDefaultLanguage();

        const auto suffix = QFileInfo(path).suffix();

        if (Util::cppSuffix.contains(suffix))
            langFromFile = "C++";
        else if (Util::javaSuffix.contains(suffix))
            langFromFile = "Java";
        else if (Util::pythonSuffix.contains(suffix))
            langFromFile = "Python";

        newWindow->setLanguage(lang);
    }

    openTab(newWindow);
}

/************************* ACTIONS ************************/
void AppWindow::on_actionCheckForUpdates_triggered()
{
    updateChecker->checkUpdate(false);
}

void AppWindow::on_actionCompile_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->compileOnly();
}

void AppWindow::on_actionCompileRun_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->compileAndRun();
}

void AppWindow::on_actionRun_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->runOnly();
}

void AppWindow::on_actionFindReplace_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        findReplaceDialog->showDialog(tmp->getEditor()->textCursor().selectedText());
}

void AppWindow::on_actionFormatCode_triggered()
{
    if (currentWindow() != nullptr)
    {
        currentWindow()->formatSource(true, true);
    }
}

void AppWindow::on_actionRunDetached_triggered()
{
    if (currentWindow() != nullptr)
    {
        currentWindow()->detachedExecution();
    }
}

void AppWindow::on_actionKillProcesses_triggered()
{
    if (currentWindow() != nullptr)
    {
        currentWindow()->killProcesses();
    }
}

void AppWindow::on_actionUseSnippets_triggered()
{
    LOG_INFO("Use snippets trigerred");
    auto *current = currentWindow();
    if (current != nullptr)
    {
        QString lang = current->getLanguage();
        QStringList names = SettingsHelper::getLanguageConfig(lang).getSnippets();
        if (names.isEmpty())
        {
            activeLogger->warn(
                tr("Snippets"),
                tr("There are no snippets for %1. Please add snippets in the preference window.").arg(lang));
        }
        else
        {
            auto *ok = new bool;
            auto name = QInputDialog::getItem(this, tr("Use Snippets"), tr("Choose a snippet:"), names, 0, true, ok);
            if (*ok)
            {
                LOG_INFO("Looking for snippet : " << name);
                if (names.contains(name))
                {
                    LOG_INFO("Found snippet and inserted");
                    QString content = SettingsHelper::getLanguageConfig(lang).getSnippet(name);
                    current->insertText(content);
                }
                else
                {
                    activeLogger->warn(tr("Snippets"), tr("There is no snippet named %1 for %2").arg(name, lang));
                }
            }
            delete ok;
        }
    }
}

void AppWindow::on_actionEditorMode_triggered()
{
    SettingsHelper::setViewMode("code");
    ui->actionEditorMode->setChecked(true);
    ui->actionIOMode->setChecked(false);
    ui->actionSplitMode->setChecked(false);
    if (currentWindow() != nullptr)
    {
        LOG_INFO("Switched to code mode");
        currentWindow()->setViewMode("code");
    }
}

void AppWindow::on_actionIOMode_triggered()
{
    SettingsHelper::setViewMode("io");
    ui->actionEditorMode->setChecked(false);
    ui->actionIOMode->setChecked(true);
    ui->actionSplitMode->setChecked(false);
    if (currentWindow() != nullptr)
    {
        LOG_INFO("Switched to IO Mode");
        currentWindow()->setViewMode("io");
    }
}

void AppWindow::on_actionSplitMode_triggered()
{
    SettingsHelper::setViewMode("split");
    ui->actionEditorMode->setChecked(false);
    ui->actionIOMode->setChecked(false);
    ui->actionSplitMode->setChecked(true);
    LOG_INFO("Entered split mode");
    if (currentWindow() != nullptr)
    {
        LOG_INFO("Restored splitter state");
        currentWindow()->setViewMode("split");
    }
}

void AppWindow::on_actionFullScreen_toggled(bool checked)
{
    LOG_INFO(INFO_OF(checked));
    auto state = windowState();
    state.setFlag(Qt::WindowFullScreen, checked);
    setWindowState(state);
    if (checked && !SettingsHelper::isFullScreenDialogShown())
    {
        SettingsHelper::setFullScreenDialogShown(true);
        QMessageBox::information(this, tr("How to exit full-screen"), tr("Press F11 key to exit full-screen mode."));
    }
}

void AppWindow::on_actionIndent_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->indent();
}

void AppWindow::on_actionUnindent_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->unindent();
}

void AppWindow::on_actionSwapLineUp_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->swapLineUp();
}

void AppWindow::on_actionSwapLineDown_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->swapLineDown();
}

void AppWindow::on_actionDuplicateLine_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->duplicate();
}

void AppWindow::on_actionDeleteLine_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->deleteLine();
}

void AppWindow::on_actionToggleComment_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->toggleComment();
}

void AppWindow::on_actionToggleBlockComment_triggered()
{
    auto *tmp = currentWindow();
    if (tmp != nullptr)
        tmp->getEditor()->toggleBlockComment();
}

void AppWindow::onConfirmTriggered(MainWindow *widget)
{
    int index = indexOfWindow(widget);
    if (index != -1)
        setTabAt(index);
}

void AppWindow::onTabContextMenuRequested(const QPoint &pos)
{
    int index = ui->tabWidget->tabBar()->tabAt(pos);
    if (index != -1)
    {
        LOG_INFO(INFO_OF(index));

        auto *window = windowAt(index);

        delete tabMenu;
        tabMenu = new QMenu();

        tabMenu->addAction(tr("Close"), [index, this] { closeTab(index); });

        tabMenu->addAction(tr("Close Others"), [window, this] {
            for (int i = 0; i < tabCount(); ++i)
                if (windowAt(i) != window && closeTab(i))
                    --i;
        });

        tabMenu->addAction(tr("Close to the Left"), [window, this] {
            for (int i = 0; i < tabCount() && windowAt(i) != window; ++i)
                if (closeTab(i))
                    --i;
        });

        tabMenu->addAction(tr("Close to the Right"), [index, this] {
            for (int i = index + 1; i < tabCount(); ++i)
                if (closeTab(i))
                    --i;
        });
        tabMenu->addAction(tr("Close Saved"), [this] { on_actionCloseSaved_triggered(); });

        tabMenu->addAction(tr("Close All"), [this] { on_actionCloseAll_triggered(); });
        QString filePath = window->getFilePath();

        tabMenu->addSeparator();

        tabMenu->addAction(tr("Duplicate Tab"), [window, this] { openTab(window->toStatus(), true); });

        tabMenu->addSeparator();

        if (window->getLanguage() != "Python")
            tabMenu->addAction(tr("Set Compile Command"), [window] { window->updateCompileCommand(); });

        tabMenu->addAction(tr("Set Time Limit"), [window] { window->updateTimeLimit(); });

        LOG_INFO(INFO_OF(filePath));

        const auto outputFilePath =
            Core::Compiler::outputFilePath(window->tmpPath(), window->getFilePath(), window->getLanguage(), false);

        const auto revealSourceFile = Util::revealInFileManager(filePath, tr("Source File"));
        const auto revealExecutableFile = Util::revealInFileManager(outputFilePath, tr("Executable File"));

        if (!revealSourceFile.second.isEmpty() || !revealExecutableFile.second.isEmpty())
            tabMenu->addSeparator();

        if (!revealSourceFile.second.isEmpty())
        {
            tabMenu->addAction(tr("Copy File Path"), [filePath] { QGuiApplication::clipboard()->setText(filePath); });
            tabMenu->addAction(revealSourceFile.second, revealSourceFile.first);
        }

        if (!revealExecutableFile.second.isEmpty())
        {
            tabMenu->addAction(revealExecutableFile.second, revealExecutableFile.first);
        }

        tabMenu->addSeparator();
        if (!window->getProblemURL().isEmpty())
        {
            tabMenu->addAction(tr("Open Problem in Browser"),
                               [window] { QDesktopServices::openUrl(window->getProblemURL()); });
            tabMenu->addAction(tr("Copy Problem URL"),
                               [window] { QGuiApplication::clipboard()->setText(window->getProblemURL()); });
        }
        tabMenu->addAction(tr("Set Codeforces URL"), [window, this] {
            QString contestId;
            QString problemCode;
            Extensions::CFTool::parseCfUrl(window->getProblemURL(), contestId, problemCode);
            bool ok = false;
            contestId = QInputDialog::getText(this, tr("Set CF URL"), tr("Enter the contest ID:"), QLineEdit::Normal,
                                              contestId, &ok);
            if (ok)
                problemCode = QInputDialog::getText(this, tr("Set CF URL"), tr("Enter the problem Code (A-Z):"),
                                                    QLineEdit::Normal, problemCode, &ok);
            if (ok)
            {
                auto url = "https://codeforces.com/contest/" + contestId + "/problem/" + problemCode;
                window->setProblemURL(url);
            }
        });
        tabMenu->addAction(tr("Set Problem URL"), [window, this] {
            bool ok = false;
            auto url = QInputDialog::getText(this, tr("Set Problem URL"), tr("Enter the new problem URL:"),
                                             QLineEdit::Normal, window->getProblemURL(), &ok);
            if (ok)
            {
                if (url.isEmpty() && window->isUntitled())
                    window->setUntitledIndex(getNewUntitledIndex());
                window->setProblemURL(url);
            }
        });
        tabMenu->popup(ui->tabWidget->tabBar()->mapToGlobal(pos));
    }
}

void AppWindow::setTabAt(int index)
{
    ui->tabWidget->setCurrentIndex(index);
}

int AppWindow::indexOfWindow(MainWindow *window)
{
    return ui->tabWidget->indexOf(window);
}

int AppWindow::tabCount() const
{
    return ui->tabWidget->count();
}

MainWindow *AppWindow::currentWindow()
{
    int current = ui->tabWidget->currentIndex();
    if (current == -1)
    {
        return nullptr;
    }
    return qobject_cast<MainWindow *>(ui->tabWidget->widget(current));
}

void AppWindow::reAttachLanguageServer(MainWindow *window)
{
    window->getEditor()->clearSquiggle();
    lspTimerCpp->stop();
    lspTimerJava->stop();
    lspTimerPython->stop();

    if (cppServer->isDocumentOpen())
        cppServer->closeDocument();
    if (javaServer->isDocumentOpen())
        javaServer->closeDocument();
    if (pythonServer->isDocumentOpen())
        pythonServer->closeDocument();

    if (window->getLanguage() == "C++")
    {
        cppServer->openDocument(window->filePathOrTmpPath(), window->getEditor(), window->getLogger());
        cppServer->requestLinting();
        lspTimerCpp->start();
    }
    else if (window->getLanguage() == "Java")
    {
        javaServer->openDocument(window->filePathOrTmpPath(), window->getEditor(), window->getLogger());
        javaServer->requestLinting();
        lspTimerJava->start();
    }
    else if (window->getLanguage() == "Python")
    {
        pythonServer->openDocument(window->filePathOrTmpPath(), window->getEditor(), window->getLogger());
        pythonServer->requestLinting();
        lspTimerPython->start();
    }
}

MainWindow *AppWindow::windowAt(int index)
{
    if (index == -1)
    {
        return nullptr;
    }
    return qobject_cast<MainWindow *>(ui->tabWidget->widget(index));
}

void AppWindow::on_actionShowLogs_triggered() // NOLINT: Method can be made static
{
    Core::Log::revealInFileManager();
}

void AppWindow::on_actionClearLogs_triggered()
{
    Core::Log::clearOldLogs();
    if (currentWindow() != nullptr)
    {
        currentWindow()->getLogger()->info(tr("EventLogger"),
                                           tr("All logs except for current session has been deleted"));
    }
}

void AppWindow::showOnTop()
{
    Util::showWidgetOnTop(this);
}

void AppWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
    {
        if (!isMinimized())
            showMinimized();
        else
            showOnTop();
    }
}

void AppWindow::onCompileOrRunTriggered()
{
    if (ui->actionEditorMode->isChecked())
        on_actionSplitMode_triggered();
}
