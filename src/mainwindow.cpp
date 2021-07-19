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

#include "mainwindow.hpp"

#include "Core/Checker.hpp"
#include "Core/Compiler.hpp"
#include "Core/EventLogger.hpp"
#include "Core/FakeVimProxy.hpp"
#include "Core/MessageLogger.hpp"
#include "Core/Runner.hpp"
#include "Editor/CodeEditor.hpp"
#include "Extensions/CFTool.hpp"
#include "Extensions/ClangFormatter.hpp"
#include "Extensions/CompanionServer.hpp"
#include "Extensions/YAPFormatter.hpp"
#include "Settings/DefaultPathManager.hpp"
#include "Settings/FileProblemBinder.hpp"
#include "Settings/PreferencesWindow.hpp"
#include "Util/FileUtil.hpp"
#include "Widgets/TestCases.hpp"
#include "appwindow.hpp"
#include "generated/SettingsHelper.hpp"
#include "generated/version.hpp"
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTimer>

#include "../ui/ui_mainwindow.h"

static const int MAX_NUMBER_OF_RECENT_FILES = 20;

// ***************************** RAII  ****************************

MainWindow::MainWindow(int index, AppWindow *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), editor(nullptr), appWindow(parent), untitledIndex(index),
      fileWatcher(new QFileSystemWatcher(this)), reloading(false), killingProcesses(false),
      autoSaveTimer(new QTimer(this))
{
    LOG_INFO(INFO_OF(index));

    ui->setupUi(this);

    log = new MessageLogger(appWindow->getPreferencesWindow(), this);
    ui->messageLoggerLayout->addWidget(log);

    testcases = new Widgets::TestCases(log, this);
    ui->testCasesLayout->addWidget(testcases);
    connect(testcases, &Widgets::TestCases::checkerChanged, this, &MainWindow::updateChecker);
    connect(testcases, &Widgets::TestCases::requestRun, this, &MainWindow::runTestCase);

    setEditor();
    connect(fileWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileWatcherChanged);
    connect(
        autoSaveTimer, &QTimer::timeout, autoSaveTimer, [this] { saveFile(AutoSave, tr("Auto Save"), false); },
        Qt::DirectConnection);
    applySettings("");
    QTimer::singleShot(0, [this] { setLanguage(language); }); // See issue #187 for more information
}

MainWindow::MainWindow(const QString &fileOpen, int index, AppWindow *parent) : MainWindow(index, parent)
{
    LOG_INFO(INFO_OF(fileOpen));
    loadFile(fileOpen);
    if (testcases->count() == 0)
        testcases->addTestCase();
}

MainWindow::MainWindow(const EditorStatus &status, bool duplicate, int index, AppWindow *parent)
    : MainWindow(index, parent)
{
    LOG_INFO(INFO_OF(duplicate));
    loadStatus(status, duplicate);
}

MainWindow::~MainWindow()
{
    killProcesses();

    delete cftool;
    delete tmpDir;

    delete ui;
    delete autoSaveTimer;
    delete testcases;
    delete fileWatcher;
    delete editor;
    delete log;

    if (fakevimHandler)
    {
        fakevimHandler->disconnectFromEditor();
        fakevimHandler->deleteLater();
        fakevimHandler = nullptr;
    }
}

void MainWindow::setEditor()
{
    editor = new Editor::CodeEditor();
    editor->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
    editor->setAcceptDrops(false);

    ui->editorArea->addWidget(editor);

    connect(editor, &Editor::CodeEditor::textChanged, this, &MainWindow::onTextChanged);
    connect(editor, &Editor::CodeEditor::fontChanged, this, &MainWindow::onEditorFontChanged);
    // cursorPositionChanged() does not imply selectionChanged() if you press Left with
    // a selection (and the cursor is at the begin of the selection)
    connect(editor, &Editor::CodeEditor::cursorPositionChanged, this, &MainWindow::updateCursorInfo);
    connect(editor, &Editor::CodeEditor::selectionChanged, this, &MainWindow::updateCursorInfo);
}

void MainWindow::compile()
{
    if (SettingsHelper::isSaveFileOnCompilation())
        saveFile(IgnoreUntitled, tr("Compiler"), true);

    killProcesses();

    compiler = new Core::Compiler();

    auto path = tmpPath();
    if (path.isEmpty())
        return;

    if (language == "Python")
    {
        onCompilationFinished("");
        return;
    }

    if (language != "C++" && language != "Java")
    {
        log->warn(tr("Compiler"), tr("Please set the language"));
        return;
    }

    connect(compiler, &Core::Compiler::compilationStarted, this, &MainWindow::onCompilationStarted);
    connect(compiler, &Core::Compiler::compilationFinished, this, &MainWindow::onCompilationFinished);
    connect(compiler, &Core::Compiler::compilationErrorOccurred, this, &MainWindow::onCompilationErrorOccurred);
    connect(compiler, &Core::Compiler::compilationFailed, this, &MainWindow::onCompilationFailed);
    connect(compiler, &Core::Compiler::compilationKilled, this, &MainWindow::onCompilationKilled);
    compiler->start(path, filePath, compileCommand(), language);
}

void MainWindow::run()
{
    if (SettingsHelper::isSaveFileOnExecution())
        saveFile(IgnoreUntitled, tr("Runner"), true);

    LOG_INFO("Requesting run of testcases");
    killProcesses();
    testcases->clearOutput();

    if (!QStringList({"C++", "Java", "Python"}).contains(language))
    {
        log->warn(tr("Runner"), tr("Wrong language, please set the language"));
        return;
    }

    checker->clearTasks();

    for (int i = 0; i < testcases->count(); ++i)
    {
        if ((!testcases->input(i).trimmed().isEmpty() || SettingsHelper::isRunOnEmptyTestcase()) &&
            testcases->isChecked(i))
        {
            run(i);
        }
    }

    if (runner.empty())
        log->warn(tr("Runner"), tr("All inputs are empty, nothing to run"));
}

