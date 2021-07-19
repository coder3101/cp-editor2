## 您的首次貢獻

-   [How to contribute to an open source project on GitHub](https://app.egghead.io/courses/how-to-contribute-to-an-open-source-project-on-github)
-   [自原始碼建置 CP Editor](https://cpeditor.org/docs/installation/build-from-source/)
-   [Qt 說明文件](https://doc.qt.io/)
-   [Discover issues](https://github.com/cpeditor/cpeditor/contribute)

## 在編寫程式之前

先瀏覽議題，看看是否已經有人提出相同的想法。若無，基於以下的原因，請在您著手編寫程式前，新增一個議題：


1.  您可以在過程中得到他人的協助
2.  他人可以藉此得知您正在進行的事項，避免多餘的工作
3.  藉由議題的討論，了解這功能是否會被納入使用，或者您以為的臭蟲其實是正常運作的表現……總之可以大大節省您的時間

## 貢獻守則

-   請牢記我們的目標是為競程愛好者開發好用的編輯器，而非一般開發者
-   每次拉取請求（PR）中僅做一件事。若您 PR 包含多個不相關的功能或臭蟲修補，請拆分為多個 PR。您可能需要建立多個分支（branch）以便發起多次 PR，[學習更多 git 分支的知識](https://learngitbranching.js.org/)
-   請將新功能加在 master 分支，bug 修補則在 bug 存在的最新分支（master、beta、stable）上處理。另請參閱[版號規則](VERSIONING.md)
-   請撰寫[明確的提交（commit）訊息](https://chris.beams.io/posts/git-commit/)，建議遵循 [約定式提交](https://www.conventionalcommits.org/) 規範。由於我們採用 [Squash and Merge](https://docs.github.com/en/free-pro-team@latest/github/collaborating-with-issues-and-pull-requests/about-pull-request-merges#squash-and-merge-your-pull-request-commits)，PR 的提交訊息並不是這麼重要，然而 PR 的主旨及 Squash 之後的提交訊息就變得極為關鍵
-   使用 Clang Format 及 [`.clang-format`](.clang-format) 排版程式碼。若排版的過程中發生任何問題，請試著將 `clang-format` 更新到 [最新版本](https://releases.llvm.org/download.html)
-   非必要時禁止移除任何功能，加上預設停用的開關就好。
-   保持設定檔的向下相容性。
-   為營造一個免於遭受騷擾的社群，參與者應遵守[行為準則](CODE_OF_CONDUCT.md)

## 說明文件

說明文件置放於 [cpeditor.github.io](https://github.com/cpeditor/cpeditor.github.io)。您可以參見[貢獻指引](https://github.com/cpeditor/cpeditor.github.io/blob/hugo/CONTRIBUTING.md)以了解更多資訊。

## 翻譯

翻譯檔案位於 [translations](translations) 目錄。我們推薦使用 Qt Linguist 來編輯翻譯檔案，若您只是想修正錯字，當然也可以使用平時習慣的文字編輯器。在 Qt Linguist 中，當完成一條翻譯之後，按下 <kbd>Ctrl + Enter</kbd> 以將其標示為完成，並移至下一條尚未完成的翻譯。若某些文字不需要翻譯，如「C++」您可以將翻譯的內容留空，並將其標記為翻譯完成。您應該解決 Qt Linguist 提出的所有警告。

若您想要加上新語言，可以向開發者們尋求協助，舉例來說，您可以提出議題來討論欲翻譯的語言（若您可以等候我們做好前期準備，再開始提交檔案，這會是個較好的做法）。

若您想更進一步：翻譯的字串被包在原始碼中的 `tr()`，當原始碼變更，您可以執行 [updateTranslation.sh](tools/updateTranslation.sh) 或 [updateTranslation.bat](tools/updateTranslation.bat) 來更新翻譯內容。如果您對此感興趣，可以閱讀 [Qt Linguist 使用手冊](https://doc.qt.io/qt-5/qtlinguist-index.html)。

## 設定

設定值體系的基本架構如下：

1.  關於設定值的資訊存放於 [settings.json](src/Settings/settings.json)
2.  由 [genSettings.py](src/Settings/genSettings.py) 將 `settings.json` 的資訊解譯給 `SettingsHelper.hpp` 及 `SettingsInfo.cpp` 使用
3.  設定值陳列於 [PreferencesWindow](src/Settings/PreferencesWindow.hpp) 並分類於 [PreferencesPage](src/Settings/PreferencesPage.hpp)。多數的設定會在 [PreferencesPageTemplate](src/Settings/PreferencesPageTemplate.hpp) 中顯示，後者使用 `SettingsInfo` 以產生頁面
4.  在 CP Editor 的其餘部分，您可以使用 `SettingsHelper.hpp` 或 [SettingsManager](src/Settings/SettingsManager.hpp) 中提供的函式來取得設定值。`SettingsHelper.hpp` 是開發過程中的幫手，提供了自動完成的功能，避免將設定值名稱拼錯。`SettingsManager` 通常用於設定值的名稱是變數的場合，例如：`SettingsManager::get(QString("%1/Compile Command").arg(language)).toString()`


想了解如何新增、修改設定值，您可以參考其他設定或閱讀[說明文件](src/Settings/README.md)。基本上您需要修改 [settings.json](src/Settings/settings.json) 以及 [PreferencesWindow.cpp](src/Settings/PreferencesWindow.cpp) 中以 `AddPageHelper(this)` 開頭的部分。

## 訣竅

-   即便具體實作簡短，也盡量使用現有的程式碼
-   需要使用者選擇路徑時，使用 [DefaultFileManager](src/Settings/DefaultPathManager.hpp)（除了一些例外情況，例如在偏好設定的視窗中，其路徑已存於設定值）
-   測試時記得確認 stderr 輸出流，一些 Qt 及 `qDebug()` 的警告訊息會輸出到這裡，有助於找到常見的錯誤
-   使用 `git submodule update --init --recursive` 更新子模組
