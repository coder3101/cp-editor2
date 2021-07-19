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

/* Some codes are from the examples of KDE/syntax-highlighting, which are licensed under the MIT License:

    Copyright (C) 2016 Volker Krause <vkrause@kde.org>

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef CODEEDITOR_HPP
#define CODEEDITOR_HPP

#include <QPlainTextEdit>
#include <theme.h>
#include <utility>

namespace KSyntaxHighlighting
{
class SyntaxHighlighter;
}

namespace Editor
{
class CodeEditorSidebar;

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

  public:
    enum class SeverityLevel
    {
        Hint,
        Information,
        Warning,
        Error
    };

    struct Parenthesis
    {
        QChar left, right;
        bool autoComplete, autoRemove, tabJumpOut;

        Parenthesis(const QChar &l = '(', const QChar &r = ')', bool complete = true, bool remove = true,
                    bool jumpout = true)
            : left(l), right(r), autoComplete(complete), autoRemove(remove), tabJumpOut(jumpout)
        {
        }
    };

    /**
     * @brief Constructor.
     * @param widget Pointer to parent widget.
     */
    explicit CodeEditor(QWidget *widget = nullptr);

    /**
     * @brief Method for getting first visible block
     * index.
     * @return Index.
     */
    int getFirstVisibleBlock();

    /**
     * @brief squiggle Puts a underline squiggle under text ranges in Editor
     * @param level defines the color of the underline depending upon the severity
     * @param tooltipMessage The tooltip hover message to show when over selection.
     * @note QPair<int, int>: first -> Line number in 1-based indexing
     *                        second -> Character number in 0-based indexing
     */
    void squiggle(SeverityLevel level, QPair<int, int>, QPair<int, int>, QString tooltipMessage);

    /**
     * @brief clearSquiggle, Clears complete squiggle from editor
     */
    void clearSquiggle();

    /**
     * @brief Enables or disables Vim Like cursor
     */
    void setVimCursor(bool value);

    /**
     * @brief Checks if cursor type is Vim Cursor
     */
    bool vimCursor() const;

    /**
     * @brief Enables or disables current line highlighting
     * @note In vim mode this cannot enable line highlighting
     */
    void setHighlightCurrentLine(bool enabled);

    /**
     * @brief Checks if current line is being higlighted in non vim mode
     */
    bool isHighlightingCurrentLine() const;

    void applySettings(const QString &lang);

  signals:
    /**
     * @brief Signal, the font is changed by the wheel event.
     */
    void fontChanged(const QFont &newFont);

  public slots:
    /**
     * @brief Slot, that indent the selected lines.
     */
    void indent();

    /**
     * @brief Slot, that unindent the selected lines.
     */
    void unindent();

    /**
     * @brief Slot, that swap the selected lines up.
     */
    void swapLineUp();

    /**
     * @brief Slot, that swap the selected lines down.
     */
    void swapLineDown();

    /**
     * @brief Slot, that delete the selected lines.
     */
    void deleteLine();

    /**
     * @brief Slot, that duplicate the current line or the selection.
     */
    void duplicate();

    /**
     * @brief Slot, that toggle single line comment of the
     * selected lines.
     */
    void toggleComment();

    /**
     * @brief Slot, that toggle block comment of the selection.
     */
    void toggleBlockComment();

  protected:
    /**
     * @brief Method, that's called on any text insertion of
     * mimedata into editor. If it's text - it inserts text
     * as plain text.
     */
    void insertFromMimeData(const QMimeData *source) override;

    /**
     * @brief Method, that's called on any widget resize.
     * This method if overloaded for line number area
     * resizing.
     */
    void resizeEvent(QResizeEvent *e) override;

    /**
     * @brief Method, update the bottom margin when the font changes.
     */
    void changeEvent(QEvent *e) override;

    /**
     * @brief Method, update the font size when the wheel is rotated with Ctrl pressed
     */
    void wheelEvent(QWheelEvent *e) override;

    /**
     * @brief Method, that's called on any key press, posted
     * into code editor widget. This method is overloaded for:
     *
     * 1. Completion
     * 2. Tab to spaces
     * 3. Low indentation
     * 4. Auto parenthesis
     */
    void keyPressEvent(QKeyEvent *e) override;

    /**
     * @brief Method, that's called on focus loss
     * It's required for setting block cursor
     * in fakevim mode.
     */
    void focusOutEvent(QFocusEvent *e) override;

    void paintEvent(QPaintEvent *e) override;

    /**
     * @brief Method for tooltip generation
     */
    bool event(QEvent *e) override;

    int sidebarWidth() const;

    void sidebarPaintEvent(QPaintEvent *event);

    void updateSidebarGeometry();

    void updateSidebarArea(const QRect &rect, int dy);

    QTextBlock blockAtPosition(int y) const;

    bool isFoldable(const QTextBlock &block) const;

    static bool isFolded(const QTextBlock &block);

    void toggleFold(const QTextBlock &block);

  private slots:
    /**
     * @brief Slot, that updates the bottom margin.
     */
    void updateBottomMargin();

    void highlightParentheses();

    void highlightOccurrences();

    void highlightCurrentLine();

  private:
    /**
     * @brief Method for getting character under
     * cursor.
     * @param offset Offset to cursor.
     */
    QChar charUnderCursor(int offset = 0) const;

    /**
     * @brief Method to check if character at given position
     * is inside a block comment. Return false if language does
     * not have notion of block comments.
     * @param the position to check
     */
    bool isPositionInsideBlockComments(int position) const;

    /**
     * @brief Method to check if character at given position
     * is inside a line comment.
     * @param the position to check
     */

    bool isPositionInsideLineComments(int position) const;

    /**
     * @brief Method to check if character at given position
     * is inside a single quote.
     * @param the position to check
     */
    bool isPositionInsideSingleQuotes(int position) const;

    /**
     * @brief Method to check if character at given position
     * is inside a Double quote.
     * @param the position to check
     */

    bool isPositionInsideDoubleQuotes(int position) const;

    /**
     * @brief Method to check if character at given position
     * is inside a Raw String or String literals quote.
     * @param the position to check
     */

    bool isPositionPartOfRawOrStringLiteral(int position) const;

    /**
     * @brief Method for remove the first group of regex
     * in each line of the selection.
     * @param regex remove its first group
     * @param force if true, remove regardless of whether
     * all lines are begun with regex; if false remove
     * only when all lines are begun with regex.
     * @return if regex is removed
     */
    bool removeInEachLineOfSelection(const QRegularExpression &regex, bool force);

    /**
     * @brief Method for add the str at the begin of regex
     * in each line of the selection.
     * @param regex add at the begin of its match
     * @param str string to add
     */
    void addInEachLineOfSelection(const QRegularExpression &regex, const QString &str);

    /**
     * @brief Set the theme of the Code Editor from KSybtaxHighlighting::Theme
     */
    void setTheme(const KSyntaxHighlighting::Theme &newTheme);

    void updateExtraSelections();

    /**
     * @brief The SquiggleInformation struct, Line number will be index of vector+1;
     */
    struct SquiggleInformation
    {
        SquiggleInformation() = default;

        SquiggleInformation(QPair<int, int> start, QPair<int, int> stop, QString text)
            : m_startPos(start), m_stopPos(stop), m_tooltipText(std::move(text))
        {
        }

        QPair<int, int> m_startPos;
        QPair<int, int> m_stopPos;
        QString m_tooltipText;
    };

    QList<QTextEdit::ExtraSelection> currentLineExtraSelections, parenthesesExtraSelections, occurrencesExtraSelections,
        squigglesExtraSelections;

    QString m_tabReplace;

    bool m_vimCursor = false;

    bool m_highlightingCurrentLine = true;

    QRect m_cursorRect;

    QVector<SquiggleInformation> squiggles;

    QVector<Parenthesis> parentheses;

    KSyntaxHighlighting::SyntaxHighlighter *highlighter = nullptr;

    KSyntaxHighlighting::Theme theme;

    CodeEditorSidebar *sideBar = nullptr;

    QString language;

    friend class CodeEditorSidebar;
};
} // namespace Editor

#endif // CODEEDITOR_HPP