void MainWindow::run(int index)
{
    if (index < 0 || index >= testcases->count())
    {
        LOG_DEV(INFO_OF(index) << INFO_OF(testcases->count()));
        return;
    }

    auto *tmp = new Core::Runner(index);
    connect(tmp, &Core::Runner::runStarted, this, &MainWindow::onRunStarted);
    connect(tmp, &Core::Runner::runFinished, this, &MainWindow::onRunFinished);
    connect(tmp, &Core::Runner::failedToStartRun, this, &MainWindow::onFailedToStartRun);
    connect(tmp, &Core::Runner::runOutputLimitExceeded, this, &MainWindow::onRunOutputLimitExceeded);
    connect(tmp, &Core::Runner::runKilled, this, &MainWindow::onRunKilled);
    tmp->run(tmpPath(), filePath, language, SettingsManager::get(QString("%1/Run Command").arg(language)).toString(),
             SettingsManager::get(QString("%1/Run Arguments").arg(language)).toString(), testcases->input(index),
             timeLimit());
    runner.push_back(tmp);
}

void MainWindow::runTestCase(int index)
{
    LOG_INFO(INFO_OF(index));
    killProcesses();
    testcases->clearOutput();
    log->clear();

    if (!QStringList({"C++", "Java", "Python"}).contains(language))
    {
        log->warn(tr("Runner"), tr("Wrong language, please set the language"));
        return;
    }

    run(index);
}

void MainWindow::loadTests()
{
    if (!isUntitled() && SettingsHelper::isSaveTests())
        testcases->loadFromSavedFiles(filePath);
}

void MainWindow::saveTests(bool safe)
{
    if (!isUntitled() && SettingsHelper::isSaveTests())
        testcases->saveToFiles(filePath, safe);
}

void MainWindow::setCFToolUI()
{
    if (submitToCodeforces == nullptr)
    {
        submitToCodeforces = new QPushButton(tr("Submit"), this);
        cftool = new Extensions::CFTool(cftoolPath, log);
        connect(cftool, &Extensions::CFTool::requestToastMessage, this, &MainWindow::requestToastMessage);
        ui->compileAndRunButtons->addWidget(submitToCodeforces);
        connect(submitToCodeforces, &QPushButton::clicked, this, [this] {
            emit confirmTriggered(this);
            auto response = QMessageBox::warning(
                this, tr("Sure to submit"),
                tr("Are you sure you want to submit this solution to Codeforces?\n\n URL: %1\n Language: %2")
                    .arg(problemURL, language),
                QMessageBox::Yes | QMessageBox::No);

            if (response == QMessageBox::Yes)
            {
                auto path = tmpPath();
                if (path.isEmpty())
                {
                    QMessageBox::warning(this, tr("CF Tool"),
                                         tr("Failed to save the temp file, and the solution is not submitted."));
                }
                else
                {
                    log->clear();
                    cftool->submit(tmpPath(), problemURL);
                }
            }
        });
    }
    if (!Extensions::CFTool::check(cftoolPath))
    {
        submitToCodeforces->setEnabled(false);
        log->error(tr("CF Tool"),
                   tr("You need to install CF Tool to submit your code to Codeforces. If already installed, you can "
                      "add it in the PATH environment variable or check your settings at %1.")
                       .arg(SettingsHelper::pathOfCFPath()),
                   false);
    }
}

int MainWindow::getUntitledIndex() const
{
    return untitledIndex;
}

QString MainWindow::getFileName() const
{
    if (!isUntitled())
        return QFileInfo(filePath).fileName();
    if (!problemURL.isEmpty())
        return QRegularExpression(R"(.*/([^\?#].*?)/?$)").match(problemURL).captured(1);
    return tr("Untitled-%1").arg(untitledIndex);
}

QString MainWindow::getFilePath() const
{
    return filePath;
}

QString MainWindow::getProblemURL() const
{
    return problemURL;
}

QString MainWindow::getCompleteTitle() const
{
    if (!isUntitled())
        return filePath;
    if (!problemURL.isEmpty())
        return problemURL;

    return getFileName();
}

QString MainWindow::getTabTitle(bool complete, bool star, int removeLength)
{
    QString tabTitle;
    if (!complete || (isUntitled() && problemURL.isEmpty()))
        tabTitle = getFileName();
    else if (!isUntitled())
        tabTitle = getFileName() + " - " + QFileInfo(filePath).path().remove(0, removeLength);
    else
        tabTitle = getFileName() + " - " + getProblemURL().remove(0, removeLength);
    if (star && isTextChanged())
        tabTitle += " *";
    return tabTitle;
}

Editor::CodeEditor *MainWindow::getEditor() const
{
    return editor;
}

bool MainWindow::isUntitled() const
{
    return filePath.isEmpty();
}

void MainWindow::setFilePath(QString path, bool updateBinder)
{
    if (QFile::exists(path))
        path = QFileInfo(path).canonicalFilePath();
    LOG_INFO(INFO_OF(path));
    if (filePath == path)
    {
        updateWatcher();
        return;
    }
    filePath = path;
    if (updateBinder)
        FileProblemBinder::set(path, problemURL);
    if (!isUntitled())
    {
        auto recentFiles = SettingsHelper::getRecentFiles();
        recentFiles.removeAll(filePath);
        recentFiles.push_front(filePath);
        if (recentFiles.length() > MAX_NUMBER_OF_RECENT_FILES)
            recentFiles.erase(recentFiles.begin() + MAX_NUMBER_OF_RECENT_FILES, recentFiles.end());
        SettingsHelper::setRecentFiles(recentFiles);

        emit requestUpdateLanguageServerFilePath(this, path);
    }
    emit editorFileChanged();
    updateWatcher();
}

