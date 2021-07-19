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

/* Most of the code in this file is under the following copyright

    Copyright (c) 2017, Lukas Holecek <hluk@email.cz>
    This file is part of CopyQ.
    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Core/FakeVimProxy.hpp"
#include "Core/EventLogger.hpp"
#include "Editor/CodeEditor.hpp"
#include "appwindow.hpp"
#include "generated/SettingsHelper.hpp"
#include "mainwindow.hpp"
#include "third_party/FakeVim/fakevim/fakevimactions.h"
#include "third_party/FakeVim/fakevim/fakevimhandler.h"
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTextBlock>

namespace Core
{

FakeVimProxy::FakeVimProxy(QWidget *widget, MainWindow *mw, AppWindow *aw, QObject *parent)
    : QObject(parent), m_widget(widget), m_mainWindow(mw), m_appWindow(aw),
      m_commandHandler(new FakeVimCommand(aw, parent))
{
    m_statusData = new QLabel(m_mainWindow);
    m_statusMessage = new QLabel(m_mainWindow);
    setStatusBar();
}

FakeVimProxy::~FakeVimProxy()
{
    delete m_commandHandler;
}

void FakeVimProxy::changeStatusData(QString const &info)
{
    m_statusData->setText(info);
}

void FakeVimProxy::highlightMatches(QString const &pattern)
{
    QTextDocument *doc = nullptr;

    { // in a block so we don't inadvertently use one of them later
        auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
        auto *editor = qobject_cast<QTextEdit *>(m_widget);
        if (editor)
        {
            doc = editor->document();
        }
        else if (plainEditor)
        {
            doc = plainEditor->document();
        }
        else
        {
            return;
        }
    }
    Q_ASSERT(doc);

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(Qt::yellow);
    selection.format.setForeground(Qt::black);

    // Highlight matches.
    QRegExp re(pattern);
    QTextCursor cur = doc->find(re);

    m_searchSelection.clear();

    int a = cur.position();
    while (!cur.isNull())
    {
        if (cur.hasSelection())
        {
            selection.cursor = cur;
            m_searchSelection.append(selection);
        }
        else
        {
            cur.movePosition(QTextCursor::NextCharacter);
        }
        cur = doc->find(re, cur);
        int b = cur.position();
        if (a == b)
        {
            cur.movePosition(QTextCursor::NextCharacter);
            cur = doc->find(re, cur);
            b = cur.position();
            if (a == b)
                break;
        }
        a = b;
    }

    updateExtraSelections();
}

void FakeVimProxy::changeStatusMessage(QString const &contents, int cursorPos)
{
    m_statusMessage->setText(cursorPos == -1 ? contents
                                             : contents.left(cursorPos) + QChar(10073) + contents.mid(cursorPos));
}

void FakeVimProxy::setStatusBar()
{
    m_mainWindow->statusBar()->addPermanentWidget(m_statusData);
    m_mainWindow->statusBar()->addWidget(m_statusMessage);
    m_mainWindow->statusBar()->setFont(SettingsHelper::getEditorFont());
}

void FakeVimProxy::handleExCommand(bool *handled, FakeVim::Internal::ExCommand const &cmd)
{
    auto customCommandType = FakeVimCommand::customCommandType(cmd);
    if (customCommandType != FakeVimCommand::CommandTypes::Unknown)
    {
        *handled = m_commandHandler->handleCustomCommand(customCommandType, cmd.args, cmd.hasBang);
        return;
    }
    if (wantSaveAndQuit(cmd))
    {
        // :wq
        if (save())
            quit();
    }
    else if (wantSave(cmd))
    {
        save(); // :w
    }
    else if (wantQuit(cmd))
    {
        if (cmd.hasBang)
            forceQuit(); // :q!
        else
            quit(); // :q
    }
    else
    {
        *handled = false;
        return;
    }

    *handled = true;
}

void FakeVimProxy::requestSetBlockSelection(const QTextCursor &tc)
{
    auto *editor = qobject_cast<QTextEdit *>(m_widget);
    auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
    if (!editor && !plainEditor)
    {
        return;
    }

    QPalette pal = m_widget->parentWidget() != nullptr ? m_widget->parentWidget()->palette() : QApplication::palette();

    m_blockSelection.clear();
    m_clearSelection.clear();

    QTextCursor cur = tc;

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(pal.color(QPalette::Base));
    selection.format.setForeground(pal.color(QPalette::Text));
    selection.cursor = cur;
    m_clearSelection.append(selection);

    selection.format.setBackground(pal.color(QPalette::Highlight));
    selection.format.setForeground(pal.color(QPalette::HighlightedText));

    int from = cur.positionInBlock();
    int to = cur.anchor() - cur.document()->findBlock(cur.anchor()).position();
    const int min = qMin(cur.position(), cur.anchor());
    const int max = qMax(cur.position(), cur.anchor());
    for (QTextBlock block = cur.document()->findBlock(min); block.isValid() && block.position() < max;
         block = block.next())
    {
        cur.setPosition(block.position() + qMin(from, block.length()));
        cur.setPosition(block.position() + qMin(to, block.length()), QTextCursor::KeepAnchor);
        selection.cursor = cur;
        m_blockSelection.append(selection);
    }

    if (editor)
    {
        disconnect(editor, &QTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
        editor->setTextCursor(tc);
        connect(editor, &QTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
    }
    else
    {
        disconnect(plainEditor, &QPlainTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
        plainEditor->setTextCursor(tc);
        connect(plainEditor, &QPlainTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
    }

    QPalette pal2 = m_widget->palette();
    pal2.setColor(QPalette::Highlight, Qt::transparent);
    pal2.setColor(QPalette::HighlightedText, Qt::transparent);
    m_widget->setPalette(pal2);

    updateExtraSelections();
}

void FakeVimProxy::requestDisableBlockSelection()
{
    auto *editor = qobject_cast<QTextEdit *>(m_widget);
    auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
    if (!editor && !plainEditor)
    {
        return;
    }

    QPalette pal = m_widget->parentWidget() != nullptr ? m_widget->parentWidget()->palette() : QApplication::palette();

    m_blockSelection.clear();
    m_clearSelection.clear();

    m_widget->setPalette(pal);

    if (editor)
    {
        disconnect(editor, &QTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
    }
    else
    {
        disconnect(plainEditor, &QPlainTextEdit::selectionChanged, this, &FakeVimProxy::updateBlockSelection);
    }

    updateExtraSelections();
}

void FakeVimProxy::updateBlockSelection()
{
    auto *editor = qobject_cast<QTextEdit *>(m_widget);
    auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
    if (!editor && !plainEditor)
    {
        return;
    }

    requestSetBlockSelection(editor ? editor->textCursor() : plainEditor->textCursor());
}

void FakeVimProxy::requestHasBlockSelection(bool *on)
{
    *on = !m_blockSelection.isEmpty();
}

void FakeVimProxy::indentRegion(int beginBlock, int endBlock, QChar typedChar)
{
    QTextDocument *doc = nullptr;
    { // in a block so we don't inadvertently use one of them later
        auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
        auto *editor = qobject_cast<QTextEdit *>(m_widget);
        if (editor)
        {
            doc = editor->document();
        }
        else if (plainEditor)
        {
            doc = plainEditor->document();
        }
        else
        {
            return;
        }
    }
    Q_ASSERT(doc);

    const int indentSize = FakeVim::Internal::theFakeVimSetting(FakeVim::Internal::ConfigShiftWidth)->value().toInt();

    QTextBlock startBlock = doc->findBlockByNumber(beginBlock);

    // Record line lengths for mark adjustments
    QVector<int> lineLengths(endBlock - beginBlock + 1);
    QTextBlock block = startBlock;

    for (int i = beginBlock; i <= endBlock; ++i)
    {
        const auto line = block.text();
        lineLengths[i - beginBlock] = line.length();
        if (typedChar.unicode() == 0 && line.simplified().isEmpty())
        {
            // clear empty lines
            QTextCursor cursor(block);
            while (!cursor.atBlockEnd())
                cursor.deleteChar();
        }
        else
        {
            const auto previousBlock = block.previous();
            const auto previousLine = previousBlock.isValid() ? previousBlock.text() : QString();

            int indent = firstNonSpace(previousLine);
            if (typedChar == '}')
                indent = std::max(0, indent - indentSize);
            else if (previousLine.endsWith("{"))
                indent += indentSize;
            const auto indentString = QString(" ").repeated(indent);

            QTextCursor cursor(block);
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, firstNonSpace(line));
            cursor.removeSelectedText();
            cursor.insertText(indentString);
            cursor.endEditBlock();
        }
        block = block.next();
    }
}

void FakeVimProxy::moveToMatchingParenthesis(bool *moved, bool *forward, QTextCursor *cursor)
{
    auto isClosingParenthesis = [this](QChar symbol) {
        return std::any_of(parenthesisList.begin(), parenthesisList.end(),
                           [&](auto const &e) { return symbol == e.second; });
    };

    QChar underCursor = charUnderCursor(cursor, 0);
    QChar counterSymbol = getCounterParenthesis(underCursor);
    if (counterSymbol == QChar())
        return;

    int direction = -1;
    int position = cursor->position();
    if (isClosingParenthesis(counterSymbol)) // if counter symbol is closing, search for it in forward direction
    {
        direction = 1;
    }

    int counter = 1;
    int singleCounter = 0;
    int doubleCounter = 0;

    while (counter != 0 && position > 0 && position < (document()->characterCount() - 1))
    {
        position += direction;
        auto character = document()->characterAt(position);
        if (character == "\"")
            doubleCounter++;
        else if (character == "'")
            singleCounter++;
        else if (character == underCursor && singleCounter % 2 == 0 && doubleCounter % 2 == 0)
            ++counter;
        else if (character == counterSymbol && singleCounter % 2 == 0 && doubleCounter % 2 == 0)
            --counter;
    }

    // position stopped at matching parenthesis
    if (!counter)
    {
        *moved = true;
        cursor->setPosition(position);
    }
}

void FakeVimProxy::checkForElectricCharacter(bool *result, QChar c)
{
    *result = c == '{' || c == '}';
}

QChar FakeVimProxy::charUnderCursor(QTextCursor *cursor, int offset)
{

    QChar underCursor;
    auto text = document()->findBlockByNumber(cursor->blockNumber()).text();
    int index = cursor->positionInBlock();
    index += offset;
    if (index < text.size() && index >= 0)
        underCursor = text[index];
    return underCursor;
}

QChar FakeVimProxy::getCounterParenthesis(QChar symbol)
{
    for (auto const &e : parenthesisList)
    {
        if (symbol == e.first)
            return e.second;
        if (symbol == e.second)
            return e.first;
    }
    return {};
}

int FakeVimProxy::firstNonSpace(const QString &text)
{
    int indent = 0;
    while (indent < text.length() && text.at(indent) == ' ')
        ++indent;
    return indent;
}

void FakeVimProxy::updateExtraSelections()
{
    auto *editor = qobject_cast<QTextEdit *>(m_widget);
    auto *plainEditor = qobject_cast<QPlainTextEdit *>(m_widget);
    if (editor)
    {
        editor->setExtraSelections(m_clearSelection + m_searchSelection + m_blockSelection);
    }
    else if (plainEditor)
    {
        plainEditor->setExtraSelections(m_clearSelection + m_searchSelection + m_blockSelection);
    }
}

bool FakeVimProxy::wantSaveAndQuit(const FakeVim::Internal::ExCommand &cmd)
{
    return cmd.cmd == "wq";
}

bool FakeVimProxy::wantSave(const FakeVim::Internal::ExCommand &cmd)
{
    return cmd.matches("w", "write") || cmd.matches("wa", "wall");
}

bool FakeVimProxy::wantQuit(const FakeVim::Internal::ExCommand &cmd)
{
    return cmd.matches("q", "quit") || cmd.matches("qa", "qall");
}

bool FakeVimProxy::save()
{
    return m_mainWindow->save(true, "Vim Save");
}

void FakeVimProxy::quit()
{
    m_appWindow->closeWindow(m_mainWindow);
}

void FakeVimProxy::forceQuit()
{
    m_appWindow->closeWindow(m_mainWindow, true);
}

bool FakeVimProxy::hasChanges()
{
    return m_mainWindow->isTextChanged();
}

QTextDocument *FakeVimProxy::document() const
{
    QTextDocument *doc = nullptr;
    if (auto *ed = qobject_cast<QPlainTextEdit *>(m_widget))
        doc = ed->document();
    else if (auto *ed = qobject_cast<QTextEdit *>(m_widget))
        doc = ed->document();
    return doc;
}

QString FakeVimProxy::content() const
{
    return document()->toPlainText();
}

void FakeVimProxy::initHandler(FakeVim::Internal::FakeVimHandler *handler)
{
    handler->handleCommand(QLatin1String("set nopasskeys"));
    handler->handleCommand(QLatin1String("set nopasscontrolkey"));
    handler->installEventFilter();
    handler->setupWidget();
}

void FakeVimProxy::sourceVimRc(FakeVim::Internal::FakeVimHandler *handler)
{
    QTemporaryFile file;
    if (file.open())
    {
        file.write(SettingsHelper::getFakeVimRC().toLocal8Bit());
        file.close();
        handler->handleCommand("source " + file.fileName());
    }
    else
    {
        LOG_WARN("Failed to open a temporary file to source vimrc.");
        handler->handleCommand(QLatin1String("set noexpandtab"));
        handler->handleCommand(QLatin1String("set shiftwidth=4"));
        handler->handleCommand(QLatin1String("set tabstop=4"));
        handler->handleCommand(QLatin1String("set autoindent"));
        handler->handleCommand(QLatin1String("set smartindent"));
    }
}

void FakeVimProxy::clearUndoRedo(QWidget *editor)
{
    if (auto *ed = qobject_cast<QPlainTextEdit *>(editor))
    {
        ed->setUndoRedoEnabled(false);
        ed->setUndoRedoEnabled(true);
    }
    else if (auto *ed = qobject_cast<QTextEdit *>(editor))
    {
        ed->setUndoRedoEnabled(false);
        ed->setUndoRedoEnabled(true);
    }
}

void FakeVimProxy::connectSignals(FakeVim::Internal::FakeVimHandler *handler, QWidget *editor, MainWindow *mainWindow,
                                  AppWindow *appWindow)
{
    auto *proxy = new FakeVimProxy(editor, mainWindow, appWindow, handler);

    handler->commandBufferChanged.connect(
        [proxy](const QString &contents, int cursorPos, int /*anchorPos*/, int /*messageLevel*/) {
            proxy->changeStatusMessage(contents, cursorPos);
        });
    handler->statusDataChanged.connect([proxy](const QString &text) { proxy->changeStatusData(text); });
    handler->highlightMatches.connect([proxy](const QString &needle) { proxy->highlightMatches(needle); });
    handler->handleExCommandRequested.connect(
        [proxy](bool *handled, const FakeVim::Internal::ExCommand &cmd) { proxy->handleExCommand(handled, cmd); });
    handler->requestSetBlockSelection.connect(
        [proxy](const QTextCursor &cursor) { proxy->requestSetBlockSelection(cursor); });
    handler->requestDisableBlockSelection.connect([proxy] { proxy->requestDisableBlockSelection(); });
    handler->requestHasBlockSelection.connect([proxy](bool *on) { proxy->requestHasBlockSelection(on); });

    handler->indentRegion.connect([proxy](int beginBlock, int endBlock, QChar typedChar) {
        proxy->indentRegion(beginBlock, endBlock, typedChar);
    });
    handler->checkForElectricCharacter.connect(
        [proxy](bool *result, QChar c) { proxy->checkForElectricCharacter(result, c); });

    handler->moveToMatchingParenthesis.connect([proxy](bool *moved, bool *forward, QTextCursor *cursor) {
        proxy->moveToMatchingParenthesis(moved, forward, cursor);
    });

    handler->tabNextRequested.connect([appWindow, mainWindow] {
        auto total = appWindow->tabCount();
        auto curr = appWindow->indexOfWindow(mainWindow);
        int next = (curr + 1) % total;
        if (next != curr)
            appWindow->setTabAt(next);
    });
    handler->tabPreviousRequested.connect([appWindow, mainWindow] {
        auto total = appWindow->tabCount();
        auto curr = appWindow->indexOfWindow(mainWindow);
        int last = curr ? curr - 1 : total - 1;
        if (last != curr)
            appWindow->setTabAt(last);
    });
    QObject::connect(proxy, &FakeVimProxy::handleInput, handler,
                     [handler](const QString &text) { handler->handleInput(text); });
}

} // namespace Core
