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

#ifndef CODEFORMATTER_HPP
#define CODEFORMATTER_HPP

#include <QObject>

namespace Editor
{
class CodeEditor;
}
class MessageLogger;
class QTextCursor;

namespace Extensions
{
class CodeFormatter : public QObject
{
    Q_OBJECT

  public:
    explicit CodeFormatter(Editor::CodeEditor *editor, const QString &lang, bool selectionOnly, bool logOnNoChange,
                           MessageLogger *log, QObject *parent = nullptr);

    void format() const;

  protected:
    /**
     * @brief the key of the formatter in the settings, e.g. Clang Format
     * @note there should be these settings: settingKey()/Program, settingKey()/Arguments, settingKey()/Style
     */
    virtual QString settingKey() const = 0;

    /**
     * @brief the arguments of the formatter, except args for formatting a range and args set by the user
     */
    virtual QStringList arguments() const = 0;

    /**
     * @brief the arguments used to format selection only
     * @note you can use cursorPos, cursorLine, cursorCol, anchorPos, anchorLine, anchorCol in this function
     */
    virtual QStringList rangeArgs() const = 0;

    /**
     * @brief the name of the style file, e.g. .clang-format
     */
    virtual QString styleFileName() const = 0;

    /**
     * @brief the new source code after formatting
     * @param out the stdout of the format process
     */
    virtual QString newSource(const QString &out) const = 0;

    /**
     * @brief the new text cursor after formatting
     * @param out the stdout of the format process
     * @param args the arguments used when formatting
     */
    virtual QTextCursor newCursor(const QString &out, const QStringList &args) const = 0;

    /**
     * @brief run the format process and get the result
     * @param args the arguments of the format process
     * @returns the stdout of the format process, or an empty QString if error occurred
     */
    QString runProcess(const QStringList &args) const;

    /**
     * @brief check whether only the selection is formatted
     */
    bool formatSelectionOnly() const;

  private:
    /**
     * @brief get settingKey()/key
     */
    QVariant getSetting(const QString &key) const;

  private:
    Editor::CodeEditor *m_editor;
    QString m_lang;
    bool m_selectionOnly;
    bool m_logOnNoChange;
    int m_cursorPos, m_cursorLine, m_cursorCol, m_anchorPos, m_anchorLine, m_anchorCol;

  protected:
    Editor::CodeEditor *editor() const
    {
        return m_editor;
    }
    QString lang() const
    {
        return m_lang;
    }
    bool selectionOnly() const
    {
        return m_selectionOnly;
    }
    bool logOnNoChange() const
    {
        return m_logOnNoChange;
    }
    int cursorPos() const
    {
        return m_cursorPos;
    }
    int cursorLine() const
    {
        return m_cursorLine;
    }
    int cursorCol() const
    {
        return m_cursorCol;
    }
    int anchorPos() const
    {
        return m_anchorPos;
    }
    int anchorLine() const
    {
        return m_anchorLine;
    }
    int anchorCol() const
    {
        return m_anchorCol;
    }

  private:
    MessageLogger *log = nullptr;
};
} // namespace Extensions

#endif // CODEFORMATTER_HPP