void MainWindow::setProblemURL(const QString &url)
{
    LOG_INFO(INFO_OF(url));
    if (problemURL == url)
        return;
    problemURL = url;
    FileProblemBinder::set(filePath, url);
    if (problemURL.contains("codeforces.com"))
        setCFToolUI();
    emit editorFileChanged();
}

void MainWindow::setUntitledIndex(int index)
{
    untitledIndex = index;
}

#define FROMSTATUS(x) x = status.value(#x)
#define FROMSTATUS_DEFAULT(x, y) x = status.value(#x, y)
MainWindow::EditorStatus::EditorStatus(const QMap<QString, QVariant> &status)
{
    LOG_INFO("Window status from map");
    FROMSTATUS(isLanguageSet).toInt();
    FROMSTATUS(filePath).toString();
    FROMSTATUS(savedText).toString();
    FROMSTATUS(problemURL).toString();
    FROMSTATUS(editorText).toString();
    FROMSTATUS(language).toString();
    FROMSTATUS(customCompileCommand).toString();
    FROMSTATUS(editorCursor).toInt();
    FROMSTATUS(editorAnchor).toInt();
    FROMSTATUS(horizontalScrollBarValue).toInt();
    FROMSTATUS(verticalScrollbarValue).toInt();
    FROMSTATUS(untitledIndex).toInt();
    FROMSTATUS(checkerIndex).toInt();
    FROMSTATUS_DEFAULT(customTimeLimit, -1).toInt();
    FROMSTATUS(input).toStringList();
    FROMSTATUS(expected).toStringList();
    FROMSTATUS(customCheckers).toStringList();
    FROMSTATUS(testcasesIsShow).toList();
    FROMSTATUS(testCaseSplitterStates).toList();
}
#undef FROMSTATUS

#define TOSTATUS(x) status[#x] = x
QMap<QString, QVariant> MainWindow::EditorStatus::toMap() const
{
    LOG_INFO("Window status to hashmap");
    QMap<QString, QVariant> status;
    TOSTATUS(isLanguageSet);
    TOSTATUS(filePath);
    TOSTATUS(savedText);
    TOSTATUS(problemURL);
    TOSTATUS(editorText);
    TOSTATUS(language);
    TOSTATUS(customCompileCommand);
    TOSTATUS(editorCursor);
    TOSTATUS(editorAnchor);
    TOSTATUS(horizontalScrollBarValue);
    TOSTATUS(verticalScrollbarValue);
    TOSTATUS(untitledIndex);
    TOSTATUS(checkerIndex);
    TOSTATUS(customTimeLimit);
    TOSTATUS(input);
    TOSTATUS(expected);
    TOSTATUS(customCheckers);
    TOSTATUS(testcasesIsShow);
    TOSTATUS(testCaseSplitterStates);
    return status;
}
#undef TOSTATUS

MainWindow::EditorStatus MainWindow::toStatus() const
{
    EditorStatus status;

    status.isLanguageSet = isLanguageSet;
    status.filePath = filePath;
    status.problemURL = problemURL;
    status.language = language;
    status.customCompileCommand = customCompileCommand;
    status.untitledIndex = untitledIndex;
    status.checkerIndex = testcases->checkerIndex();
    status.customCheckers = testcases->customCheckers();
    status.editorText = editor->toPlainText();
    status.editorCursor = editor->textCursor().position();
    status.editorAnchor = editor->textCursor().anchor();
    status.horizontalScrollBarValue = editor->horizontalScrollBar()->value();
    status.verticalScrollbarValue = editor->verticalScrollBar()->value();
    status.customTimeLimit = customTimeLimit;
    status.input = testcases->inputs();
    status.expected = testcases->expecteds();
    for (int i = 0; i < testcases->count(); ++i)
        status.testcasesIsShow.push_back(testcases->isChecked(i));
    status.testCaseSplitterStates = testcases->splitterStates();

    return status;
}

