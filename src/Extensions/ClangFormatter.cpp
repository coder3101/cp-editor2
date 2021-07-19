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

#include "Extensions/ClangFormatter.hpp"
#include "Core/EventLogger.hpp"
#include "Core/MessageLogger.hpp"
#include "Editor/CodeEditor.hpp"
#include "Util/FileUtil.hpp"
#include "generated/SettingsHelper.hpp"

#include <QJsonDocument>
#include <QTextCursor>

namespace Extensions
{

ClangFormatter::ClangFormatter(Editor::CodeEditor *editor, const QString &lang, bool selectionOnly, bool logOnNoChange,
                               MessageLogger *log, QObject *parent)
    : CodeFormatter(editor, lang, selectionOnly, logOnNoChange, log, parent)
{
}

QString ClangFormatter::settingKey() const
{
    return "Clang Format";
}

QStringList ClangFormatter::arguments() const
{
    return {"--style=file", QString("--cursor=%1").arg(anchorPos())};
}

QStringList ClangFormatter::rangeArgs() const
{
    return {QString("--offset=%1").arg(qMin(cursorPos(), anchorPos())),
            QString("--length=%1").arg(qAbs(cursorPos() - anchorPos()))};
}

QString ClangFormatter::styleFileName() const
{
    return ".clang-format";
}

QString ClangFormatter::newSource(const QString &out) const
{
    return out.mid(out.indexOf('\n') + 1);
}

QTextCursor ClangFormatter::newCursor(const QString &out, const QStringList &args) const
{
    auto cursor = editor()->textCursor();
    cursor.setPosition(newCursorPos(out));

    if (cursorPos() != anchorPos())
    {
        // change the `--cursor` argument and format again to get the position of the other end of the selection

        QStringList newArgs;

        for (const auto &arg : qAsConst(args))
            if (!arg.startsWith("--cursor"))
                newArgs.append(arg);
        newArgs.append(QString("--cursor=%1").arg(cursorPos()));

        auto res = runProcess(newArgs);
        cursor.setPosition(newCursorPos(res), QTextCursor::KeepAnchor);
    }

    return cursor;
}

int ClangFormatter::newCursorPos(const QString &out) const // NOLINT: method 'newCursorPos' can be made static
{
    auto json = QJsonDocument::fromJson(out.left(out.indexOf('\n')).toUtf8());
    return json["Cursor"].toInt();
}

} // namespace Extensions
