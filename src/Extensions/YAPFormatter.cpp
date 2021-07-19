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

#include "Extensions/YAPFormatter.hpp"
#include "Editor/CodeEditor.hpp"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace Extensions
{

YAPFormatter::YAPFormatter(Editor::CodeEditor *editor, const QString &lang, bool selectionOnly, bool logOnNoChange,
                           MessageLogger *log, QObject *parent)
    : CodeFormatter(editor, lang, selectionOnly, logOnNoChange, log, parent)
{
}

QString YAPFormatter::settingKey() const
{
    return "YAPF";
}

QStringList YAPFormatter::arguments() const
{
    return {};
}

QStringList YAPFormatter::rangeArgs() const
{
    return {QString("-l %1-%2").arg(qMin(cursorLine(), anchorLine()) + 1).arg(qMax(cursorLine(), anchorLine()) + 1)};
}

QString YAPFormatter::styleFileName() const
{
    return ".style.yapf";
}

QString YAPFormatter::newSource(const QString &out) const
{
    return out;
}

QTextCursor YAPFormatter::newCursor(const QString &out, const QStringList & /*args*/) const
{
    auto cursor = editor()->textCursor();

    auto setCol = [this, &cursor](QTextCursor::MoveMode mode, int col) {
        cursor.movePosition(QTextCursor::NextCharacter, mode,
                            qMin(col, editor()->document()->findBlockByNumber(cursor.blockNumber()).length() - 1));
    };

    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, anchorLine());
    setCol(QTextCursor::MoveAnchor, anchorCol());

    if (cursorPos() == anchorPos())
        return cursor;

    if (anchorLine() <= cursorLine())
    {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, cursorLine() - anchorLine());
    }
    else
    {
        cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor, anchorLine() - cursorLine());
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    }

    setCol(QTextCursor::KeepAnchor, cursorCol());

    return cursor;
}

} // namespace Extensions