void MainWindow::loadStatus(const EditorStatus &status, bool duplicate)
{
    LOG_INFO("Requesting loadStatus");
    setProblemURL(status.problemURL);
    if (status.isLanguageSet)
        setLanguage(status.language);
    customCompileCommand = status.customCompileCommand; // this must be after setLanguage
    if (!duplicate)
    {
        untitledIndex = status.untitledIndex;
        setFilePath(status.filePath);
    }
    testcases->addCustomCheckers(status.customCheckers);
    testcases->setCheckerIndex(status.checkerIndex);
    savedText = status.savedText;
    editor->setPlainText(status.editorText);
    auto cursor = editor->textCursor();
    cursor.setPosition(status.editorAnchor);
    cursor.setPosition(status.editorCursor, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    editor->horizontalScrollBar()->setValue(status.horizontalScrollBarValue);
    editor->verticalScrollBar()->setValue(status.verticalScrollbarValue);
    customTimeLimit = status.customTimeLimit;
    testcases->loadStatus(status.input, status.expected);
    for (int i = 0; i < status.testcasesIsShow.count() && i < testcases->count(); ++i)
        testcases->setChecked(i, status.testcasesIsShow[i].toBool());
    testcases->restoreSplitterStates(status.testCaseSplitterStates);
}

void MainWindow::applyCompanion(const Extensions::CompanionData &data)
{
    LOG_INFO("Requesting apply from companion");

    if (isUntitled() && !isTextChanged())
    {
        QString comments = SettingsHelper::getCompetitiveCompanionHeadComments().replace(
            "${time}",
            QDateTime::currentDateTime().toString(SettingsHelper::getCompetitiveCompanionHeadCommentsTimeFormat()));

        auto it = QRegularExpression(R"(\$\{json\..+?\})").globalMatch(comments);

        QString finalComments;
        int lastEnd = 0;

        while (it.hasNext())
        {
            auto match = it.next();
            finalComments += comments.mid(lastEnd, match.capturedStart() - lastEnd);
            auto path = match.captured().mid(7, match.capturedLength() - 8).split(".");
            auto value = QJsonValue::fromVariant(data.doc.toVariant());
            for (auto const &attr : path)
                value = value[attr];
            if (value.isUndefined())
            {
                log->error("Companion",
                           tr("Unknown attribute: [%1]. Please check the head comments setting at %2.")
                               .arg(path.join('.').arg(SettingsHelper::pathOfCompetitiveCompanionHeadComments())),
                           false);
            }
            else if (value.isObject() || value.isArray())
            {
                finalComments += QJsonDocument::fromVariant(value.toVariant()).toJson(QJsonDocument::Compact);
            }
            else
            {
                // convert to QVariant first so that all types can be converted to string
                finalComments += value.toVariant().toString();
            }
            lastEnd = match.capturedEnd();
        }

        finalComments += comments.mid(lastEnd);

        if (SettingsHelper::isCompetitiveCompanionHeadCommentsPoweredByCPEditor())
        {
            if (!finalComments.isEmpty())
                finalComments += "\n\n";
            finalComments += "Powered by CP Editor (https://cpeditor.org)";
        }

        if (!finalComments.isEmpty())
        {
            finalComments.replace(QRegularExpression("^", QRegularExpression::MultilineOption),
                                  language == "Python" ? "# " : "// ");
            finalComments += "\n\n";
        }

        auto cursor = editor->textCursor();
        int cursorPos = cursor.position(); // keep Template Cursor Position
        editor->setPlainText(finalComments + editor->toPlainText());
        cursor.setPosition(cursorPos + finalComments.length());
        editor->setTextCursor(cursor);
    }

    testcases->clear();

    for (auto const &testcase : data.testcases)
        testcases->addTestCase(testcase.input, testcase.output);

    setProblemURL(data.url);

    if (SettingsHelper::isCompetitiveCompanionSetTimeLimitForTab())
        customTimeLimit = data.timeLimit;
}

void MainWindow::applySettings(const QString &pagePath)
{
    LOG_INFO(INFO_OF(pagePath));

    auto pageChanged = [this, pagePath](const QString &page) {
        if (!appWindow->getPreferencesWindow()->pathExists(page))
            LOG_DEV("Unknown path: " << page);
        return pagePath.isEmpty() || pagePath == page;
    };

    if (pageChanged("Extensions/CF Tool"))
    {
        cftoolPath = SettingsHelper::getCFPath();

        if (cftool != nullptr && Extensions::CFTool::check(cftoolPath))
        {
            cftool->updatePath(cftoolPath);
            if (submitToCodeforces != nullptr)
                submitToCodeforces->setEnabled(true);
        }
    }

    if (pageChanged("Code Editing") || pagePath.startsWith("Appearance/") ||
        pageChanged(QString("Language/%1/%1 Parentheses").arg(language)))
    {
        if (pageChanged("Code Editing"))
        {
            editor->setVimCursor(SettingsHelper::isFakeVimEnable());
            ui->cursorInfo->setVisible(!SettingsHelper::isFakeVimEnable());
            delete fakevimHandler;
            fakevimHandler = nullptr;
            setStatusBar(nullptr);
            if (SettingsHelper::isFakeVimEnable())
            {
                fakevimHandler = new FakeVim::Internal::FakeVimHandler(editor, nullptr);

                Core::FakeVimProxy::connectSignals(fakevimHandler, editor, this, appWindow);
                Core::FakeVimProxy::initHandler(fakevimHandler);
                Core::FakeVimProxy::sourceVimRc(fakevimHandler);
            }
            Core::FakeVimProxy::clearUndoRedo(editor);
        }
        editor->applySettings(language);
    }

    if (!isLanguageSet && pageChanged("Language/General"))
    {
        setLanguage(SettingsHelper::getDefaultLanguage());
    }

    if (pageChanged("Appearance/General"))
    {
        testcases->updateHeights();
        updateCompileAndRunButtons();
    }

    if (pageChanged("Appearance/Font"))
    {
        log->setFont(SettingsHelper::getMessageLoggerFont());
        testcases->setTestCaseEditFont(SettingsHelper::getTestCasesFont());
    }

    if (pageChanged("Language/C++/C++ Commands"))
        updateChecker();

    if (pageChanged("Actions/Auto Save"))
    {
        if (SettingsHelper::isAutoSave())
        {
            autoSaveTimer->setSingleShot(SettingsHelper::getAutoSaveIntervalType() != "Without modification");
            autoSaveTimer->setInterval(SettingsHelper::getAutoSaveInterval());
            autoSaveTimer->start();
        }
        else
            autoSaveTimer->stop();
    }
}

bool MainWindow::save(bool force, const QString &head, bool safe)
{
    LOG_INFO("Save " << BOOL_INFO_OF(force) << INFO_OF(head) << BOOL_INFO_OF(safe));
    return saveFile(force ? AlwaysSave : IgnoreUntitled, head, safe);
}

void MainWindow::saveAs()
{
    LOG_INFO("Save as clicked");
    saveFile(SaveAs, tr("Save as"), true);
}

void MainWindow::on_compile_clicked()
{
    LOG_INFO("Compile button clicked");
    compileOnly();
}

void MainWindow::on_runOnly_clicked()
{
    LOG_INFO("Run only button clicked");
    runOnly();
}

void MainWindow::on_run_clicked()
{
    LOG_INFO("Run button clicked");
    compileAndRun();
}

void MainWindow::compileOnly()
{
    LOG_INFO("Requesting Compile Only");
    emit compileOrRunTriggered();
    afterCompile = Nothing;
    log->clear();
    compile();
}

void MainWindow::runOnly()
{
    LOG_INFO("Requesting Run only");
    emit compileOrRunTriggered();
    log->clear();
    run();
}

void MainWindow::compileAndRun()
{
    LOG_INFO("Requested Compile and Run");
    emit compileOrRunTriggered();
    afterCompile = Run;
    log->clear();
    compile();
}

void MainWindow::formatSource(bool selectionOnly, bool logOnNoChange)
{
    LOG_INFO("Requested code format");
    if (language == "Python")
        Extensions::YAPFormatter(editor, language, selectionOnly, logOnNoChange, log, this).format();
    else
        Extensions::ClangFormatter(editor, language, selectionOnly, logOnNoChange, log, this).format();
}

void MainWindow::setLanguage(const QString &lang)
{
    LOG_INFO(INFO_OF(lang));
    if (lang == language)
    {
        LOG_INFO("Language not changed");
        return;
    }
    if (!QFile::exists(filePath))
    {
        QString templateContent;
        if (!language.isEmpty())
            templateContent = Util::readFile(SettingsManager::get(QString("%1/Template Path").arg(language)).toString(),
                                             tr("Open %1 Template").arg(language), log);
        if (templateContent == editor->toPlainText())
        {
            language = lang;
            loadFile(filePath);
        }
    }
    language = lang;
    if (language != "Python" && language != "Java")
        language = "C++";
    editor->applySettings(language);
    customCompileCommand.clear();
    ui->changeLanguageButton->setText(language);
    updateCompileAndRunButtons();
    isLanguageSet = true;
    emit editorLanguageChanged(this);
}

QString MainWindow::getLanguage()
{
    return language;
}

MessageLogger *MainWindow::getLogger()
{
    return log;
}

void MainWindow::insertText(const QString &text)
{
    editor->insertPlainText(text);
}

void MainWindow::setViewMode(const QString &mode)
{
    if (mode == "code")
    {
        ui->leftWidget->show();
        ui->rightWidget->hide();
    }
    else if (mode == "io")
    {
        ui->leftWidget->hide();
        ui->rightWidget->show();
    }
    else
    {
        ui->leftWidget->show();
        ui->rightWidget->show();
        ui->splitter->restoreState(SettingsHelper::getSplitterSize());
    }
}

void MainWindow::detachedExecution()
{
    LOG_INFO("Executing in detached mode");
    afterCompile = RunDetached;
    log->clear();
    compile();
}

void MainWindow::killProcesses()
{
    LOG_INFO("Killing all processes");

    if (killingProcesses) // prevent deleting the same pointer multiple times
    {
        LOG_INFO("Already killing processes");
        return;
    }

    killingProcesses = true;

    if (compiler != nullptr)
    {
        delete compiler;
        compiler = nullptr;
    }

    for (auto &t : runner)
    {
        delete t;
    }
    runner.clear();

    if (detachedRunner != nullptr)
    {
        delete detachedRunner;
        detachedRunner = nullptr;
    }

    killingProcesses = false;
}

//***************** HELPER FUNCTIONS *****************

void MainWindow::setText(const QString &text, bool keep)
{
    if (keep)
    {
        auto cursor = editor->textCursor();
        int oldPos = cursor.position();
        cursor.select(QTextCursor::Document);
        cursor.insertText(text);
        cursor.setPosition(oldPos);
        editor->setTextCursor(cursor);
    }
    else
        editor->setPlainText(text);
}

void MainWindow::updateWatcher()
{
    if (fileWatcher->files() == QStringList(filePath))
        return;
    if (!fileWatcher->files().isEmpty())
        fileWatcher->removePaths(fileWatcher->files());
    if (!isUntitled())
        fileWatcher->addPath(filePath);
}

void MainWindow::loadFile(const QString &loadPath)
{
    LOG_INFO(INFO_OF(loadPath));

    auto path = loadPath;

    bool samePath = !isUntitled() && filePath == path;
    setFilePath(path, false);

    bool isTemplate = false;

    if (!QFile::exists(path))
    {
        QString templatePath = SettingsManager::get(QString("%1/Template Path").arg(language)).toString();

        QFile f(templatePath);

        if (!templatePath.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            isTemplate = true;
            path = templatePath;
        }
        else
        {
            setText("");
            return;
        }
    }

    auto content = Util::readFile(path, tr("Open File"), log);

    if (content.isNull())
        return;

    savedText = content;
    if (content.length() > SettingsHelper::getOpenFileLengthLimit())
    {
        log->error(tr("Open File"),
                   tr("The file [%1] contains more than %2 characters, so it's not opened. You can change the "
                      "open file length limit at %3.")
                       .arg(path)
                       .arg(SettingsHelper::getOpenFileLengthLimit())
                       .arg(SettingsHelper::pathOfOpenFileLengthLimit()),
                   false);
        setText("");
        setFilePath("");
        return;
    }

    if (!isUntitled() && SettingsHelper::isRestoreOldProblemUrl() && FileProblemBinder::containsFile(filePath))
        setProblemURL(FileProblemBinder::getProblemForFile(filePath));

    setText(content, samePath);

    if (isTemplate)
    {
        auto match = QRegularExpression(SettingsManager::get(language + "/Template Cursor Position Regex").toString())
                         .match(content);
        if (match.hasMatch())
        {
            int pos = SettingsManager::get(language + "/Template Cursor Position Offset Type").toString() == "start"
                          ? match.capturedStart()
                          : match.capturedEnd();
            pos += SettingsManager::get(language + "/Template Cursor Position Offset Characters").toInt();
            pos = qMax(pos, 0);
            pos = qMin(pos, content.length());
            auto cursor = editor->textCursor();
            cursor.setPosition(pos);
            editor->setTextCursor(cursor);
        }
        else
        {
            editor->moveCursor(QTextCursor::End);
        }
    }

    loadTests();
}

bool MainWindow::saveFile(SaveMode mode, const QString &head, bool safe)
{
    LOG_INFO(INFO_OF(mode) << INFO_OF(head) << BOOL_INFO_OF(safe));

    if ((mode != AutoSave && SettingsHelper::isFormatOnManualSave()) ||
        (mode == AutoSave && SettingsHelper::isFormatOnAutoSave()))
    {
        formatSource(false, false);
    }

    if (mode == SaveAs || (isUntitled() && mode == AlwaysSave))
    {
        QString defaultPath;

        if (!isUntitled())
        {
            defaultPath = filePath;
        }
        else
        {
            if (!problemURL.isEmpty())
            {
                auto rules = SettingsHelper::getDefaultFilePathsForProblemURLs();
                for (auto const &rule : rules)
                {
                    if (rule.toStringList().front().isEmpty())
                        continue;
                    auto url = QRegularExpression(rule.toStringList().front());
                    auto match = url.match(problemURL);
                    if (match.hasMatch())
                    {
                        defaultPath = match.captured().replace(url, rule.toStringList().back());
                        break;
                    }
                }
            }

            if (defaultPath.isEmpty())
            {
                defaultPath =
                    QDir(DefaultPathManager::defaultPathForAction("Save File")).filePath(getTabTitle(false, false));
            }

            defaultPath = Util::fileNameWithSuffix(defaultPath, language);
        }

        // Create the parent directories of the default path if they don't exist,
        // and remove them if they are empty after saving.

        QStringList madePaths;
        if (!QDir(QFileInfo(defaultPath).absolutePath()).exists())
        {
            QDir dir(QFileInfo(defaultPath).absolutePath());
            while (!dir.exists() && !dir.isRoot())
            {
                madePaths.push_back(dir.path());
                dir.setPath(QDir::cleanPath(dir.filePath("..")));
            }
            QDir().mkpath(QFileInfo(defaultPath).absolutePath());
        }
        auto beforeReturn = [&](bool ret) {
            for (auto const &path : madePaths)
                if (!QDir().rmdir(path))
                    break;
            return ret;
        };

        emit confirmTriggered(this);
        auto newFilePath =
            QFileDialog::getSaveFileName(this, tr("Save File"), defaultPath, Util::fileNameFilter(true, true, true));
        if (newFilePath.isEmpty())
            return beforeReturn(false);

        if (!Util::saveFile(newFilePath, editor->toPlainText(), head, safe, log, true))
            return beforeReturn(false);

        savedText = editor->toPlainText();

        setFilePath(newFilePath);

        DefaultPathManager::setDefaultPathForAction("Save File", QFileInfo(filePath).canonicalPath());

        auto suffix = QFileInfo(filePath).suffix();
        if (Util::cppSuffix.contains(suffix))
            setLanguage("C++");
        else if (Util::javaSuffix.contains(suffix))
            setLanguage("Java");
        else if (Util::pythonSuffix.contains(suffix))
            setLanguage("Python");

        beforeReturn(true);
    }
    else if (!isUntitled())
    {
        if (!Util::saveFile(filePath, editor->toPlainText(), head, safe, log, true))
            return false;

        savedText = editor->toPlainText();
    }
    else
    {
        return false;
    }

    setFilePath(filePath); // make sure that the file path is the canonical file path and the file watcher is working
    emit editorTextChanged(this); // make sure that the tab title is updated

    saveTests(safe);

    return true;
}

QString MainWindow::tmpPath()
{
    bool created = false;
    if (tmpDir == nullptr || !tmpDir->isValid() || !QDir(tmpDir->path()).exists())
    {
        delete tmpDir;
        tmpDir = new QTemporaryDir();
        if (!tmpDir->isValid())
        {
            LOG_ERR("Failed to create the temporary directory");
            log->error(tr("Temp File"), tr("Failed to create the temporary directory"));
            return QString();
        }
        created = true;
    }
    QString name;
    if (language == "C++")
        name = "sol." + Util::cppSuffix.first();
    else if (language == "Java")
        name = SettingsHelper::getJavaClassName() + "." + Util::javaSuffix.first();
    else if (language == "Python")
        name = "sol." + Util::pythonSuffix.first();
    else
    {
        log->error(tr("Temp File"), tr("Please set the language"));
        return "";
    }
    QString path = tmpDir->filePath(name);
    if (!Util::saveFile(path, editor->toPlainText(), tr("Temp File"), false, log))
        return QString();
    if (created && isUntitled())
        emit requestUpdateLanguageServerFilePath(this, path);
    return path;
}

QString MainWindow::filePathOrTmpPath()
{
    return isUntitled() ? tmpPath() : filePath;
}

void MainWindow::updateCompileCommand()
{
    bool ok = false;
    const auto command =
        QInputDialog::getText(this, tr("Set Compile Command"), tr("Custom compile command for this tab:"),
                              QLineEdit::Normal, compileCommand(), &ok);
    if (ok)
        customCompileCommand = command;
}

void MainWindow::updateTimeLimit()
{
    bool ok = false;
    const int limit = QInputDialog::getInt(this, tr("Set Time Limit"), tr("Custom time limit for this tab: (ms)"),
                                           timeLimit(), 1, 3600000, 1000, &ok);
    if (ok)
        customTimeLimit = limit;
}

bool MainWindow::isTextChanged() const
{
    if (isUntitled())
    {
        auto content = Util::readFile(SettingsManager::get(QString("%1/Template Path").arg(language)).toString(),
                                      tr("Read %1 Template").arg(language), log);
        if (content.isNull())
            return !editor->toPlainText().isEmpty();
        return editor->toPlainText() != content;
    }
    auto content = Util::readFile(filePath);

    if (content.isNull())
        return true;

    return editor->toPlainText() != content;
}

bool MainWindow::closeConfirm()
{
    LOG_INFO("Confirming to close this window");
    bool confirmed = !isTextChanged();
    if (!confirmed)
    {
        emit confirmTriggered(this);
        auto res = QMessageBox::warning(
            this, tr("Save changes"),
            tr("Save changes to [%1] before closing?").arg(isUntitled() ? tr("New File") : getFileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (res == QMessageBox::Save)
            confirmed = saveFile(AlwaysSave, tr("Save"), true);
        else if (res == QMessageBox::Discard)
            confirmed = true;
    }
    LOG_INFO(BOOL_INFO_OF(confirmed));
    return confirmed;
}

void MainWindow::on_clearMessagesButton_clicked()
{
    log->clear();
}

void MainWindow::on_changeLanguageButton_clicked()
{
    LOG_INFO("Window language change button requested");
    bool ok = false;
    int curr = 0;

    if (language == "C++")
        curr = 0;
    else if (language == "Java")
        curr = 1;
    else if (language == "Python")
        curr = 2;

    auto response = QInputDialog::getItem(this, tr("Set Tab language"), tr("Set the language to use in this Tab"),
                                          {"C++", "Java", "Python"}, curr, false, &ok);

    if (ok)
        setLanguage(response);
}

void MainWindow::onFileWatcherChanged(const QString &path)
{
    LOG_INFO(INFO_OF(path));

    emit editorTextChanged(this);

    auto currentText = editor->toPlainText();

    auto fileText = Util::readFile(path);

    if (!fileText.isNull())
    {
        if (fileText == savedText)
            return;

        if (fileText == currentText)
        {
            savedText = fileText;
            return;
        }

        if (savedText == currentText && SettingsHelper::isAutoLoadExternalChangesIfNoUnsavedModification())
        {
            loadFile(path);
            return;
        }

        if (!reloading && SettingsHelper::isAskForLoadingExternalChanges())
        {
            reloading = true;

            emit confirmTriggered(this);
            auto reload = QMessageBox::question(
                this, tr("Reload"), tr("[%1]\n\nhas been changed on disk.\nDo you want to reload it?").arg(filePath));

            reloading = false;

            if (reload == QMessageBox::StandardButton::Yes)
            {
                loadFile(path);
                return;
            }
        }
    }
}

void MainWindow::onTextChanged()
{
    if (SettingsHelper::isAutoSave() && SettingsHelper::getAutoSaveIntervalType() != "Without modification" &&
        (!autoSaveTimer->isActive() || SettingsHelper::getAutoSaveIntervalType() == "After the last modification"))
    {
        autoSaveTimer->start();
    }
    emit editorTextChanged(this);
}

void MainWindow::onEditorFontChanged(const QFont &newFont)
{
    SettingsHelper::setCodeEditorFont(newFont);
    emit editorFontChanged();
}

void MainWindow::updateCursorInfo()
{
    auto cursor = editor->textCursor();
    auto selection = cursor.selectedText();
    QString info;
    if (selection.isEmpty())
    {
        auto line = editor->document()->findBlockByNumber(cursor.blockNumber()).text();
        int column = cursor.columnNumber();
        int col = 0;
        for (int i = 0; i < column; ++i)
        {
            if (line[i] != '\t')
                ++col;
            else
                col += SettingsHelper::getTabWidth() - col % SettingsHelper::getTabWidth();
        }
        info = tr("Line %1, Column %2").arg(cursor.blockNumber() + 1).arg(col + 1);
    }
    else
    {
        int selectionStart = cursor.selectionStart();
        int selectionEnd = cursor.selectionEnd();
        cursor.setPosition(selectionStart);
        int lineStart = cursor.blockNumber();
        cursor.setPosition(selectionEnd);
        int lineEnd = cursor.blockNumber();
        int selectionLines = lineEnd - lineStart + 1;
        if (selectionLines > 1)
            info = tr("%1 lines, %2 characters selected").arg(selectionLines).arg(selection.length());
        else
            info = tr("%1 characters selected").arg(selection.length());
    }
    ui->cursorInfo->setText(info);
}

void MainWindow::updateChecker()
{
    delete checker;
    if (testcases->checkerType() == Core::Checker::Custom)
        checker = new Core::Checker(testcases->checkerText(), log, this);
    else
        checker = new Core::Checker(testcases->checkerType(), log, this);
    connect(checker, &Core::Checker::checkFinished, testcases, &Widgets::TestCases::setVerdict);
    checker->prepare(SettingsManager::get(QString("C++/Compile Command")).toString());
}

QSplitter *MainWindow::getSplitter()
{
    return ui->splitter;
}

QSplitter *MainWindow::getRightSplitter()
{
    return ui->rightSplitter;
}

QString MainWindow::compileCommand() const
{
    if (customCompileCommand.isEmpty())
        return SettingsManager::get(QString("%1/Compile Command").arg(language)).toString();
    return customCompileCommand;
}

int MainWindow::timeLimit() const
{
    if (customTimeLimit == -1)
        return SettingsHelper::getDefaultTimeLimit();
    return customTimeLimit;
}

void MainWindow::updateCompileAndRunButtons() const
{
    if (language == "Python")
    {
        ui->runOnly->show();
        ui->compile->hide();
        ui->run->hide();
    }
    else
    {
        if (SettingsHelper::isShowCompileAndRunOnly())
        {
            ui->run->show();
            ui->runOnly->hide();
            ui->compile->hide();
        }
        else
        {
            ui->run->show();
            ui->runOnly->show();
            ui->compile->show();
        }
    }
}

// -------------------- COMPILER SLOTS ---------------------------

void MainWindow::onCompilationStarted()
{
    log->info(tr("Compiler"), tr("Compilation has started"));
}

void MainWindow::onCompilationFinished(const QString &warning)
{
    if (language != "Python")
    {
        log->info(tr("Compiler"), tr("Compilation has finished"));
        if (!warning.trimmed().isEmpty())
        {
            log->warn(tr("Compile Warnings"), warning);
        }
    }

    if (afterCompile == Run)
    {
        run();
    }
    else if (afterCompile == RunDetached)
    {
        if (SettingsHelper::isSaveFileOnExecution())
            saveFile(IgnoreUntitled, tr("Runner"), true);

        killProcesses();

        if (!QStringList({"C++", "Java", "Python"}).contains(language))
        {
            log->warn(tr("Runner"), tr("Wrong language, please set the language"));
            return;
        }

        detachedRunner = new Core::Runner(-1);
        connect(detachedRunner, &Core::Runner::runStarted, this, &MainWindow::onRunStarted);
        connect(detachedRunner, &Core::Runner::failedToStartRun, this, &MainWindow::onFailedToStartRun);
        connect(detachedRunner, &Core::Runner::runKilled, this, &MainWindow::onRunKilled);
        detachedRunner->runDetached(tmpPath(), filePath, language,
                                    SettingsManager::get(QString("%1/Run Command").arg(language)).toString(),
                                    SettingsManager::get(QString("%1/Run Arguments").arg(language)).toString());
    }
}

void MainWindow::onCompilationErrorOccurred(const QString &error)
{
    log->error(tr("Compiler"), tr("Error occurred while compiling"));
    if (!error.trimmed().isEmpty())
    {
        log->error(tr("Compile Errors"), error);
        if (language == "Java" && error.contains("public class"))
        {
            log->warn(
                tr("Compile Errors"),
                tr("Have you set a proper name for the main class in your solution? If not, you can set it at %1.")
                    .arg(SettingsHelper::pathOfJavaClassName()),
                false);
        }
    }
}

void MainWindow::onCompilationFailed(const QString &reason)
{
    log->error(tr("Compiler"), tr("Failed to start compilation: %1").arg(reason), false);
}

void MainWindow::onCompilationKilled()
{
    log->error(tr("Compiler"), tr("Compilation is killed"));
}

// --------------------- RUNNER SLOTS ----------------------------

QString MainWindow::getRunnerHead(int index)
{
    if (index == -1)
        return tr("Detached Runner");
    return tr("Runner[%1]").arg(index + 1);
}

void MainWindow::onRunStarted(int index)
{
    log->info(getRunnerHead(index), tr("Execution has started"));
}

void MainWindow::onRunFinished(int index, const QString &out, const QString &err, int exitCode, qint64 timeUsed,
                               bool tle)
{
    auto head = getRunnerHead(index);

    if (exitCode == 0)
    {
        log->info(head, tr("Execution for test case #%1 has finished in %2ms").arg(index + 1).arg(timeUsed));

        if ((!out.isEmpty() && !testcases->expected(index).isEmpty()) ||
            (SettingsHelper::isCheckOnTestcasesWithEmptyOutput() && exitCode == 0))
            checker->reqeustCheck(index, testcases->input(index), out, testcases->expected(index));
    }

    else
    {
        if (tle)
        {
            log->warn(head, tr("Time Limit Exceeded"));
            testcases->setVerdict(index, Widgets::TestCase::TLE);
        }
        else
            testcases->setVerdict(index, Widgets::TestCase::RE);

        log->error(head, tr("Execution for test case #%1 has finished with non-zero exitcode %2 in %3ms")
                             .arg(index + 1)
                             .arg(exitCode)
                             .arg(timeUsed));
    }

    if (!err.trimmed().isEmpty())
        log->error(head + tr("/stderr"), err);
    testcases->setOutput(index, out);
}

void MainWindow::onFailedToStartRun(int index, const QString &error)
{
    log->error(getRunnerHead(index), error, false);
}

void MainWindow::onRunOutputLimitExceeded(int index, const QString &type)
{
    log->warn(
        getRunnerHead(index),
        tr("The %1 of the process running on the testcase #%2 contains more than %3 characters, which is longer "
           "than the output length limit, so the process is killed. You can change the output length limit at %4.")
            .arg(type)
            .arg(index + 1)
            .arg(SettingsHelper::getOutputLengthLimit())
            .arg(SettingsHelper::pathOfOutputLengthLimit()),
        false);
}

void MainWindow::onRunKilled(int index)
{
    log->error(getRunnerHead(index),
               tr("%1 has been killed")
                   .arg(index == -1 ? tr("Detached runner") : tr("Runner for testcase #%1").arg(index + 1)));
}
