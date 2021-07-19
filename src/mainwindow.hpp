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

#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>

class AppWindow;
class MessageLogger;
namespace Editor
{
class CodeEditor;
}
class QFileSystemWatcher;
class QPushButton;
class QSplitter;
class QTemporaryDir;
class AppWindow;

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

namespace Core
{
class Checker;
class Compiler;
class Runner;
class FakeVimCommand;
} // namespace Core

namespace Extensions
{
class CFTool;
struct CompanionData;
} // namespace Extensions

namespace Widgets
{
class TestCases;
}
namespace FakeVim
{
namespace Internal
{
class FakeVimHandler;
}
} // namespace FakeVim
class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    struct EditorStatus
    {
        bool isLanguageSet{};
        QString filePath, savedText, problemURL, editorText, language, customCompileCommand;
        int editorCursor{}, editorAnchor{}, horizontalScrollBarValue{}, verticalScrollbarValue{}, untitledIndex{},
            checkerIndex{}, customTimeLimit{};
        QStringList input, expected, customCheckers;
        QVariantList testcasesIsShow; // This can't be renamed to "isChecked" because that's not compatible
        QVariantList testCaseSplitterStates;

        EditorStatus() = default;

        explicit EditorStatus(const QMap<QString, QVariant> &status);

        QMap<QString, QVariant> toMap() const;
    };

    explicit MainWindow(int index, AppWindow *parent);
    explicit MainWindow(const QString &fileOpen, int index, AppWindow *parent);
    explicit MainWindow(const EditorStatus &status, bool duplicate, int index, AppWindow *parent);
    ~MainWindow() override;

    int getUntitledIndex() const;
    QString getFileName() const;
    QString getFilePath() const;
    QString getProblemURL() const;
    QString getCompleteTitle() const;
    QString getTabTitle(bool complete, bool star, int removeLength = 0);
    Editor::CodeEditor *getEditor() const;
    bool isUntitled() const;

    void setProblemURL(const QString &url);
    void setUntitledIndex(int index);

    EditorStatus toStatus() const;
    void loadStatus(const EditorStatus &status, bool duplicate = false);

    bool save(bool force, const QString &head, bool safe = true);
    void saveAs();

    bool isTextChanged() const;
    bool closeConfirm();

    void killProcesses();
    void detachedExecution();
    void compileOnly();
    void runOnly();
    void compileAndRun();
    void formatSource(bool selectionOnly, bool logOnNoChange);

    void applyCompanion(const Extensions::CompanionData &data);

    void setLanguage(const QString &lang);
    QString getLanguage();
    void applySettings(const QString &pagePath);

    MessageLogger *getLogger();
    QSplitter *getSplitter();
    QSplitter *getRightSplitter();

    void insertText(const QString &text);

    void setViewMode(const QString &mode);
    QString tmpPath();

    /**
     * @brief get the file path of a titled path, get the tmp path of an untitled path
     */
    QString filePathOrTmpPath();

    /**
     * @brief ask the user for the new compile command for this tab
     */
    void updateCompileCommand();

    /**
     * @brief ask the user for the new time limit for this tab
     */
    void updateTimeLimit();

  private slots:
    void onCompilationStarted();
    void onCompilationFinished(const QString &warning);
    void onCompilationErrorOccurred(const QString &error);
    void onCompilationFailed(const QString &reason);
    void onCompilationKilled();

    void onRunStarted(int index);
    void onRunFinished(int index, const QString &out, const QString &err, int exitCode, qint64 timeUsed, bool tle);
    void onFailedToStartRun(int index, const QString &error);
    void onRunOutputLimitExceeded(int index, const QString &type);
    void onRunKilled(int index);

    void onFileWatcherChanged(const QString &);
    void onEditorFontChanged(const QFont &newFont);
    void onTextChanged();
    void updateCursorInfo();
    void updateChecker();
    void runTestCase(int index);

    // UI Slots

    void on_compile_clicked();

    void on_runOnly_clicked();

    void on_run_clicked();

    void on_clearMessagesButton_clicked();

    void on_changeLanguageButton_clicked();

  signals:
    void editorFileChanged();
    void requestUpdateLanguageServerFilePath(MainWindow *window, const QString &path);
    void editorTextChanged(MainWindow *window);
    void editorFontChanged();
    void confirmTriggered(MainWindow *widget);
    void requestToastMessage(const QString &head, const QString &body);
    void editorLanguageChanged(MainWindow *window);
    void compileOrRunTriggered();

  private:
    enum SaveMode
    {
        IgnoreUntitled, // save only when filePath is not empty
        AutoSave,       // basically the same as IgnoreUntitled, only different in auto-format
        AlwaysSave,     // save to filePath if it's not empty, otherwise ask for new path
        SaveAs,         // ask for new path no matter filePath is empty or not
    };
    enum AfterCompile
    {
        Nothing,
        Run,
        RunDetached
    };

    Ui::MainWindow *ui;
    Editor::CodeEditor *editor;
    QString language;
    bool isLanguageSet = false;

    Core::Compiler *compiler = nullptr;
    QVector<Core::Runner *> runner;
    Core::Checker *checker = nullptr;
    Core::Runner *detachedRunner = nullptr;
    QTemporaryDir *tmpDir = nullptr;
    AfterCompile afterCompile = Nothing;

    MessageLogger *log = nullptr;

    AppWindow *appWindow = nullptr;

    int untitledIndex;
    QString problemURL;
    QString filePath;
    QString savedText;
    QString cftoolPath;
    QFileSystemWatcher *fileWatcher;

    std::atomic<bool> reloading;
    std::atomic<bool> killingProcesses;

    QPushButton *submitToCodeforces = nullptr;
    Extensions::CFTool *cftool = nullptr;
    FakeVim::Internal::FakeVimHandler *fakevimHandler = nullptr;

    Widgets::TestCases *testcases = nullptr;

    QTimer *autoSaveTimer = nullptr;

    int customTimeLimit = -1;     // the custom time limit for this tab, -1 represents for the same as settings
    QString customCompileCommand; // the custom compile command for this tab, empty represents for the same as settings

    void setEditor();
    void compile();
    void run();
    void run(int index);
    void loadTests();
    void saveTests(bool safe);
    void setCFToolUI();
    void setFilePath(QString path, bool updateBinder = true);
    void setText(const QString &text, bool keep = false);
    void updateWatcher();
    void loadFile(const QString &loadPath);
    bool saveFile(SaveMode mode, const QString &head, bool safe);
    void performCompileAndRunDiagonistics();
    static QString getRunnerHead(int index);
    QString compileCommand() const;
    int timeLimit() const;
    void updateCompileAndRunButtons() const;
    friend class Core::FakeVimCommand;
};
#endif // MAINWINDOW_HPP
