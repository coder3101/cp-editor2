/*
 * Copyright (C) 2019-2020 Ashar Khan <ashar786khan@gmail.com>
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

#include "Core/FakeVimCommand.hpp"
#include "Util/FileUtil.hpp"
#include "Widgets/TestCases.hpp"
#include "appwindow.hpp"
#include "generated/SettingsHelper.hpp"
#include "third_party/FakeVim/fakevim/fakevimhandler.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>

namespace Core
{

FakeVimCommand::FakeVimCommand(AppWindow *window, QObject *parent) : QObject(parent), appwin(window)
{
}

FakeVimCommand::CommandTypes FakeVimCommand::customCommandType(FakeVim::Internal::ExCommand const &ex)
{
    if (ex.cmd == "new")
        return CommandTypes::New;

    if (ex.cmd == "open" || ex.cmd == "opn")
        return CommandTypes::Open;

    if (ex.cmd == "compile" || ex.cmd == "cmp")
        return CommandTypes::Compile;

    if (ex.cmd == "crun" || ex.cmd == "crn")
        return CommandTypes::CompileRun;

    if (ex.cmd == "run")
        return CommandTypes::Run;

    if (ex.cmd == "drun" || ex.cmd == "drn")
        return CommandTypes::DetachedRun;

    if (ex.cmd == "killall" || ex.cmd == "kap")
        return CommandTypes::KillProcess;

    if (ex.cmd == "format" || ex.cmd == "fmt")
        return CommandTypes::FormatCode;

    if (ex.cmd == "snippet" || ex.cmd == "snp")
        return CommandTypes::Snippets;

    if (ex.cmd == "vmode" || ex.cmd == "vmd")
        return CommandTypes::Vmode;

    if (ex.cmd == "preference" || ex.cmd == "prf")
        return CommandTypes::Preference;

    if (ex.cmd == "chlang" || ex.cmd == "chl")
        return CommandTypes::Chlang;

    if (ex.cmd == "clear" || ex.cmd == "clr")
        return CommandTypes::Clear;

    if (ex.cmd == "exit" || ex.cmd == "ext")
        return CommandTypes::Exit;

    return CommandTypes::Unknown;
}

bool FakeVimCommand::handleCustomCommand(CommandTypes type, QString const &args, bool hasbang)
{
    if (type == CommandTypes::Unknown)
        return false;

    switch (type)
    {
    case CommandTypes::New: {
        const QString lang = args.isEmpty() ? SettingsHelper::getDefaultLanguage() : language(args);

        if (!lang.isEmpty())
            appwin->openTab("", lang);
        else
            showError(tr("`new` requires no argument or one of 'cpp', 'java' and 'python', got [%1]").arg(args));
        break;
    }
    case CommandTypes::Open: {
        QString path = args;

        if (path.isEmpty())
        {
            appwin->on_actionOpen_triggered();
            break;
        }

        if (path.startsWith('~'))
            path = QDir::home().filePath(args.mid(1));

        QFileInfo file(path);

        if (!Util::cppSuffix.contains(file.suffix()) && !Util::javaSuffix.contains(file.suffix()) &&
            !Util::javaSuffix.contains(file.suffix()))
        {
            showError(tr("[%1] is not C++, Python or Java source file").arg(file.absoluteFilePath()));
            break;
        }

        if (!file.exists() && !hasbang)
        {
            showError(tr("[%1] does not exist. To open a tab with a non-existing file, use `open!` instead").arg(path));
            break;
        }

        appwin->openTab(file.absoluteFilePath());
        break;
    }
    case CommandTypes::Compile: {
        appwin->on_actionCompile_triggered();
        break;
    }
    case CommandTypes::CompileRun: {
        appwin->on_actionCompileRun_triggered();
        break;
    }
    case CommandTypes::Run: {

        if (args.isEmpty())
            appwin->on_actionRun_triggered();
        else // args is not empty
        {
            bool ok = false;
            int caseNum = args.toInt(&ok);

            if (!ok)
                showError(tr("[%1] is not a number").arg(args));
            else // args is a number
            {
                if (appwin->currentWindow() && caseNum > 0 &&
                    caseNum <= appwin->currentWindow()->testcases->count()) // args is valid
                    appwin->currentWindow()->runTestCase(caseNum - 1);
                else
                    showError(
                        tr("%1 is out of range [1, %2]").arg(args).arg(appwin->currentWindow()->testcases->count()));
            }
        }

        break;
    }
    case CommandTypes::DetachedRun: {
        appwin->on_actionRunDetached_triggered();
    }
    break;
    case CommandTypes::KillProcess: {
        appwin->on_actionKillProcesses_triggered();
        break;
    }
    case CommandTypes::FormatCode: {
        appwin->on_actionFormatCode_triggered();
        break;
    }
    case CommandTypes::Snippets: {
        appwin->on_actionUseSnippets_triggered();
        break;
    }
    case CommandTypes::Vmode: {
        if (args == "edit")
            appwin->on_actionEditorMode_triggered();
        else if (args == "split")
            appwin->on_actionSplitMode_triggered();
        else
            showError(tr("[%1] is not a valid view mode. It should be one of 'split' and 'edit'").arg(args));
        break;
    }
    case CommandTypes::Preference: {
        appwin->on_actionSettings_triggered();
        break;
    }
    case CommandTypes::Chlang: {
        const QString lang = language(args);

        if (!lang.isEmpty() && appwin->currentWindow())
            appwin->currentWindow()->setLanguage(lang);
        else
            showError(tr("%1 is not a valid language name. It should be one of 'cpp', 'java' and 'python'").arg(args));
        break;
    }
    case CommandTypes::Clear: {
        appwin->currentWindow()->on_clearMessagesButton_clicked();
        break;
    }
    case CommandTypes::Exit: {
        appwin->on_actionQuit_triggered();
        break;
    }
    case CommandTypes::Unknown: {
        Q_UNREACHABLE();
    }
    };
    return true;
}

void FakeVimCommand::showError(QString const &message)
{
    if (auto *handler = qobject_cast<FakeVim::Internal::FakeVimHandler *>(parent()))
    {
        handler->showMessage(FakeVim::Internal::MessageInfo, message);
    }
}

QString FakeVimCommand::language(const QString &langCode)
{
    const auto code = langCode.toLower().trimmed();
    if (code == "cpp" || code == "c++")
        return "C++";
    if (code == "java")
        return "Java";
    if (code == "py" || code == "python")
        return "Python";
    return QString();
}

} // namespace Core
