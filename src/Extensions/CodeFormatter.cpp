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

#include "Extensions/CodeFormatter.hpp"
#include "Core/EventLogger.hpp"
#include "Core/MessageLogger.hpp"
#include "Editor/CodeEditor.hpp"
#include "Settings/SettingsManager.hpp"
#include "Util/FileUtil.hpp"
#include <QProcess>
#include <QTemporaryDir>

namespace Extensions
{

CodeFormatter::CodeFormatter(Editor::CodeEditor *editor, const QString &lang, bool selectionOnly, bool logOnNoChange,
                             MessageLogger *log, QObject *parent)
    : QObject(parent), m_editor(editor), m_lang(lang), m_selectionOnly(selectionOnly), m_logOnNoChange(logOnNoChange),
      log(log)
{
    LOG_INFO(INFO_OF(lang) << INFO_OF(selectionOnly) << INFO_OF(logOnNoChange));

    auto cursor = editor->textCursor();
    m_cursorPos = cursor.position();
    m_cursorLine = cursor.blockNumber();
    m_cursorCol = cursor.columnNumber();
    m_anchorPos = cursor.anchor();
    cursor.setPosition(m_anchorPos);
    m_anchorLine = cursor.blockNumber();
    m_anchorCol = cursor.columnNumber();

    LOG_INFO(INFO_OF(m_cursorPos) << INFO_OF(m_cursorLine) << INFO_OF(m_anchorPos) << INFO_OF(m_anchorLine));
}

void CodeFormatter::format() const
{
    QStringList args = arguments() << QProcess::splitCommand(getSetting("Arguments").toString());

    if (formatSelectionOnly())
        args.append(rangeArgs());

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid())
    {
        log->error(tr("Formatter"), tr("Failed to create temporary directory"));
        return;
    }

    auto tmpPath = tmpDir.filePath(Util::fileNameWithSuffix("tmp", m_lang));
    args.append(tmpPath);

    if (!Util::saveFile(tmpPath, m_editor->toPlainText(), tr("Formatter"), true, log))
        return;

    if (!Util::saveFile(tmpDir.filePath(styleFileName()), getSetting("Style").toString(), tr("Formatter"), true, log))
        return;

    auto res = runProcess(args);
    if (res.isEmpty())
        return;

    auto source = newSource(res);

    if (source == m_editor->toPlainText())
    {
        if (m_logOnNoChange)
            log->info(tr("Formatter"), tr("Formatting completed"));
        return;
    }

    auto cursor = m_editor->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.insertText(source);

    m_editor->setTextCursor(newCursor(res, args));

    log->info(tr("Formatter"), tr("Formatting completed"));
}

QString CodeFormatter::runProcess(const QStringList &args) const
{
    QProcess formatProcess;
    formatProcess.start(getSetting("Program").toString(), args);
    LOG_INFO(INFO_OF(formatProcess.program()) << INFO_OF(formatProcess.arguments().join(' ')));

    bool finished = formatProcess.waitForFinished(2000); // blocks main Thread
    if (!finished)
    {
        formatProcess.kill();
        log->error(tr("Formatter"),
                   tr("The format process didn't finish in 2 seconds. This is probably because the %1 program is not "
                      "found by CP Editor. You can set the path to the program at %2.")
                       .arg(settingKey())
                       .arg(SettingsManager::getPathText(settingKey() + "/Program")),
                   false);
        return QString();
    }

    auto exitCode = formatProcess.exitCode();

    if (exitCode != 0)
    {
        LOG_WARN(INFO_OF(exitCode));

        log->warn(tr("Formatter"), tr("The format command [%1 %2] finished with exit code %3.")
                                       .arg(formatProcess.program())
                                       .arg(formatProcess.arguments().join(' '))
                                       .arg(exitCode));
        auto stdOut = formatProcess.readAllStandardOutput();
        if (!stdOut.isEmpty())
            log->warn(tr("Formatter[stdout]"), stdOut);
        auto stdError = formatProcess.readAllStandardError();
        if (!stdError.isEmpty())
            log->error(tr("Formatter[stderr]"), stdError);
        return QString();
    }

    auto out = formatProcess.readAllStandardOutput();

    if (out.isEmpty())
    {
        LOG_WARN("Output is empty");
        log->warn(tr("Formatter"), tr("The output of the format process is empty. Please ensure there is no in-place "
                                      "modification option in the formatting arguments."));
    }

    return out;
}

bool CodeFormatter::formatSelectionOnly() const
{
    return m_selectionOnly && m_cursorPos != m_anchorPos;
}

QVariant CodeFormatter::getSetting(const QString &key) const
{
    return SettingsManager::get(settingKey() + "/" + key);
}

} // namespace Extensions
