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

/*
 * The Formatter is used to format codes.
 * It runs synchronously, so the event loop is blocked while formatting.
 * The time limit for formatting is 2 seconds.
 */

#ifndef FORMATTER_HPP
#define FORMATTER_HPP

#include "Extensions/CodeFormatter.hpp"

namespace Extensions
{
class ClangFormatter : public CodeFormatter
{
    Q_OBJECT

  public:
    explicit ClangFormatter(Editor::CodeEditor *editor, const QString &lang, bool selectionOnly, bool logOnNoChange,
                            MessageLogger *log, QObject *parent = nullptr);

  protected:
    QString settingKey() const override;

    QStringList arguments() const override;

    QStringList rangeArgs() const override;

    QString styleFileName() const override;

    QString newSource(const QString &out) const override;

    QTextCursor newCursor(const QString &out, const QStringList &args) const override;

  private:
    int newCursorPos(const QString &out) const;
};

} // namespace Extensions

#endif // FORMATTER_HPP
