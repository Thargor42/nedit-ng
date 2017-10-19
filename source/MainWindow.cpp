
#include "MainWindow.h"
#include "DialogAbout.h"
#include "DialogColors.h"
#include "DialogExecuteCommand.h"
#include "DialogFilter.h"
#include "DialogFind.h"
#include "DialogFonts.h"
#include "DialogLanguageModes.h"
#include "DialogMacros.h"
#include "DialogRepeat.h"
#include "DialogReplace.h"
#include "DialogShellMenu.h"
#include "DialogSmartIndent.h"
#include "DialogTabs.h"
#include "Help.h"
#include "DialogWindowBackgroundMenu.h"
#include "DialogWindowSize.h"
#include "DialogWindowTitle.h"
#include "DialogWrapMargin.h"
#include "DocumentWidget.h"
#include "LanguageMode.h"
#include "MenuItem.h"
#include "Settings.h"
#include "SignalBlocker.h"
#include "TextArea.h"
#include "TextBuffer.h"
#include "util/ClearCase.h"
#include "highlight.h"
#include "highlightData.h"
#include "macro.h"
#include "memory.h"
#include "nedit.h"
#include "preferences.h"
#include "regularExp.h"
#include "search.h"
#include "selection.h"
#include "shift.h"
#include "tags.h"
#include "util/fileUtils.h"
#include "utils.h"
#include "CommandRecorder.h"
#include "WindowMenuEvent.h"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QShortcut>
#include <QToolButton>
#include <QRegExp>
#include <QtDebug>
#include <QScrollBar>

#include <cmath>
#include <memory>
#include <fstream>
#include <iostream>
#include <string>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(DONT_HAVE_GLOB)
#include <glob.h>
#endif

#define EMIT_EVENT(name)                                \
    do {                                                \
        WindowMenuEvent menuEvent(QLatin1String(name)); \
        QApplication::sendEvent(this, &menuEvent);      \
    } while(0)

#define EMIT_EVENT_ARG(name, arg)                            \
    do {                                                     \
        WindowMenuEvent menuEvent(QLatin1String(name), arg); \
        QApplication::sendEvent(this, &menuEvent);           \
    } while(0)

namespace {

bool currentlyBusy = false;
long busyStartTime = 0;
bool modeMessageSet = false;

QPointer<DialogShellMenu>            WindowShellMenu;
QPointer<DialogWindowBackgroundMenu> WindowBackgroundMenu;
QPointer<DialogMacros>               WindowMacros;
QPointer<DialogSmartIndent>          SmartIndentDlg;
QPointer<DocumentWidget>             lastFocusDocument;

auto neditDBBadFilenameChars = QLatin1String("\n");

QList<QString> PrevOpen;

/*
 * Auxiliary function for measuring elapsed time during busy waits.
 */
long getRelTimeInTenthsOfSeconds() {
    struct timeval current;
    gettimeofday(&current, nullptr);
    return (current.tv_sec * 10 + current.tv_usec / 100000) & 0xFFFFFFFL;
}

}

//------------------------------------------------------------------------------
// Name: MainWindow
//------------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags flags) : QMainWindow(parent, flags) {
	ui.setupUi(this);

    connect(qApp, SIGNAL(focusChanged(QWidget *, QWidget *)), this, SLOT(focusChanged(QWidget *, QWidget *)));
	
	setupMenuGroups();
	setupTabBar();
	setupMenuStrings();
	setupMenuAlternativeMenus();
    CreateLanguageModeSubMenu();
    setupMenuDefaults();

    setupPrevOpenMenuActions();
    updatePrevOpenMenu();

    showISearchLine_       = GetPrefISearchLine();
    iSearchHistIndex_      = 0;
    iSearchStartPos_       = -1;
    iSearchLastRegexCase_  = true;
    iSearchLastLiteralCase_= false;
    wasSelected_           = false;
    showLineNumbers_       = GetPrefLineNums();
	
	// default to hiding the optional panels
    ui.incrementalSearchFrame->setVisible(showISearchLine_);

    ui.action_Statistics_Line->setChecked(GetPrefStatsLine());

    CheckCloseDimEx();
}

//------------------------------------------------------------------------------
// Name: setDimmensions
//------------------------------------------------------------------------------
void MainWindow::parseGeometry(QString geometry) {
    int rows = -1;
    int cols = -1;

    /* If window geometry was specified, split it apart into a window position
       component and a window size component.  Create a new geometry string
       containing the position component only.  Rows and cols are stripped off
       because we can't easily calculate the size in pixels from them until the
       whole window is put together.  Note that the preference resource is only
       for clueless users who decide to specify the standard X geometry
       application resource, which is pretty useless because width and height
       are the same as the rows and cols preferences, and specifying a window
       location will force all the windows to pile on top of one another */
    if (geometry.isEmpty()) {
        geometry = GetPrefGeometry();
    }

    if (geometry.isEmpty()) {
        rows = GetPrefRows();
        cols = GetPrefCols();
    } else {

        QRegExp regex(QLatin1String("(?:([0-9]+)(?:[xX]([0-9]+)(?:([\\+-][0-9]+)(?:([\\+-][0-9]+))?)?)?)?"));
        if(regex.exactMatch(geometry)) {
            cols = regex.capturedTexts()[1].toInt();
            rows = regex.capturedTexts()[2].toInt();
        }
    }

    if(DocumentWidget *document = currentDocument()) {
        QFontMetrics fm(document->fontStruct_);
		constexpr int margin = 5;
		int w = (fm.maxWidth() * cols) + (margin * 2);
		int h = (fm.ascent() + fm.descent()) * rows + (margin * 2);

		// NOTE(eteran): why 17? your guess is as good as mine
		// but getting the width/height from the actual scrollbars
		// yielded nonsense on my system (like 100px). Maybe it's a bug
		// involving HiDPI screens? I dunno
		w += 17; // area->verticalScrollBar()->width();
		h += 17; // area->horizontalScrollBar()->height();

		document->setMinimumSize(w, h);
		adjustSize();

        // NOTE(eteran): this processEvents() is to force the resize
		// to happen now, so that we can set the widget to be fully
		// resizable again right after we make everything adjust
		QApplication::processEvents();
		document->setMinimumSize(0, 0);
    }
}

//------------------------------------------------------------------------------
// Name: setupTabBar
//------------------------------------------------------------------------------
void MainWindow::setupTabBar() {
	// create and hook up the tab close button
	auto deleteTabButton = new QToolButton(ui.tabWidget);
	deleteTabButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
	deleteTabButton->setIcon(QIcon::fromTheme(tr("tab-close")));
	deleteTabButton->setAutoRaise(true);
    deleteTabButton->setFocusPolicy(Qt::NoFocus);
    deleteTabButton->setObjectName(tr("tab-close"));
	ui.tabWidget->setCornerWidget(deleteTabButton);
	
	connect(deleteTabButton, SIGNAL(clicked()), this, SLOT(deleteTabButtonClicked()));

    ui.tabWidget->tabBar()->installEventFilter(this);
}

//------------------------------------------------------------------------------
// Name: setupMenuDefaults
//------------------------------------------------------------------------------
void MainWindow::setupMenuDefaults() {

    // active settings
    no_signals(ui.action_Statistics_Line)->setChecked(GetPrefStatsLine());
    no_signals(ui.action_Incremental_Search_Line)->setChecked(GetPrefISearchLine());
    no_signals(ui.action_Show_Line_Numbers)->setChecked(GetPrefLineNums());
    no_signals(ui.action_Highlight_Syntax)->setChecked(GetPrefHighlightSyntax());
    no_signals(ui.action_Apply_Backlighting)->setChecked(GetPrefBacklightChars());
    no_signals(ui.action_Make_Backup_Copy)->setChecked(GetPrefAutoSave());
    no_signals(ui.action_Incremental_Backup)->setChecked(GetPrefSaveOldVersion());
    no_signals(ui.action_Matching_Syntax)->setChecked(GetPrefMatchSyntaxBased());


    // based on document, which defaults to this
    switch(GetPrefAutoIndent(PLAIN_LANGUAGE_MODE)) {
    case NO_AUTO_INDENT:
        no_signals(ui.action_Indent_Off)->setChecked(true);
        break;
    case AUTO_INDENT:
        no_signals(ui.action_Indent_On)->setChecked(true);
        break;
    case SMART_INDENT:
        no_signals(ui.action_Indent_Smart)->setChecked(true);
        break;
    default:
        break;
    }

    // based on document, which defaults to this
    switch(GetPrefWrap(PLAIN_LANGUAGE_MODE)) {
    case NO_WRAP:
        no_signals(ui.action_Wrap_None)->setChecked(true);
        break;
    case NEWLINE_WRAP:
        no_signals(ui.action_Wrap_Auto_Newline)->setChecked(true);
        break;
    case CONTINUOUS_WRAP:
        no_signals(ui.action_Wrap_Continuous)->setChecked(true);
        break;
    default:
        break;
    }

    switch(static_cast<ShowMatchingStyle>(GetPrefShowMatching())) {
    case NO_FLASH:
        no_signals(ui.action_Matching_Off)->setChecked(true);
        break;
    case FLASH_DELIMIT:
        no_signals(ui.action_Matching_Delimiter)->setChecked(true);
        break;
    case FLASH_RANGE:
        no_signals(ui.action_Matching_Range)->setChecked(true);
        break;
    default:
        break;
    }

    // Default Indent
    switch(GetPrefAutoIndent(PLAIN_LANGUAGE_MODE)) {
    case NO_AUTO_INDENT:
        no_signals(ui.action_Default_Indent_Off)->setChecked(true);
        break;
    case AUTO_INDENT:
        no_signals(ui.action_Default_Indent_On)->setChecked(true);
        break;
    case SMART_INDENT:
        no_signals(ui.action_Default_Indent_Smart)->setChecked(true);
        break;
    default:
        break;
    }

    // Default Wrap
    switch(GetPrefWrap(PLAIN_LANGUAGE_MODE)) {
    case NO_WRAP:
        no_signals(ui.action_Default_Wrap_None)->setChecked(true);
        break;
    case NEWLINE_WRAP:
        no_signals(ui.action_Default_Wrap_Auto_Newline)->setChecked(true);
        break;
    case CONTINUOUS_WRAP:
        no_signals(ui.action_Default_Wrap_Continuous)->setChecked(true);
        break;
    default:
        break;
    }

    if(GetPrefSmartTags()) {
        no_signals(ui.action_Default_Tag_Smart)->setChecked(true);
    } else {
        no_signals(ui.action_Default_Tag_Show_All)->setChecked(true);
    }

    // Default Search Settings
    no_signals(ui.action_Default_Search_Verbose)->setChecked(GetPrefSearchDlogs());
    no_signals(ui.action_Default_Search_Wrap_Around)->setChecked(GetPrefSearchWraps() == WrapMode::Wrap);
    no_signals(ui.action_Default_Search_Beep_On_Search_Wrap)->setChecked(GetPrefBeepOnSearchWrap());
    no_signals(ui.action_Default_Search_Keep_Dialogs_Up)->setChecked(GetPrefKeepSearchDlogs());

    switch(GetPrefSearch()) {
    case SEARCH_LITERAL:
        no_signals(ui.action_Default_Search_Literal)->setChecked(true);
        break;
    case SEARCH_CASE_SENSE:
        no_signals(ui.action_Default_Search_Literal_Case_Sensitive)->setChecked(true);
        break;
    case SEARCH_LITERAL_WORD:
        no_signals(ui.action_Default_Search_Literal_Whole_Word)->setChecked(true);
        break;
    case SEARCH_CASE_SENSE_WORD:
        no_signals(ui.action_Default_Search_Literal_Case_Sensitive_Whole_Word)->setChecked(true);
        break;
    case SEARCH_REGEX:
        no_signals(ui.action_Default_Search_Regular_Expression)->setChecked(true);
        break;
    case SEARCH_REGEX_NOCASE:
        no_signals(ui.action_Default_Search_Regular_Expresison_Case_Insensitive)->setChecked(true);
        break;
    default:
        break;
    }

    // Default syntax
    if(GetPrefHighlightSyntax()) {
        no_signals(ui.action_Default_Syntax_On)->setChecked(true);
    } else {
        no_signals(ui.action_Default_Syntax_Off)->setChecked(true);
    }

    no_signals(ui.action_Default_Apply_Backlighting)->setChecked(GetPrefBacklightChars());

    // Default tab settings
    no_signals(ui.action_Default_Tab_Open_File_In_New_Tab)->setChecked(GetPrefOpenInTab());
    no_signals(ui.action_Default_Tab_Show_Tab_Bar)->setChecked(GetPrefTabBar());
    no_signals(ui.action_Default_Tab_Hide_Tab_Bar_When_Only_One_Document_is_Open)->setChecked(GetPrefTabBarHideOne());
    no_signals(ui.action_Default_Tab_Next_Prev_Tabs_Across_Windows)->setChecked(GetPrefGlobalTabNavigate());
    no_signals(ui.action_Default_Tab_Sort_Tabs_Alphabetically)->setChecked(GetPrefSortTabs());

    no_signals(ui.tabWidget)->tabBar()->setVisible(GetPrefTabBar());
    no_signals(ui.tabWidget)->setTabBarAutoHide(GetPrefTabBarHideOne());

    no_signals(ui.action_Default_Show_Tooltips)->setChecked(GetPrefToolTips());
    no_signals(ui.action_Default_Statistics_Line)->setChecked(GetPrefStatsLine());
    no_signals(ui.action_Default_Incremental_Search_Line)->setChecked(GetPrefISearchLine());
    no_signals(ui.action_Default_Show_Line_Numbers)->setChecked(GetPrefLineNums());
    no_signals(ui.action_Default_Make_Backup_Copy)->setChecked(GetPrefSaveOldVersion());
    no_signals(ui.action_Default_Incremental_Backup)->setChecked(GetPrefAutoSave());

    switch(GetPrefShowMatching()) {
    case NO_FLASH:
        no_signals(ui.action_Default_Matching_Off)->setChecked(true);
        break;
    case FLASH_DELIMIT:
        no_signals(ui.action_Default_Matching_Delimiter)->setChecked(true);
        break;
    case FLASH_RANGE:
        no_signals(ui.action_Default_Matching_Range)->setChecked(true);
        break;
    }

    no_signals(ui.action_Default_Matching_Syntax_Based)->setChecked(GetPrefMatchSyntaxBased());
    no_signals(ui.action_Default_Terminate_with_Line_Break_on_Save)->setChecked(GetPrefAppendLF());
    no_signals(ui.action_Default_Popups_Under_Pointer)->setChecked(GetPrefRepositionDialogs());
    no_signals(ui.action_Default_Auto_Scroll_Near_Window_Top_Bottom)->setChecked(GetPrefAutoScroll());
    no_signals(ui.action_Default_Warnings_Files_Modified_Externally)->setChecked(GetPrefWarnFileMods());
    no_signals(ui.action_Default_Warnings_Check_Modified_File_Contents)->setChecked(GetPrefWarnRealFileMods());
    no_signals(ui.action_Default_Warnings_On_Exit)->setChecked(GetPrefWarnExit());
    no_signals(ui.action_Default_Warnings_Check_Modified_File_Contents)->setEnabled(GetPrefWarnFileMods());

    no_signals(ui.action_Default_Sort_Open_Prev_Menu)->setChecked(GetPrefSortOpenPrevMenu());
    no_signals(ui.action_Default_Show_Path_In_Windows_Menu)->setChecked(GetPrefShowPathInWindowsMenu());

    updateWindowSizeMenu();
    updateTipsFileMenuEx();
    updateTagsFileMenuEx();
    updateMenuItems(); // NOTE(eteran): from tags.cpp
}

//------------------------------------------------------------------------------
// Name: setupMenuStrings
// Desc: nedit has some menu shortcuts which are different from conventional
//       shortcuts. Fortunately, Qt has a means to do this stuff manually.
//------------------------------------------------------------------------------
void MainWindow::setupMenuStrings() {

	ui.action_Shift_Left         ->setText(tr("Shift &Left\t[Shift] Ctrl+9"));
	ui.action_Shift_Right        ->setText(tr("Shift Ri&ght\t[Shift] Ctrl+0"));
	ui.action_Find               ->setText(tr("&Find...\t[Shift] Ctrl+F"));
	ui.action_Find_Again         ->setText(tr("F&ind Again\t[Shift] Ctrl+G"));
	ui.action_Find_Selection     ->setText(tr("Find &Selection\t[Shift] Ctrl+H"));
	ui.action_Find_Incremental   ->setText(tr("Fi&nd Incremental\t[Shift] Ctrl+I"));
	ui.action_Replace            ->setText(tr("&Replace...\t[Shift] Ctrl+R"));
	ui.action_Replace_Find_Again ->setText(tr("Replace Find &Again\t[Shift] Ctrl+T"));
	ui.action_Replace_Again      ->setText(tr("Re&place Again\t[Shift] Alt+T"));
	ui.action_Mark               ->setText(tr("Mar&k\tAlt+M a-z"));
	ui.action_Goto_Mark          ->setText(tr("G&oto Mark\t[Shift] Alt+G a-z"));
	ui.action_Goto_Matching      ->setText(tr("Goto &Matching (..)\t[Shift] Ctrl+M"));

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_9), this, SLOT(action_Shift_Left_Tabs()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_0), this, SLOT(action_Shift_Right_Tabs()));

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_F), this, SLOT(action_Shift_Find()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_G), this, SLOT(action_Shift_Find_Again()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_H), this, SLOT(action_Shift_Find_Selection_triggered()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_I), this, SLOT(action_Shift_Find_Incremental_triggered()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_R), this, SLOT(action_Shift_Replace_triggered()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_M), this, SLOT(action_Shift_Goto_Matching_triggered()));

    // This is an annoying solution... we can probably do better...
    for(int i = Qt::Key_A; i <= Qt::Key_Z; ++i) {
        new QShortcut(QKeySequence(Qt::ALT + Qt::Key_M, i),             this, SLOT(action_Mark_Shortcut_triggered()));
        new QShortcut(QKeySequence(Qt::ALT + Qt::Key_G, i),             this, SLOT(action_Goto_Mark_Shortcut_triggered()));
        new QShortcut(QKeySequence(Qt::SHIFT + Qt::ALT + Qt::Key_G, i), this, SLOT(action_Shift_Goto_Mark_Shortcut_triggered()));
    }

    new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageDown), this, SLOT(action_Next_Document()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageUp), this, SLOT(action_Prev_Document()));
    new QShortcut(QKeySequence(Qt::ALT + Qt::Key_Home), this, SLOT(action_Last_Document()));
}

/**
 * @brief MainWindow::setupPrevOpenMenuActions
 */
void MainWindow::setupPrevOpenMenuActions() {
    const int maxPrevOpenFiles = GetPrefMaxPrevOpenFiles();

    if (maxPrevOpenFiles <= 0) {
        ui.action_Open_Previous->setEnabled(false);
    } else {

        // create all of the actions
        for(int i = 0; i < maxPrevOpenFiles; ++i) {
            auto action = new QAction(this);
            action->setVisible(true);
            previousOpenFilesList_.push_back(action);
        }

        auto prevMenu = new QMenu(this);

        prevMenu->addActions(previousOpenFilesList_);

        connect(prevMenu, SIGNAL(triggered(QAction *)), this, SLOT(openPrevCB(QAction *)));
        ui.action_Open_Previous->setMenu(prevMenu);
    }
}

//------------------------------------------------------------------------------
// Name: setupMenuAlternativeMenus
// Desc: under some configurations, menu items have different text/functionality
//------------------------------------------------------------------------------
void MainWindow::setupMenuAlternativeMenus() {
	if(!GetPrefOpenInTab()) {
		ui.action_New_Window->setText(tr("New &Tab"));
    } else {
        ui.action_New_Window->setText(tr("New &Window"));
    }

#if defined(REPLACE_SCOPE)
    auto replaceScopeGroup = new QActionGroup(this);
    auto replaceScopeMenu = new QMenu(tr("Default &Replace Scope"), this);

    replaceScopeInWindow_    = replaceScopeMenu->addAction(tr("In &Window"));
    replaceScopeInSelection_ = replaceScopeMenu->addAction(tr("In &Selection"));
    replaceScopeSmart_       = replaceScopeMenu->addAction(tr("Smart"));

    replaceScopeInWindow_->setCheckable(true);
    replaceScopeInSelection_->setCheckable(true);
    replaceScopeSmart_->setCheckable(true);

    switch(GetPrefReplaceDefScope()) {
    case REPL_DEF_SCOPE_WINDOW:
        replaceScopeInWindow_->setChecked(true);
        break;
    case REPL_DEF_SCOPE_SELECTION:
        replaceScopeInSelection_->setChecked(true);
        break;
    case REPL_DEF_SCOPE_SMART:
        replaceScopeSmart_->setChecked(true);
        break;
    }

    ui.menu_Default_Searching->addMenu(replaceScopeMenu);

    replaceScopeGroup->addAction(replaceScopeInWindow_);
    replaceScopeGroup->addAction(replaceScopeInSelection_);
    replaceScopeGroup->addAction(replaceScopeSmart_);

    connect(replaceScopeGroup, SIGNAL(triggered(QAction *)), this, SLOT(defaultReplaceScopeGroupTriggered(QAction *)));
#endif
}

//------------------------------------------------------------------------------
// Name: setupMenuGroups
//------------------------------------------------------------------------------
void MainWindow::setupMenuGroups() {
	auto indentGroup = new QActionGroup(this);
	indentGroup->addAction(ui.action_Indent_Off);
	indentGroup->addAction(ui.action_Indent_On);
	indentGroup->addAction(ui.action_Indent_Smart);
	
    auto wrapGroup = new QActionGroup(this);
    wrapGroup->addAction(ui.action_Wrap_None);
    wrapGroup->addAction(ui.action_Wrap_Auto_Newline);
    wrapGroup->addAction(ui.action_Wrap_Continuous);
	
	auto matchingGroup = new QActionGroup(this);
	matchingGroup->addAction(ui.action_Matching_Off);
	matchingGroup->addAction(ui.action_Matching_Range);
	matchingGroup->addAction(ui.action_Matching_Delimiter);
	
	auto defaultIndentGroup = new QActionGroup(this);
	defaultIndentGroup->addAction(ui.action_Default_Indent_Off);
	defaultIndentGroup->addAction(ui.action_Default_Indent_On);
	defaultIndentGroup->addAction(ui.action_Default_Indent_Smart);
	
	auto defaultWrapGroup = new QActionGroup(this);
	defaultWrapGroup->addAction(ui.action_Default_Wrap_None);
	defaultWrapGroup->addAction(ui.action_Default_Wrap_Auto_Newline);
	defaultWrapGroup->addAction(ui.action_Default_Wrap_Continuous);
	
	auto defaultTagCollisionsGroup = new QActionGroup(this);
	defaultTagCollisionsGroup->addAction(ui.action_Default_Tag_Show_All);
	defaultTagCollisionsGroup->addAction(ui.action_Default_Tag_Smart);
	
	auto defaultSearchGroup = new QActionGroup(this);
	defaultSearchGroup->addAction(ui.action_Default_Search_Literal);
	defaultSearchGroup->addAction(ui.action_Default_Search_Literal_Case_Sensitive);
	defaultSearchGroup->addAction(ui.action_Default_Search_Literal_Whole_Word);
	defaultSearchGroup->addAction(ui.action_Default_Search_Literal_Case_Sensitive_Whole_Word);
	defaultSearchGroup->addAction(ui.action_Default_Search_Regular_Expression);
	defaultSearchGroup->addAction(ui.action_Default_Search_Regular_Expresison_Case_Insensitive);
	
	auto defaultSyntaxGroup = new QActionGroup(this);
	defaultSyntaxGroup->addAction(ui.action_Default_Syntax_Off);
	defaultSyntaxGroup->addAction(ui.action_Default_Syntax_On);	
	
	auto defaultMatchingGroup = new QActionGroup(this);
	defaultMatchingGroup->addAction(ui.action_Default_Matching_Off);
	defaultMatchingGroup->addAction(ui.action_Default_Matching_Delimiter);
	defaultMatchingGroup->addAction(ui.action_Default_Matching_Range);

	auto defaultSizeGroup = new QActionGroup(this);
    defaultSizeGroup->addAction(ui.action_Default_Size_24_x_80);
	defaultSizeGroup->addAction(ui.action_Default_Size_40_x_80);
	defaultSizeGroup->addAction(ui.action_Default_Size_60_x_80);
	defaultSizeGroup->addAction(ui.action_Default_Size_80_x_80);
	defaultSizeGroup->addAction(ui.action_Default_Size_Custom);

    connect(indentGroup,               SIGNAL(triggered(QAction *)), this, SLOT(indentGroupTriggered(QAction *)));
    connect(wrapGroup,                 SIGNAL(triggered(QAction *)), this, SLOT(wrapGroupTriggered(QAction *)));
    connect(matchingGroup,             SIGNAL(triggered(QAction *)), this, SLOT(matchingGroupTriggered(QAction *)));
    connect(defaultIndentGroup,        SIGNAL(triggered(QAction *)), this, SLOT(defaultIndentGroupTriggered(QAction *)));
    connect(defaultWrapGroup,          SIGNAL(triggered(QAction *)), this, SLOT(defaultWrapGroupTriggered(QAction *)));
    connect(defaultTagCollisionsGroup, SIGNAL(triggered(QAction *)), this, SLOT(defaultTagCollisionsGroupTriggered(QAction *)));
    connect(defaultSearchGroup,        SIGNAL(triggered(QAction *)), this, SLOT(defaultSearchGroupTriggered(QAction *)));
    connect(defaultSyntaxGroup,        SIGNAL(triggered(QAction *)), this, SLOT(defaultSyntaxGroupTriggered(QAction *)));
    connect(defaultMatchingGroup,      SIGNAL(triggered(QAction *)), this, SLOT(defaultMatchingGroupTriggered(QAction *)));
    connect(defaultSizeGroup,          SIGNAL(triggered(QAction *)), this, SLOT(defaultSizeGroupTriggered(QAction *)));

}

//------------------------------------------------------------------------------
// Name: deleteTabButtonClicked
//------------------------------------------------------------------------------
void MainWindow::deleteTabButtonClicked() {
    on_action_Close_triggered();
}

//------------------------------------------------------------------------------
// Name: on_action_New_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_New_triggered() {
    action_New(NewMode::Prefs);
}

//------------------------------------------------------------------------------
// Name: on_action_New_triggered
//------------------------------------------------------------------------------
void MainWindow::action_New(NewMode mode) {

    bool openInTab = true;

    switch(mode) {
    case NewMode::Prefs:
        openInTab = GetPrefOpenInTab();
        break;
    case NewMode::Tab:
        openInTab = true;
        break;
    case NewMode::Window:
        openInTab = false;
        break;
    case NewMode::Opposite:
        openInTab = !GetPrefOpenInTab();
        break;
    }

	QString path;

    if(auto doc = currentDocument()) {
        path = doc->path_;
    }

    MainWindow::EditNewFileEx(openInTab ? this : nullptr, QString(), false, QString(), path);
    MainWindow::CheckCloseDimEx();
}

QString MainWindow::PromptForExistingFileEx(const QString &path, const QString &prompt) {
    QFileDialog dialog(this, prompt);
    dialog.setOptions(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if(!path.isEmpty()) {
        dialog.setDirectory(path);
    }

    if(dialog.exec()) {
        return dialog.selectedFiles()[0];
    }

    return QString();
}

//------------------------------------------------------------------------------
// Name: on_action_Open_triggered
//------------------------------------------------------------------------------
void MainWindow::action_Open(const QString &filename) {

    EMIT_EVENT_ARG("open_dialog", filename);

    if(auto doc = currentDocument()) {
        doc->open(filename);
    }
}

//------------------------------------------------------------------------------
// Name: on_action_Open_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Open_triggered() {

    EMIT_EVENT("open_dialog");

    if(auto doc = currentDocument()) {
        QString filename = PromptForExistingFileEx(doc->path_, tr("Open File"));
        if (filename.isNull()) {
            return;
        }

        action_Open(filename);
    }

    CheckCloseDimEx();
}

/**
 * @brief action_Close_triggered
 * @param mode
 */
void MainWindow::action_Close(CloseMode mode) {
    if(auto doc = currentDocument()) {
        doc->actionClose(mode);
    }
}

/**
 * @brief MainWindow::on_action_Close_triggered
 */
void MainWindow::on_action_Close_triggered() {
    action_Close(CloseMode::Prompt);
}

/**
 * @brief MainWindow::on_action_About_triggered
 */
void MainWindow::on_action_About_triggered() {
    static auto dialog = new DialogAbout(this);
	dialog->exec();
}

//------------------------------------------------------------------------------
// Name: on_action_Select_All_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Select_All_triggered() {
	if(TextArea *w = lastFocus_) {
		w->selectAllAP();
	}
}

//------------------------------------------------------------------------------
// Name: action_Include_File
//------------------------------------------------------------------------------
void MainWindow::action_Include_File(const QString &filename) {
    if(auto doc = currentDocument()) {

        if (doc->CheckReadOnly()) {
            return;
        }

        if (filename.isNull()) {
            return;
        }

        doc->includeFile(filename);
    }
}

//------------------------------------------------------------------------------
// Name: on_action_Include_File_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Include_File_triggered() {

    if(auto doc = currentDocument()) {

        if (doc->CheckReadOnly()) {
            return;
        }

        QString filename = PromptForExistingFileEx(doc->path_, tr("Include File"));

        if (filename.isNull()) {
            return;
        }

        doc->includeFile(filename);
    }
}

//------------------------------------------------------------------------------
// Name: on_action_Cut_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Cut_triggered() {
	if(TextArea *w = lastFocus_) {
		w->cutClipboardAP();
	}
}

//------------------------------------------------------------------------------
// Name: on_action_Copy_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Copy_triggered() {
	if(TextArea *w = lastFocus_) {
		w->copyClipboardAP();
	}
}

//------------------------------------------------------------------------------
// Name: on_action_Paste_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Paste_triggered() {
	if(TextArea *w = lastFocus_) {
		w->pasteClipboardAP();
	}
}

/**
 * @brief MainWindow::on_action_Paste_Column_triggered
 */
void MainWindow::on_action_Paste_Column_triggered() {
    if(TextArea *w = lastFocus_) {
        w->pasteClipboardAP(TextArea::RectFlag);
    }
}

/**
 * @brief MainWindow::on_action_Delete_triggered
 */
void MainWindow::on_action_Delete_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        doc->buffer_->BufRemoveSelected();
    }
}

/**
 * @brief MainWindow::CreateDocument
 * @param name
 * @return
 */
DocumentWidget *MainWindow::CreateDocument(QString name) {
	auto window = new DocumentWidget(name, this);
	int i = ui.tabWidget->addTab(window, name);
	ui.tabWidget->setCurrentIndex(i);
	return window;
}

/**
 * @brief MainWindow::UpdateWindowTitle
 * @param doc
 */
void MainWindow::UpdateWindowTitle(DocumentWidget *doc) {

    QString clearCaseTag = ClearCase::GetViewTag();

	QString title = DialogWindowTitle::FormatWindowTitle(
		doc->filename_,
		doc->path_,
		clearCaseTag,
        GetPrefServerName(),
		IsServer,
		doc->filenameSet_,
		doc->lockReasons_,
		doc->fileChanged_,
        GetPrefTitleFormat());

	setWindowTitle(title);


	QString iconTitle = doc->filename_;

    if (doc->fileChanged_) {
		iconTitle.append(tr("*"));
	}

    // NOTE(eteran): is the functional equivalent to "XmNiconName"?
    setWindowIconText(iconTitle);

	/* If there's a find or replace dialog up in "Keep Up" mode, with a
	   file name in the title, update it too */
	if (auto dialog = qobject_cast<DialogFind *>(dialogFind_)) {
        dialog->setDocument(doc);
	}

	if(auto dialog = getDialogReplace()) {
        dialog->setDocument(doc);
	}

	// Update the Windows menus with the new name
	InvalidateWindowMenus();
}

/**
 * @brief MainWindow::getDialogReplace
 * @return
 */
DialogReplace *MainWindow::getDialogReplace() const {
	return qobject_cast<DialogReplace *>(dialogReplace_);
}

/*
** Invalidate the Window menus of all NEdit windows to but don't change
** the menus until they're needed (Originally, this was "UpdateWindowMenus",
** but creating and destroying manu items for every window every time a
** new window was created or something changed, made things move very
** slowly with more than 10 or so windows).
*/
void MainWindow::InvalidateWindowMenus() {

    for(MainWindow *window : MainWindow::allWindows()) {
        window->updateWindowMenu();
    }
}

/*
** Update the Window menu of a single window to reflect the current state of
** all NEdit windows as determined by the global window list.
*/
void MainWindow::updateWindowMenu() {

    // Make a sorted list of windows
    QList<DocumentWidget *> documents = DocumentWidget::allDocuments();

    std::sort(documents.begin(), documents.end(), [](const DocumentWidget *a, const DocumentWidget *b) {

        // Untitled first
        int rc = a->filenameSet_ == b->filenameSet_ ? 0 : a->filenameSet_ && !b->filenameSet_ ? 1 : -1;
        if (rc != 0) {
            return rc < 0;
        }

        if(a->filename_ < b->filename_) {
            return true;
        }

        return a->path_ < b->path_;
    });

    ui.menu_Windows->clear();
    ui.menu_Windows->addAction(ui.action_Split_Pane);
    ui.menu_Windows->addAction(ui.action_Close_Pane);
    ui.menu_Windows->addSeparator();
    ui.menu_Windows->addAction(ui.action_Detach_Tab);
    ui.menu_Windows->addAction(ui.action_Move_Tab_To);
    ui.menu_Windows->addSeparator();

    for(DocumentWidget *document : documents) {
        QString title = document->getWindowsMenuEntry();
        QAction *action = ui.menu_Windows->addAction(title, this, SLOT(raiseCB()));
        action->setData(reinterpret_cast<qulonglong>(document));
    }
}

/*
** Update the read-only state of the text area(s) in the window, and
** the ReadOnly toggle button in the File menu to agree with the state in
** the window data structure.
*/
void MainWindow::UpdateWindowReadOnly(DocumentWidget *doc) {

	bool state = doc->lockReasons_.isAnyLocked();

    QList<TextArea *> textAreas = doc->textPanes();
	for(TextArea *area : textAreas) {
		area->setReadOnly(state);
	}

	ui.action_Read_Only->setChecked(state);
	ui.action_Read_Only->setEnabled(!doc->lockReasons_.isAnyLockedIgnoringUser());
}



/*
** check if tab bar is to be shown on this window
*/
int MainWindow::TabCount() {
	return ui.tabWidget->count();
}

/*
** Sort tabs in the tab bar alphabetically, if demanded so.
*/
void MainWindow::SortTabBar() {

    if (!GetPrefSortTabs()) {
		return;
    }

	// need more than one tab to sort
	const int nDoc = TabCount();
	if (nDoc < 2) {
		return;
	}

	// get a list of all the documents
	QVector<DocumentWidget *> windows;
	windows.reserve(nDoc);

	for(int i = 0; i < nDoc; ++i) {
		if(auto doc = qobject_cast<DocumentWidget *>(ui.tabWidget->widget(i))) {
			windows.push_back(doc);
		}
	}

	// sort them first by filename, then by path
	std::sort(windows.begin(), windows.end(), [](const DocumentWidget *a, const DocumentWidget *b) {
		if(a->filename_ < b->filename_) {
			return true;
		}
		return a->path_ < b->path_;
	});

	// shuffle around the tabs to their new indexes
	for(int i = 0; i < windows.size(); ++i) {
		int from = ui.tabWidget->indexOf(windows[i]);
		int to   = i;
        ui.tabWidget->tabBar()->moveTab(from, to);
	}
}

QList<MainWindow *> MainWindow::allWindows() {
    QList<MainWindow *> windows;

    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if(auto window = qobject_cast<MainWindow *>(widget)) {

            // we only include visible main windows because apparently
            // closed ones which haven't been deleted will end up in the list
            //if(window->isVisible()) {
                windows.push_back(window);
            //}
        }
    }
    return windows;
}

MainWindow *MainWindow::firstWindow() {
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if(auto window = qobject_cast<MainWindow *>(widget)) {
           return window;
        }
    }
    return nullptr;
}

QList<DocumentWidget *> MainWindow::openDocuments() const {
    QList<DocumentWidget *> list;
    for(int i = 0; i < ui.tabWidget->count(); ++i) {
        if(auto document = qobject_cast<DocumentWidget *>(ui.tabWidget->widget(i))) {
            list.push_back(document);
        }
    }
    return list;
}



/*
** Make sure the close menu item is dimmed appropriately for the current
** set of windows.  It should be dim only for the last Untitled, unmodified,
** editor window, and sensitive otherwise.
*/
void MainWindow::CheckCloseDimEx() {

    QList<MainWindow *> windows = MainWindow::allWindows();

    if(windows.empty()) {
        return;
    }

    if(windows.size() == 1) {
        // if there is only one window, then *this* must be it... right?
        MainWindow *window = windows[0];

        QList<DocumentWidget *> documents = window->openDocuments();

        if(window->TabCount() == 1 && !documents.front()->filenameSet_ && !documents.front()->fileChanged_) {
            window->ui.action_Close->setEnabled(false);
        } else {
            window->ui.action_Close->setEnabled(true);
        }
    } else {
        // if there is more than one window, then by definition, more than one
        // document is open
        for(MainWindow *window : windows) {
            window->ui.action_Close->setEnabled(true);
        }
    }
}

void MainWindow::raiseCB() {
    if(const auto action = qobject_cast<QAction *>(sender())) {
        if(const auto ptr = reinterpret_cast<DocumentWidget *>(action->data().value<qulonglong>())) {
            ptr->RaiseFocusDocumentWindow(true);
        }
    }
}

/*
** Create either the variable Shell menu, Macro menu or Background menu
** items of "window" (driven by value of "menuType")
*/
QMenu *MainWindow::createUserMenu(DocumentWidget *document, const QVector<MenuData> &data) {

    auto rootMenu = new QMenu(this);
    for(int i = 0; i < data.size(); ++i) {
        const MenuData &menuData = data[i];

        bool found = menuData.info->umiLanguageModes.isEmpty();

        for(int language = 0; language < menuData.info->umiLanguageModes.size(); ++language) {
            if(menuData.info->umiLanguageModes[language] == document->languageMode_) {
                found = true;
            }
        }

        if(!found) {
            continue;
        }

        QMenu *parentMenu = rootMenu;
        QString name = menuData.info->umiName;

        int index = 0;
        for (;;) {
            int subSep = name.indexOf(QLatin1Char('>'), index);
            if(subSep == -1) {
                name = name.mid(index);

                // create the actual action or, if it represents one of our
                // *very* common entries make it equivalent to the global
                // QAction representing that task
                if(menuData.item->cmd.trimmed() == QLatin1String("cut_clipboard()")) {
                    parentMenu->addAction(ui.action_Cut);
                } else if(menuData.item->cmd.trimmed() == QLatin1String("copy_clipboard()")) {
                    parentMenu->addAction(ui.action_Copy);
                } else if(menuData.item->cmd.trimmed() == QLatin1String("paste_clipboard()")) {
                    parentMenu->addAction(ui.action_Paste);
                } else if(menuData.item->cmd.trimmed() == QLatin1String("undo()")) {
                    parentMenu->addAction(ui.action_Undo);
                } else if(menuData.item->cmd.trimmed() == QLatin1String("redo()")) {
                    parentMenu->addAction(ui.action_Redo);
                } else {
                    QAction *action = parentMenu->addAction(name);
                    action->setData(i);

                    if(!menuData.item->shortcut.isEmpty()) {
                        action->setShortcut(menuData.item->shortcut);
                    }
                }

                break;
            }

            QString parentName = name.mid(index, subSep);
            int subSubSep = parentName.indexOf(QLatin1Char('>'));
            if(subSubSep != -1) {
                parentName = parentName.mid(0, subSubSep);
            }

            QList<QAction*> actions = parentMenu->actions();
            QAction *parentAction = nullptr;
            for(QAction *action : actions) {
                if(action->text() == parentName) {
                    parentAction = action;
                    break;
                }
            }

            if(!parentAction) {
                auto newMenu = new QMenu(parentName, this);
                parentMenu->addMenu(newMenu);
                parentMenu = newMenu;
            } else {
                parentMenu = parentAction->menu();
            }

            index = subSep + 1;
        }
    }
    return rootMenu;
}

void MainWindow::addToGroup(QActionGroup *group, QMenu *menu) {
    for(QAction *action : menu->actions()) {
        if(QMenu *subMenu = action->menu()) {
            addToGroup(group, subMenu);
        }
        group->addAction(action);
    }
}

/*
** Update the Shell, Macro, and Window Background menus of window
** "window" from the currently loaded command descriptions.
*/
void MainWindow::UpdateUserMenus(DocumentWidget *document) {

    // NOTE(eteran): the old code used to only do this if the language mode changed
    //               we should probably restore that behavior

    /* update user menus, which are shared over all documents, only
       if language mode was changed */
    auto shellMenu = createUserMenu(document, ShellMenuData);
    ui.menu_Shell->clear();
    ui.menu_Shell->addAction(ui.action_Execute_Command);
    ui.menu_Shell->addAction(ui.action_Execute_Command_Line);
    ui.menu_Shell->addAction(ui.action_Filter_Selection);
    ui.menu_Shell->addAction(ui.action_Cancel_Shell_Command);
    ui.menu_Shell->addSeparator();
    ui.menu_Shell->addActions(shellMenu->actions());
    auto shellGroup = new QActionGroup(this);
    shellGroup->setExclusive(false);
    addToGroup(shellGroup, shellMenu);
    connect(shellGroup, SIGNAL(triggered(QAction*)), this, SLOT(shellTriggered(QAction *)));

    auto macroMenu = createUserMenu(document, MacroMenuData);
    ui.menu_Macro->clear();
    ui.menu_Macro->addAction(ui.action_Learn_Keystrokes);
    ui.menu_Macro->addAction(ui.action_Finish_Learn);
    ui.menu_Macro->addAction(ui.action_Cancel_Learn);
    ui.menu_Macro->addAction(ui.action_Replay_Keystrokes);
    ui.menu_Macro->addAction(ui.action_Repeat);
    ui.menu_Macro->addSeparator();
    ui.menu_Macro->addActions(macroMenu->actions());
    auto macroGroup = new QActionGroup(this);
    macroGroup->setExclusive(false);
    addToGroup(macroGroup, macroMenu);
    connect(macroGroup, SIGNAL(triggered(QAction*)), this, SLOT(macroTriggered(QAction *)));

    /* update background menu, which is owned by a single document, only
       if language mode was changed */
    document->contextMenu_ = createUserMenu(document, BGMenuData);
}

/*
** Re-build the language mode sub-menu using the current data stored
** in the master list: LanguageModes.
*/
void MainWindow::updateLanguageModeSubmenu() {

    auto languageGroup = new QActionGroup(this);
    auto languageMenu = new QMenu(this);
    QAction *action = languageMenu->addAction(tr("Plain"));
    action->setData(PLAIN_LANGUAGE_MODE);
    action->setCheckable(true);
    action->setChecked(true);
    languageGroup->addAction(action);


    for (int i = 0; i < LanguageModes.size(); i++) {
        QAction *action = languageMenu->addAction(LanguageModes[i].name);
        action->setData(i);
        action->setCheckable(true);
        languageGroup->addAction(action);
    }

    ui.action_Language_Mode->setMenu(languageMenu);
    connect(languageMenu, SIGNAL(triggered(QAction *)), this, SLOT(setLangModeCB(QAction *)));
}

/*
** Create a submenu for chosing language mode for the current window.
*/
void MainWindow::CreateLanguageModeSubMenu() {
    updateLanguageModeSubmenu();
}

void MainWindow::setLangModeCB(QAction *action) {

    if(auto document = qobject_cast<DocumentWidget *>(ui.tabWidget->currentWidget())) {
        const int mode = action->data().value<int>();

        // If the mode didn't change, do nothing
        if (document->languageMode_ == mode) {
            return;
        }

        if(mode == PLAIN_LANGUAGE_MODE) {
            document->setLanguageMode(QString());
        } else {
            document->setLanguageMode(LanguageModes[mode].name);
        }
    }
}

/*
**  If necessary, enlarges the window and line number display area to make
**  room for numbers.
*/
int MainWindow::updateLineNumDisp() {

    if (!showLineNumbers_) {
        return 0;
    }

    /* Decide how wide the line number field has to be to display all
       possible line numbers */
    return updateGutterWidth();
}

/*
**  Set the new gutter width in the window. Sadly, the only way to do this is
**  to set it on every single document, so we have to iterate over them.
**
**  (Iteration taken from TabCount(); is there a better way to do it?)
*/
int MainWindow::updateGutterWidth() {

    int reqCols = MIN_LINE_NUM_COLS;
    int maxCols = 0;

    QList<DocumentWidget *> documents  = openDocuments();
    for(DocumentWidget *document : documents) {
        //  We found ourselves a document from this window. Pick a TextArea
        // (doesn't matter which), so we can probe some details...
        if(TextArea *area = document->firstPane()) {
            const int lineNumCols = area->getLineNumCols();

            /* Is the width of the line number area sufficient to display all the
               line numbers in the file?  If not, expand line number field, and the
               this width. */
            if (lineNumCols > maxCols) {
                maxCols = lineNumCols;
            }

            int tmpReqCols = area->getBufferLinesCount() < 1 ? 1 : static_cast<int>(log10(static_cast<double>(area->getBufferLinesCount()) + 1)) + 1;

            if (tmpReqCols > reqCols) {
                reqCols = tmpReqCols;
            }
        }
    }

#if 0
    int newColsDiff = 0;
    if (reqCols != maxCols) {
        Dimension windowWidth;
        short fontWidth;

        int newColsDiff = reqCols - maxCols;

        fs = textD_of(textArea_)->getFont();
        fontWidth = fs->max_bounds.width;

        XtVaGetValues(shell_, XmNwidth, &windowWidth, nullptr);
        XtVaSetValues(shell_, XmNwidth, (Dimension)windowWidth + (newColsDiff * fontWidth), nullptr);
    }
#endif


    for(DocumentWidget *document : documents) {
        if(TextArea *area = document->firstPane()) {
            int lineNumCols = area->getLineNumCols();
            if (lineNumCols == reqCols) {
                continue;
            }

            //  Update all panes of this document.
            for(TextArea *pane : document->textPanes()) {
                pane->setLineNumCols(reqCols);
            }
        }
    }

    return reqCols;
}

/*
** Temporarily show and hide the incremental search line if the line is not
** already up.
*/
void MainWindow::TempShowISearch(bool state) {
    if (showISearchLine_) {
        return;
    }

    if(ui.incrementalSearchFrame->isVisible() != state) {
        ui.incrementalSearchFrame->setVisible(state);
    }
}

/*
** Find a name for an untitled file, unique in the name space of in the opened
** files in this session, i.e. Untitled or Untitled_nn, and write it into
** the string "name".
*/
QString MainWindow::UniqueUntitledNameEx() {

    for (int i = 0; i < INT_MAX; i++) {
        QString name;

        if (i == 0) {
            name = tr("Untitled");
        } else {
            name = tr("Untitled_%1").arg(i);
        }

        QList<DocumentWidget *> documents = DocumentWidget::allDocuments();

        auto it = std::find_if(documents.begin(), documents.end(), [name](DocumentWidget *window) {
            return window->filename_ == name;
        });

        if(it == documents.end()) {
            return name;
        }
    }

    return QString();
}

void MainWindow::on_action_Undo_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        doc->Undo();
    }
}

void MainWindow::on_action_Redo_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        doc->Redo();
    }
}

/*
** Check if there is already a window open for a given file
*/
DocumentWidget *MainWindow::FindWindowWithFile(const QString &name, const QString &path) {

    QList<DocumentWidget *> documents = DocumentWidget::allDocuments();

    /* I don't think this algorithm will work on vms so I am disabling it for now */
    if (!GetPrefHonorSymlinks()) {

        QString fullname = QString(QLatin1String("%1%2")).arg(path, name);

        struct stat attribute;
        if (::stat(fullname.toLatin1().data(), &attribute) == 0) {
            auto it = std::find_if(documents.begin(), documents.end(), [attribute](DocumentWidget *document){
                return attribute.st_dev == document->device_ && attribute.st_ino == document->inode_;
            });

            if(it != documents.end()) {
                return *it;
            }
        } /*  else:  Not an error condition, just a new file. Continue to check
              whether the filename is already in use for an unsaved document.  */

    }


    auto it = std::find_if(documents.begin(), documents.end(), [name, path](DocumentWidget *document) {
        return (document->filename_ == name) && (document->path_ == path);
    });

    if(it != documents.end()) {
        return *it;
    }

    return nullptr;
}

/*
** Force ShowLineNumbers() to re-evaluate line counts for the window if line
** counts are required.
*/
void MainWindow::forceShowLineNumbers() {
    bool showLineNum = showLineNumbers_;
    if (showLineNum) {
        showLineNumbers_ = false;
        ShowLineNumbers(showLineNum);
    }
}

/*
** Turn on and off the display of line numbers
*/
void MainWindow::ShowLineNumbers(bool state) {

    if (showLineNumbers_ == state) {
        return;
    }

    showLineNumbers_ = state;

    /* Just setting showLineNumbers_ is sufficient to tell
       updateLineNumDisp() to expand the line number areas and the this
       size for the number of lines required.  To hide the line number
       display, set the width to zero, and contract the this width. */
    int reqCols = 0;
    if (state) {
        reqCols = updateLineNumDisp();
    }

    /* line numbers panel is shell-level, hence other
       tabbed documents in the this should synch */
    QList<DocumentWidget *> docs = openDocuments();
    for(DocumentWidget *doc : docs) {

        showLineNumbers_ = state;

        QList<TextArea *> areas = doc->textPanes();
        for(TextArea *area : areas) {
            //  reqCols should really be cast here, but into what? XmRInt?
            area->setLineNumCols(reqCols);
        }

    }
}

/*
** Add a file to the list of previously opened files for display in the
** File menu.
*/
void MainWindow::AddToPrevOpenMenu(const QString &filename) {

    // If the Open Previous command is disabled, just return
    if (GetPrefMaxPrevOpenFiles() < 1) {
        return;
    }

    /*  Refresh list of previously opened files to avoid Big Race Condition,
        where two sessions overwrite each other's changes in NEdit's
        history file.
        Of course there is still Little Race Condition, which occurs if a
        Session A reads the list, then Session B reads the list and writes
        it before Session A gets a chance to write.  */
    ReadNEditDB();

    // If the name is already in the list, move it to the start
    int index = PrevOpen.indexOf(filename);
    if(index != -1) {
        PrevOpen.move(index, 0);
        invalidatePrevOpenMenus();
        WriteNEditDB();
        return;
    }

    // If the list is already full, make room
    if (PrevOpen.size() >= GetPrefMaxPrevOpenFiles()) {
        //  This is only safe if GetPrefMaxPrevOpenFiles() > 0.
        PrevOpen.pop_back();
    }

    // Add it to the list
    PrevOpen.push_front(filename);

    // Mark the Previously Opened Files menu as invalid in all windows
    invalidatePrevOpenMenus();

    // Undim the menu in all windows if it was previously empty
    if (PrevOpen.size() > 0) {
        QList<MainWindow *> windows = allWindows();
        for(MainWindow *window : windows) {
            window->ui.action_Open_Previous->setEnabled(true);
        }
    }

    // Write the menu contents to disk to restore in later sessions
    WriteNEditDB();
}

/*
**  Read database of file names for 'Open Previous' submenu.
**
**  Eventually, this may hold window positions, and possibly file marks (in
**  which case it should be moved to a different module) but for now it's
**  just a list of previously opened files.
**
**  This list is read once at startup and potentially refreshed before a
**  new entry is about to be written to the file or before the menu is
**  displayed. If the file is modified since the last read (or not read
**  before), it is read in, otherwise nothing is done.
*/
void MainWindow::ReadNEditDB() {

	static QDateTime lastNeditdbModTime;

    /*  If the Open Previous command is disabled or the user set the
        resource to an (invalid) negative value, just return.  */
    if (GetPrefMaxPrevOpenFiles() < 1) {
        return;
    }

    /* Don't move this check ahead of the previous statements. PrevOpen
       must be initialized at all times. */
    QString fullName = Settings::historyFile();
    if(fullName.isNull()) {
        return;
    }

	QFileInfo info(fullName);
	QDateTime mtime = info.lastModified();

	/*  Stat history file to see whether someone touched it after this
		session last changed it.  */
	if(lastNeditdbModTime >= mtime) {
		//  Do nothing, history file is unchanged.
		return;
	} else {
		//  Memorize modtime to compare to next time.
		lastNeditdbModTime = mtime;
	}

    // open the file
    QFile file(fullName);
    if(!file.open(QIODevice::ReadOnly)) {
		return;
	}

    //  Clear previous list.
    PrevOpen.clear();

    /* read lines of the file, lines beginning with # are considered to be
       comments and are thrown away.  Lines are subject to cursory checking,
       then just copied to the Open Previous file menu list */
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();

        if(line.isEmpty()) {
			// blank line
			continue;
		}

        if (line.startsWith(QLatin1Char('#'))) {
            // comment
            continue;
        }

		// NOTE(eteran): right now, neditDBBadFilenameChars only consists of a
		// newline character, I don't see how it is even possible for that to
		// happen as we are reading lines which are obviously delimited by
		// '\n', but the check remains as we could hypothetically ban other
		// characters in the future.
        if(line.contains(neditDBBadFilenameChars)) {
            qWarning("NEdit: History file may be corrupted");
            continue;
        }

        PrevOpen.push_back(line);

        if (PrevOpen.size() >= GetPrefMaxPrevOpenFiles()) {
            // too many entries
            return;
        }
    }
}

/*
** Mark the Previously Opened Files menus of all NEdit windows as invalid.
** Since actually changing the menus is slow, they're just marked and updated
** when the user pulls one down.
*/
void MainWindow::invalidatePrevOpenMenus() {

    /* Mark the menus invalid (to be updated when the user pulls one
       down), unless the menu is torn off, meaning it is visible to the user
       and should be updated immediately */
    QList<MainWindow *> windows = allWindows();
    for(MainWindow *window : windows) {
        window->updatePrevOpenMenu();
    }
}

/*
** Write dynamic database of file names for "Open Previous".  Eventually,
** this may hold window positions, and possibly file marks, in which case,
** it should be moved to a different module, but for now it's just a list
** of previously opened files.
*/
void MainWindow::WriteNEditDB() {


    QString fullName = Settings::historyFile();
    if(fullName.isNull()) {
        return;
    }

    // If the Open Previous command is disabled, just return
    if (GetPrefMaxPrevOpenFiles() < 1) {
        return;
    }

    QFile file(fullName);
    if(file.open(QIODevice::WriteOnly)) {
        QTextStream ts(&file);

        static const QString fileHeader = tr("# File name database for NEdit Open Previous command");
        ts << fileHeader << tr("\n");

        // Write the list of file names
        for(const QString &line : PrevOpen) {
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && !line.contains(neditDBBadFilenameChars)) {
                ts << line << tr("\n");
            }
        }
    }
}

/*
** Update the Previously Opened Files menu of a single window to reflect the
** current state of the list as retrieved from FIXME.
** Thanks to Markus Schwarzenberg for the sorting part.
*/
void MainWindow::updatePrevOpenMenu() {

    if (GetPrefMaxPrevOpenFiles() == 0) {
        ui.action_Open_Previous->setEnabled(false);
        return;
    }

    //  Read history file to get entries written by other sessions.
    ReadNEditDB();

    // Sort the previously opened file list if requested
    QList<QString> prevOpenSorted = PrevOpen;
    if (GetPrefSortOpenPrevMenu()) {
        qSort(prevOpenSorted);
    }

    for(int i = 0; i < prevOpenSorted.size(); ++i) {
        QString filename = prevOpenSorted[i];
        previousOpenFilesList_[i]->setText(filename);
        previousOpenFilesList_[i]->setData(filename);
        previousOpenFilesList_[i]->setVisible(true);
    }

    for(int i = prevOpenSorted.size(); i < previousOpenFilesList_.size(); ++i) {
        previousOpenFilesList_[i]->setVisible(false);
    }
}

void MainWindow::openPrevCB(QAction *action) {

    QString filename = action->data().toString();

    if(auto doc = currentDocument()) {
        if (filename.isNull()) {
            return;
        }

        doc->open(filename);
    }

    CheckCloseDimEx();
}

void MainWindow::on_tabWidget_currentChanged(int index) {
    if(auto document = qobject_cast<DocumentWidget *>(ui.tabWidget->widget(index))) {
        document->documentRaised();
    }
}

void MainWindow::on_tabWidget_customContextMenuRequested(const QPoint &pos) {
    const int index = ui.tabWidget->tabBar()->tabAt(pos);
    if(index != -1) {
        auto *menu = new QMenu(this);
        QAction *const newTab    = menu->addAction(tr("New Tab"));
        QAction *const closeTab  = menu->addAction(tr("Close Tab"));
        menu->addSeparator();
        QAction *const detachTab = menu->addAction(tr("Detach Tab"));
        QAction *const moveTab   = menu->addAction(tr("Move Tab To..."));

        // make sure that these are always in sync with the primary UI
        detachTab->setEnabled(ui.action_Detach_Tab->isEnabled());
        moveTab->setEnabled(ui.action_Move_Tab_To->isEnabled());

        // make the icons the same too :-P
        newTab->setIcon(ui.action_New->icon());
        closeTab->setIcon(ui.action_Close->icon());
        detachTab->setIcon(ui.action_Detach_Tab->icon());
        moveTab->setIcon(ui.action_Move_Tab_To->icon());

        if(QAction *const selected = menu->exec(ui.tabWidget->tabBar()->mapToGlobal(pos))) {

            if(DocumentWidget *document = documentAt(index)) {

                if(selected == newTab) {
                    MainWindow::EditNewFileEx(this, QString(), false, QString(), document->path_);
                } else if(selected == closeTab) {
                    document->actionClose(CloseMode::Prompt);
                } else if(selected == detachTab) {
                    if(TabCount() > 1) {
                        auto new_window = new MainWindow(nullptr);
                        new_window->ui.tabWidget->addTab(document, document->filename_);
                        new_window->show();
                    }
                } else if(selected == moveTab) {
                    document->moveDocument(this);
                }
            }
        }
    }
}

void MainWindow::on_action_Open_Selected_triggered() {

    if(auto doc = currentDocument()) {

        // Get the selected text, if there's no selection, do nothing
        const QMimeData *mimeData = QApplication::clipboard()->mimeData(QClipboard::Selection);
        if(mimeData->hasText()) {
            fileCB(doc, mimeData->text());
        } else {
            QApplication::beep();
        }
    }

    CheckCloseDimEx();
}

//------------------------------------------------------------------------------
// Name: fileCB
// Desc: opens a "selected file"
//------------------------------------------------------------------------------
void MainWindow::fileCB(DocumentWidget *window, const QString &text) {

	// TODO(eteran): 2.0, let the user specify a list of potential paths!
    //       can we ask the system simply or similar?
    //       `gcc -print-prog-name=cc1plus` -v
    //       `gcc -print-prog-name=cc1` -v
    //       etc...
    static auto includeDir = QLatin1String("/usr/include/");

    /* get the string, or skip if we can't get the selection data, or it's
       obviously not a file name */
    if (text.size() > MAXPATHLEN || text.isEmpty()) {
        QApplication::beep();
        return;
    }

    QRegExp regexSystem(QLatin1String("#include\\s*<([^>]+)>"), Qt::CaseSensitive, QRegExp::RegExp2);
    QRegExp regexLocal(QLatin1String("#include\\s*\"([^\"]+)\""), Qt::CaseSensitive, QRegExp::RegExp2);
    QString nameText = text;

    // extract name from #include syntax
	// TODO(eteran): 2.0, support import/include syntax from multiple languages
    if(regexLocal.indexIn(nameText) != -1) {
        nameText = regexLocal.cap(1);
    } else if(regexSystem.indexIn(nameText) != -1) {
        nameText = QString(QLatin1String("%1%2")).arg(includeDir, regexSystem.cap(1));
    }

    // strip whitespace from name
    nameText.remove(QLatin1Char(' '));
    nameText.remove(QLatin1Char('\t'));
    nameText.remove(QLatin1Char('\n'));

    // Process ~ characters in name
    nameText = ExpandTildeEx(nameText);

    // If path name is relative, make it refer to current window's directory
    if (nameText[0] != QLatin1Char('/')) {
		nameText = QString(QLatin1String("%1%2")).arg(window->path_, nameText);
    }

#if !defined(DONT_HAVE_GLOB)
    // Expand wildcards in file name.
    {
        glob_t globbuf;
        glob(nameText.toLatin1().data(), GLOB_NOCHECK, nullptr, &globbuf);

        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            QString pathname;
            QString filename;
            if (ParseFilenameEx(QString::fromLatin1(globbuf.gl_pathv[i]), &filename, &pathname) != 0) {
                QApplication::beep();
            } else {
                DocumentWidget::EditExistingFileEx(
                            GetPrefOpenInTab() ? window : nullptr,
                            filename,
                            pathname,
                            0,
                            QString(),
                            false,
                            QString(),
                            GetPrefOpenInTab(),
                            false);
            }
        }
        globfree(&globbuf);
    }
#else
	QString pathname;
	QString filename;
	if (ParseFilenameEx(nameText, &filename, &pathname) != 0) {
		QApplication::beep();
	} else {
		DocumentWidget::EditExistingFileEx(
		            GetPrefOpenInTab() ? window : nullptr,
		            filename,
		            pathname,
		            0,
		            QString(),
		            false,
		            QString(),
		            GetPrefOpenInTab(),
		            false);
	}
#endif

    CheckCloseDimEx();
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Shift_Left_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        ShiftSelectionEx(doc, lastFocus_, SHIFT_LEFT, false);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Shift_Right_triggered() {

    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        ShiftSelectionEx(doc, lastFocus_, SHIFT_RIGHT, false);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Left_Tabs() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        ShiftSelectionEx(doc, lastFocus_, SHIFT_LEFT, true);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Right_Tabs() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        ShiftSelectionEx(doc, lastFocus_, SHIFT_RIGHT, true);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Lower_case_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        DowncaseSelectionEx(doc, lastFocus_);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Upper_case_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        UpcaseSelectionEx(doc, lastFocus_);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Fill_Paragraph_triggered() {

    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }
        FillSelectionEx(doc, lastFocus_);
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Insert_Form_Feed_triggered() {

    if(TextArea *w = lastFocus_) {
        w->insertStringAP(QLatin1String("\f"));
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Insert_Ctrl_Code_triggered() {

    if(auto doc = currentDocument()) {

        if (doc->CheckReadOnly()) {
            return;
        }

        bool ok;
        int n = QInputDialog::getInt(this, tr("Insert Ctrl Code"), tr("ASCII Character Code:"), 0, 0, 255, 1, &ok);
        if(ok) {
            char charCodeString[2];
            charCodeString[0] = static_cast<uint8_t>(n);
            charCodeString[1] = '\0';

            if (!doc->buffer_->BufSubstituteNullChars(charCodeString, 1)) {
                QMessageBox::critical(this, tr("Error"), tr("Too much binary data"));
                return;
            }

            if(TextArea *w = lastFocus_) {
                w->insertStringAP(QString::fromLatin1(charCodeString));
            }
        }
    }
}

/**
 * @brief MainWindow::action_Goto_Line_Number
 * @param s
 */
void MainWindow::action_Goto_Line_Number(const QString &s) {
	if(auto doc = currentDocument()) {

		int lineNum;
		int column;

        if (StringToLineAndCol(s, &lineNum, &column) == -1) {
			QApplication::beep();
			return;
		}

		if(TextArea *w = lastFocus_) {
			doc->gotoAP(w, s);
		}
	}
}

/**
 * @brief MainWindow::on_action_Goto_Line_Number_triggered
 */
void MainWindow::on_action_Goto_Line_Number_triggered() {

	bool ok;
	QString text = QInputDialog::getText(
	                   this,
	                   tr("Goto Line Number"),
	                   tr("Goto Line (and/or Column)  Number:"),
	                   QLineEdit::Normal,
	                   QString(),
	                   &ok);

	if (!ok) {
		return;
	}

	action_Goto_Line_Number(text);
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Goto_Selected_triggered() {
    if(auto doc = currentDocument()) {

        const QMimeData *mimeData = QApplication::clipboard()->mimeData(QClipboard::Selection);
        if(mimeData->hasText()) {
            if(TextArea *w = lastFocus_) {
				doc->gotoAP(w, mimeData->text());
            }
        } else {
            QApplication::beep();
        }
    }
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Find() {
    DoFindDlogEx(
        this,
        currentDocument(),
        SEARCH_BACKWARD,
        GetPrefKeepSearchDlogs(),
        GetPrefSearch());
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Find_Again() {
    SearchAndSelectSameEx(
        this,
        currentDocument(),
        lastFocus_,
        SEARCH_BACKWARD,
        GetPrefSearchWraps());
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Find_Selection_triggered() {
    SearchForSelectedEx(
        this,
        currentDocument(),
        lastFocus_,
        SEARCH_BACKWARD,
        GetPrefSearch(),
        GetPrefSearchWraps());
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::on_action_Find_Incremental_triggered() {
    BeginISearchEx(SEARCH_FORWARD);
}

//------------------------------------------------------------------------------
// Name:
//------------------------------------------------------------------------------
void MainWindow::action_Shift_Find_Incremental_triggered() {
    BeginISearchEx(SEARCH_BACKWARD);
}

//------------------------------------------------------------------------------
// Name: on_editIFind_textChanged
//------------------------------------------------------------------------------
/*
** Called when user types in the incremental search line.  Redoes the
** search for the new search string.
*/
void MainWindow::on_editIFind_textChanged(const QString &text) {

    SearchType searchType;

    if(ui.checkIFindCase->isChecked()) {
        if (ui.checkIFindRegex->isChecked()) {
            searchType = SEARCH_REGEX;
        } else {
            searchType = SEARCH_CASE_SENSE;
        }
    } else {
        if (ui.checkIFindRegex->isChecked()) {
            searchType = SEARCH_REGEX_NOCASE;
        } else {
            searchType = SEARCH_LITERAL;
        }
    }

    const SearchDirection direction = ui.checkIFindReverse->isChecked() ? SEARCH_BACKWARD : SEARCH_FORWARD;

    /* If the search type is a regular expression, test compile it.  If it
       fails, silently skip it.  (This allows users to compose the expression
       in peace when they have unfinished syntax, but still get beeps when
       correct syntax doesn't match) */
    if (isRegexType(searchType)) {
        try {
            auto compiledRE = make_regex(text, defaultRegexFlags(searchType));
        } catch(const regex_error &) {
            return;
        }
    }

    /* Call the incremental search action proc to do the searching and
       selecting (this allows it to be recorded for learn/replay).  If
       there's an incremental search already in progress, mark the operation
       as "continued" so the search routine knows to re-start the search
       from the original starting position */
    if(auto doc = currentDocument()) {
		doc->findIncrAP(text, direction, searchType, GetPrefSearchWraps(), iSearchStartPos_ != -1);
    }
}

//------------------------------------------------------------------------------
// Name: on_buttonIFind_clicked
//------------------------------------------------------------------------------
void MainWindow::on_buttonIFind_clicked() {
    // same as pressing return
    on_editIFind_returnPressed();
}

//------------------------------------------------------------------------------
// Name: on_editIFind_returnPressed
// Desc: User pressed return in the incremental search bar.  Do a new search with
//       the search string displayed.  The direction of the search is toggled if
//       the Ctrl key or the Shift key is pressed when the text field is activated.
//------------------------------------------------------------------------------
void MainWindow::on_editIFind_returnPressed() {

    SearchType searchType;

    /* Fetch the string, search type and direction from the incremental
       search bar widgets at the top of the window */
    QString searchString = ui.editIFind->text();

    if (ui.checkIFindCase->isChecked()) {
        if (ui.checkIFindRegex->isChecked())
            searchType = SEARCH_REGEX;
        else
            searchType = SEARCH_CASE_SENSE;
    } else {
        if (ui.checkIFindRegex->isChecked())
            searchType = SEARCH_REGEX_NOCASE;
        else
            searchType = SEARCH_LITERAL;
    }

    SearchDirection direction = ui.checkIFindReverse->isChecked() ? SEARCH_BACKWARD : SEARCH_FORWARD;

    // Reverse the search direction if the Ctrl or Shift key was pressed
    if(QApplication::keyboardModifiers() & (Qt::CTRL | Qt::SHIFT)) {
        direction = (direction == SEARCH_FORWARD) ? SEARCH_BACKWARD : SEARCH_FORWARD;
    }

    // find the text and mark it
    if(auto doc = currentDocument()) {
        doc->findAP(searchString, direction, searchType, GetPrefSearchWraps());
    }
}

/**
 * @brief MainWindow::keyPressEvent
 * @param event
 */
void MainWindow::keyPressEvent(QKeyEvent *event) {

	int index;

	// only process up and down arrow keys
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down && event->key() != Qt::Key_Escape) {
        QMainWindow::keyPressEvent(event);
        return;
    }

	index = iSearchHistIndex_;

    // allow escape key to cancel search
    if (event->key() == Qt::Key_Escape) {
        EndISearchEx();
        return;
    }

    // increment or decrement the index depending on which arrow was pressed
    index += (event->key() == Qt::Key_Up) ? 1 : -1;

    // if the index is out of range, beep and return
    if (index != 0 && historyIndex(index) == -1) {
        QApplication::beep();
        return;
    }

    QString searchStr;
    SearchType searchType;

    // determine the strings and button settings to use
    if (index == 0) {
		searchStr  = QLatin1String("");
        searchType = GetPrefSearch();
    } else {
        searchStr  = SearchReplaceHistory[historyIndex(index)].search;
        searchType = SearchReplaceHistory[historyIndex(index)].type;
    }

    /* Set the info used in the value changed callback before calling XmTextSetStringEx(). */
    iSearchHistIndex_ = index;
    initToggleButtonsiSearch(searchType);

    // Beware the value changed callback is processed as part of this call
    int previousPosition = ui.editIFind->cursorPosition();
    ui.editIFind->setText(searchStr);
    ui.editIFind->setCursorPosition(previousPosition);
}

/*
** The next few callbacks handle the states of find/replace toggle
** buttons, which depend on the state of the "Regex" button, and the
** sensitivity of the Whole Word buttons.
** Callbacks are necessary for both "Regex" and "Case Sensitive"
** buttons to make sure the states are saved even after a cancel operation.
**
** If sticky case sensitivity is requested, the behaviour is as follows:
**   The first time "Regular expression" is checked, "Match case" gets
**   checked too. Thereafter, checking or unchecking "Regular expression"
**   restores the "Match case" button to the setting it had the last
**   time when literals or REs where used.
** Without sticky behaviour, the state of the Regex button doesn't influence
** the state of the Case Sensitive button.
**
** Independently, the state of the buttons is always restored to the
** default state when a dialog is popped up, and when the user returns
** from stepping through the search history.
*/
void MainWindow::on_checkIFindCase_toggled(bool searchCaseSense) {

    /* Save the state of the Case Sensitive button
       depending on the state of the Regex button*/
    if (ui.checkIFindRegex->isChecked()) {
        iSearchLastRegexCase_ = searchCaseSense;
    } else {
        iSearchLastLiteralCase_ = searchCaseSense;
    }

    // When search parameters (direction or search type), redo the search
    on_editIFind_returnPressed();
}

void MainWindow::on_checkIFindRegex_toggled(bool searchRegex) {

    bool searchCaseSense = ui.checkIFindCase->isChecked();

    // In sticky mode, restore the state of the Case Sensitive button
    if (GetPrefStickyCaseSenseBtn()) {
        if (searchRegex) {
            iSearchLastLiteralCase_ = searchCaseSense;
            no_signals(ui.checkIFindCase)->setChecked(iSearchLastRegexCase_);
        } else {
            iSearchLastRegexCase_ = searchCaseSense;
            no_signals(ui.checkIFindCase)->setChecked(iSearchLastLiteralCase_);
        }
    }
    // The iSearch bar has no Whole Word button to enable/disable.

    // When search parameters (direction or search type), redo the search
    // TODO(eteran): original nedit seems to have code to do this @search.c:3071
    //               but in practice, toggling the case/regex often doesn't seem
    //               to change the search
    on_editIFind_returnPressed();
}

void MainWindow::on_checkIFindReverse_toggled(bool value) {

	Q_UNUSED(value);

    // When search parameters (direction or search type), redo the search
    on_editIFind_returnPressed();
}

/*
** Shared routine for replace and find dialogs and i-search bar to initialize
** the state of the regex/case/word toggle buttons, and the sticky case
** sensitivity states.
*/
void MainWindow::initToggleButtonsiSearch(SearchType searchType) {
    /* Set the initial search type and remember the corresponding case
       sensitivity states in case sticky case sensitivity is required. */

    switch (searchType) {
    case SEARCH_LITERAL:
        iSearchLastLiteralCase_ = false;
        iSearchLastRegexCase_   = true;
        ui.checkIFindRegex->setChecked(false);
        ui.checkIFindCase->setChecked(false);
        break;
    case SEARCH_CASE_SENSE:
        iSearchLastLiteralCase_ = true;
        iSearchLastRegexCase_   = true;
        ui.checkIFindRegex->setChecked(false);
        ui.checkIFindCase->setChecked(true);
        break;
    case SEARCH_LITERAL_WORD:
        iSearchLastLiteralCase_ = false;
        iSearchLastRegexCase_   = true;
        ui.checkIFindRegex->setChecked(false);
        ui.checkIFindCase->setChecked(false);
        break;
    case SEARCH_CASE_SENSE_WORD:
        iSearchLastLiteralCase_ = true;
        iSearchLastRegexCase_   = true;
        ui.checkIFindRegex->setChecked(false);
        ui.checkIFindCase->setChecked(true);
        break;
    case SEARCH_REGEX:
        iSearchLastLiteralCase_ = false;
        iSearchLastRegexCase_   = true;
        ui.checkIFindRegex->setChecked(true);
        ui.checkIFindCase->setChecked(true);
        break;
    case SEARCH_REGEX_NOCASE:
        iSearchLastLiteralCase_ = false;
        iSearchLastRegexCase_   = false;
        ui.checkIFindRegex->setChecked(true);
        ui.checkIFindCase->setChecked(false);
        break;
    default:
        Q_ASSERT(0);
    }
}

/*
** Pop up and clear the incremental search line and prepare to search.
*/
void MainWindow::BeginISearchEx(SearchDirection direction) {

    iSearchStartPos_ = -1;
	ui.editIFind->setText(QLatin1String(""));
    no_signals(ui.checkIFindReverse)->setChecked(direction == SEARCH_BACKWARD);


    /* Note: in contrast to the replace and find dialogs, the regex and
       case toggles are not reset to their default state when the incremental
       search bar is redisplayed. I'm not sure whether this is the best
       choice. If not, an initToggleButtons() call should be inserted
       here. But in that case, it might be appropriate to have different
       default search modes for i-search and replace/find. */

    TempShowISearch(true);
    ui.editIFind->setFocus();
}

/*
** Incremental searching is anchored at the position where the cursor
** was when the user began typing the search string.  Call this routine
** to forget about this original anchor, and if the search bar is not
** permanently up, pop it down.
*/
void MainWindow::EndISearchEx() {

    /* Note: Please maintain this such that it can be freely peppered in
       mainline code, without callers having to worry about performance
       or visual glitches.  */

    // Forget the starting position used for the current run of searches
    iSearchStartPos_ = -1;

    // Mark the end of incremental search history overwriting
    saveSearchHistory(QLatin1String(""), QString(), SEARCH_LITERAL, false);

    // Pop down the search line (if it's not pegged up in Preferences)
    TempShowISearch(false);
}

void MainWindow::action_Shift_Replace_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        DoFindReplaceDlogEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    SEARCH_BACKWARD,
                    GetPrefKeepSearchDlogs(),
                    GetPrefSearch());
    }
}

void MainWindow::on_action_Replace_Find_Again_triggered() {

    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        ReplaceFindSameEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    SEARCH_FORWARD,
                    GetPrefSearchWraps());
    }
}

void MainWindow::action_Shift_Replace_Find_Again_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        ReplaceFindSameEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    SEARCH_BACKWARD,
                    GetPrefSearchWraps());
    }
}

void MainWindow::action_Replace_Again(SearchDirection direction, WrapMode wrap) {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        ReplaceSameEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    direction,
                    wrap);
    }
}

/**
 * @brief MainWindow::on_action_Replace_Again_triggered
 */
void MainWindow::on_action_Replace_Again_triggered() {
    action_Replace_Again(SEARCH_FORWARD, GetPrefSearchWraps());
}

/**
 * @brief MainWindow::action_Shift_Replace_Again_triggered
 */
void MainWindow::action_Shift_Replace_Again_triggered() {
    action_Replace_Again(SEARCH_BACKWARD, GetPrefSearchWraps());
}

/**
 * @brief MainWindow::action_Mark
 * @param mark
 */
void MainWindow::action_Mark(const QString &mark) {
    if (mark.size() != 1 || !safe_ctype<isalpha>(mark[0].toLatin1())) {
        qWarning("NEdit: action requires a single-letter label");
        QApplication::beep();
        return;
    }

    if(auto doc = currentDocument()) {
        doc->markAP(mark[0]);
    }
}

/**
 * @brief MainWindow::on_action_Mark_triggered
 */
void MainWindow::on_action_Mark_triggered() {

    bool ok;
    QString result = QInputDialog::getText(
        this,
        tr("Mark"),
        tr("Enter a single letter label to use for recalling\n"
              "the current selection and cursor position.\n\n"
              "(To skip this dialog, use the accelerator key,\n"
              "followed immediately by a letter key (a-z))"),
        QLineEdit::Normal,
        QString(),
        &ok);

    if(!ok) {
        return;
    }

    action_Mark(result);
}

/**
 * @brief MainWindow::action_Mark_Shortcut_triggered
 */
void MainWindow::action_Mark_Shortcut_triggered() {

    if(auto shortcut = qobject_cast<QShortcut *>(sender())) {
        QKeySequence sequence = shortcut->key();

        switch(sequence[1]) {
        case Qt::Key_A: action_Mark(QLatin1String("A")); break;
        case Qt::Key_B: action_Mark(QLatin1String("B")); break;
        case Qt::Key_C: action_Mark(QLatin1String("C")); break;
        case Qt::Key_D: action_Mark(QLatin1String("D")); break;
        case Qt::Key_E: action_Mark(QLatin1String("E")); break;
        case Qt::Key_F: action_Mark(QLatin1String("F")); break;
        case Qt::Key_G: action_Mark(QLatin1String("G")); break;
        case Qt::Key_H: action_Mark(QLatin1String("H")); break;
        case Qt::Key_I: action_Mark(QLatin1String("I")); break;
        case Qt::Key_J: action_Mark(QLatin1String("J")); break;
        case Qt::Key_K: action_Mark(QLatin1String("K")); break;
        case Qt::Key_L: action_Mark(QLatin1String("L")); break;
        case Qt::Key_M: action_Mark(QLatin1String("M")); break;
        case Qt::Key_N: action_Mark(QLatin1String("N")); break;
        case Qt::Key_O: action_Mark(QLatin1String("O")); break;
        case Qt::Key_P: action_Mark(QLatin1String("P")); break;
        case Qt::Key_Q: action_Mark(QLatin1String("Q")); break;
        case Qt::Key_R: action_Mark(QLatin1String("R")); break;
        case Qt::Key_S: action_Mark(QLatin1String("S")); break;
        case Qt::Key_T: action_Mark(QLatin1String("T")); break;
        case Qt::Key_U: action_Mark(QLatin1String("U")); break;
        case Qt::Key_V: action_Mark(QLatin1String("V")); break;
        case Qt::Key_W: action_Mark(QLatin1String("W")); break;
        case Qt::Key_X: action_Mark(QLatin1String("X")); break;
        case Qt::Key_Y: action_Mark(QLatin1String("Y")); break;
        case Qt::Key_Z: action_Mark(QLatin1String("Z")); break;
        default:
            QApplication::beep();
            break;
        }
    }
}

void MainWindow::action_Goto_Mark(const QString &mark, bool extend) {
    if (mark.size() != 1 || !safe_ctype<isalpha>(mark[0].toLatin1())) {
        qWarning("NEdit: action requires a single-letter label");
        QApplication::beep();
        return;
    }

    if(auto doc = currentDocument()) {
        doc->gotoMarkAP(mark[0], extend);
    }
}

/**
 * @brief MainWindow::action_Goto_Mark_Dialog
 * @param extend
 */
void MainWindow::action_Goto_Mark_Dialog(bool extend) {
    bool ok;
    QString result = QInputDialog::getText(
        this,
        tr("Goto Mark"),
        tr("Enter the single letter label used to mark\n"
                      "the selection and/or cursor position.\n\n"
                      "(To skip this dialog, use the accelerator\n"
                      "key, followed immediately by the letter)"),
        QLineEdit::Normal,
        QString(),
        &ok);

    if(!ok) {
        return;
    }

    action_Goto_Mark(result, extend);
}

/**
 * @brief MainWindow::on_action_Goto_Mark_triggered
 */
void MainWindow::on_action_Goto_Mark_triggered() {

    bool extend = (QApplication::keyboardModifiers() & Qt::SHIFT);
    action_Goto_Mark_Dialog(extend);
}


void MainWindow::action_Shift_Goto_Mark_Shortcut_triggered() {
    if(auto shortcut = qobject_cast<QShortcut *>(sender())) {
        QKeySequence sequence = shortcut->key();

        switch(sequence[1]) {
        case Qt::Key_A: action_Goto_Mark(QLatin1String("A"), true); break;
        case Qt::Key_B: action_Goto_Mark(QLatin1String("B"), true); break;
        case Qt::Key_C: action_Goto_Mark(QLatin1String("C"), true); break;
        case Qt::Key_D: action_Goto_Mark(QLatin1String("D"), true); break;
        case Qt::Key_E: action_Goto_Mark(QLatin1String("E"), true); break;
        case Qt::Key_F: action_Goto_Mark(QLatin1String("F"), true); break;
        case Qt::Key_G: action_Goto_Mark(QLatin1String("G"), true); break;
        case Qt::Key_H: action_Goto_Mark(QLatin1String("H"), true); break;
        case Qt::Key_I: action_Goto_Mark(QLatin1String("I"), true); break;
        case Qt::Key_J: action_Goto_Mark(QLatin1String("J"), true); break;
        case Qt::Key_K: action_Goto_Mark(QLatin1String("K"), true); break;
        case Qt::Key_L: action_Goto_Mark(QLatin1String("L"), true); break;
        case Qt::Key_M: action_Goto_Mark(QLatin1String("M"), true); break;
        case Qt::Key_N: action_Goto_Mark(QLatin1String("N"), true); break;
        case Qt::Key_O: action_Goto_Mark(QLatin1String("O"), true); break;
        case Qt::Key_P: action_Goto_Mark(QLatin1String("P"), true); break;
        case Qt::Key_Q: action_Goto_Mark(QLatin1String("Q"), true); break;
        case Qt::Key_R: action_Goto_Mark(QLatin1String("R"), true); break;
        case Qt::Key_S: action_Goto_Mark(QLatin1String("S"), true); break;
        case Qt::Key_T: action_Goto_Mark(QLatin1String("T"), true); break;
        case Qt::Key_U: action_Goto_Mark(QLatin1String("U"), true); break;
        case Qt::Key_V: action_Goto_Mark(QLatin1String("V"), true); break;
        case Qt::Key_W: action_Goto_Mark(QLatin1String("W"), true); break;
        case Qt::Key_X: action_Goto_Mark(QLatin1String("X"), true); break;
        case Qt::Key_Y: action_Goto_Mark(QLatin1String("Y"), true); break;
        case Qt::Key_Z: action_Goto_Mark(QLatin1String("Z"), true); break;
        default:
            QApplication::beep();
            break;
        }
    }
}

void MainWindow::action_Goto_Mark_Shortcut_triggered() {
    if(auto shortcut = qobject_cast<QShortcut *>(sender())) {
        QKeySequence sequence = shortcut->key();

        switch(sequence[1]) {
        case Qt::Key_A: action_Goto_Mark(QLatin1String("A"), false); break;
        case Qt::Key_B: action_Goto_Mark(QLatin1String("B"), false); break;
        case Qt::Key_C: action_Goto_Mark(QLatin1String("C"), false); break;
        case Qt::Key_D: action_Goto_Mark(QLatin1String("D"), false); break;
        case Qt::Key_E: action_Goto_Mark(QLatin1String("E"), false); break;
        case Qt::Key_F: action_Goto_Mark(QLatin1String("F"), false); break;
        case Qt::Key_G: action_Goto_Mark(QLatin1String("G"), false); break;
        case Qt::Key_H: action_Goto_Mark(QLatin1String("H"), false); break;
        case Qt::Key_I: action_Goto_Mark(QLatin1String("I"), false); break;
        case Qt::Key_J: action_Goto_Mark(QLatin1String("J"), false); break;
        case Qt::Key_K: action_Goto_Mark(QLatin1String("K"), false); break;
        case Qt::Key_L: action_Goto_Mark(QLatin1String("L"), false); break;
        case Qt::Key_M: action_Goto_Mark(QLatin1String("M"), false); break;
        case Qt::Key_N: action_Goto_Mark(QLatin1String("N"), false); break;
        case Qt::Key_O: action_Goto_Mark(QLatin1String("O"), false); break;
        case Qt::Key_P: action_Goto_Mark(QLatin1String("P"), false); break;
        case Qt::Key_Q: action_Goto_Mark(QLatin1String("Q"), false); break;
        case Qt::Key_R: action_Goto_Mark(QLatin1String("R"), false); break;
        case Qt::Key_S: action_Goto_Mark(QLatin1String("S"), false); break;
        case Qt::Key_T: action_Goto_Mark(QLatin1String("T"), false); break;
        case Qt::Key_U: action_Goto_Mark(QLatin1String("U"), false); break;
        case Qt::Key_V: action_Goto_Mark(QLatin1String("V"), false); break;
        case Qt::Key_W: action_Goto_Mark(QLatin1String("W"), false); break;
        case Qt::Key_X: action_Goto_Mark(QLatin1String("X"), false); break;
        case Qt::Key_Y: action_Goto_Mark(QLatin1String("Y"), false); break;
        case Qt::Key_Z: action_Goto_Mark(QLatin1String("Z"), false); break;
        default:
            QApplication::beep();
            break;
        }
    }
}

void MainWindow::on_action_Goto_Matching_triggered() {
    if(auto doc = currentDocument()) {
        doc->GotoMatchingCharacter(lastFocus_);
    }
}

void MainWindow::action_Shift_Goto_Matching_triggered() {
    if(auto doc = currentDocument()) {
        doc->SelectToMatchingCharacter(lastFocus_);
    }
}

void MainWindow::updateTipsFileMenuEx() {

    auto tipsMenu = new QMenu(this);

    for(tagFile &tf : TipsFileList) {
        auto filename = tf.filename;
        QAction *action = tipsMenu->addAction(filename);
        action->setData(filename);
    }

    ui.action_Unload_Calltips_File->setMenu(tipsMenu);
    connect(tipsMenu, SIGNAL(triggered(QAction *)), this, SLOT(unloadTipsFileCB(QAction *)));
}

void MainWindow::updateTagsFileMenuEx() {
    auto tagsMenu = new QMenu(this);

    for(tagFile &tf : TagsFileList) {
        auto filename = tf.filename;
        QAction *action = tagsMenu->addAction(filename);
        action->setData(filename);
    }

    ui.action_Unload_Tags_File->setMenu(tagsMenu);
    connect(tagsMenu, SIGNAL(triggered(QAction *)), this, SLOT(unloadTagsFileCB(QAction *)));
}

void MainWindow::unloadTipsFileCB(QAction *action) {
    auto filename = action->data().toString();
    if(!filename.isEmpty()) {
        action_Unload_Tips_File(filename);
    }
}

void MainWindow::unloadTagsFileCB(QAction *action) {
    auto filename = action->data().toString();
    if(!filename.isEmpty()) {
        action_Unload_Tags_File(filename);
    }
}

void MainWindow::action_Unload_Tips_File(const QString &filename) {

    if (DeleteTagsFileEx(filename, TIP, true)) {
        for(MainWindow *window : MainWindow::allWindows()) {
            window->updateTipsFileMenuEx();
        }
    }
}

void MainWindow::action_Unload_Tags_File(const QString &filename) {

    if (DeleteTagsFileEx(filename, TAG, true)) {
        for(MainWindow *window : MainWindow::allWindows()) {
             window->updateTagsFileMenuEx();
        }
    }
}


void MainWindow::action_Load_Tips_File(const QString &filename) {

    if (!AddTagsFileEx(filename, TIP)) {
        QMessageBox::warning(this, tr("Error Reading File"), tr("Error reading tips file:\n'%1'\ntips not loaded").arg(filename));
    }
}

void MainWindow::action_Load_Tags_File(const QString &filename) {

    if (!AddTagsFileEx(filename, TAG)) {
        QMessageBox::warning(this, tr("Error Reading File"), tr("Error reading ctags file:\n'%1'\ntags not loaded").arg(filename));
    }
}

void MainWindow::on_action_Load_Calltips_File_triggered() {

    if(auto doc = currentDocument()) {
        QString filename = PromptForExistingFileEx(doc->path_, tr("Load Calltips File"));
        if (filename.isNull()) {
            return;
        }

        action_Load_Tips_File(filename);
        updateTipsFileMenuEx();
    }
}

void MainWindow::on_action_Load_Tags_File_triggered() {

    if(auto doc = currentDocument()) {
        QString filename = PromptForExistingFileEx(doc->path_, tr("Load Tags File"));
        if (filename.isNull()) {
            return;
        }

        action_Load_Tags_File(filename);
        updateTagsFileMenuEx();
    }
}

void MainWindow::on_action_Load_Macro_File_triggered() {
    if(auto doc = currentDocument()) {
        QString filename = PromptForExistingFileEx(doc->path_, tr("Load Macro File"));
        if (filename.isNull()) {
            return;
        }

        action_Load_Macro_File(filename);
    }
}

void MainWindow::action_Load_Macro_File(const QString &filename) {
    if(auto doc = currentDocument()) {
		doc->ReadMacroFileEx(filename, true);
    }
}

void MainWindow::on_action_Print_triggered() {
    if(auto doc = currentDocument()) {
        doc->PrintWindow(lastFocus_, false);
    }
}

void MainWindow::on_action_Print_Selection_triggered() {
    if(auto doc = currentDocument()) {
        doc->PrintWindow(lastFocus_, true);
    }
}

void MainWindow::on_action_Split_Pane_triggered() {
    if(auto doc = currentDocument()) {
        doc->splitPane();
        ui.action_Close_Pane->setEnabled(doc->textPanesCount() > 1);
    }
}

void MainWindow::on_action_Close_Pane_triggered() {
    if(auto doc = currentDocument()) {
        doc->closePane();
        ui.action_Close_Pane->setEnabled(doc->textPanesCount() > 1);
    }
}

void MainWindow::on_action_Move_Tab_To_triggered() {
    if(auto doc = currentDocument()) {
        doc->moveDocument(this);
    }
}

void MainWindow::on_action_About_Qt_triggered() {
    QMessageBox::aboutQt(this);
}

DocumentWidget *MainWindow::currentDocument() const {
    return qobject_cast<DocumentWidget *>(ui.tabWidget->currentWidget());
}

DocumentWidget *MainWindow::documentAt(int index) const {
    return qobject_cast<DocumentWidget *>(ui.tabWidget->widget(index));
}

void MainWindow::on_action_Statistics_Line_toggled(bool state) {
    QList<DocumentWidget *> documents = openDocuments();
    for(DocumentWidget *document : documents) {
        document->ShowStatsLine(state);
    }
}

/*
** Turn on and off the continuing display of the incremental search line
** (when off, it is popped up and down as needed via TempShowISearch)
*/
void MainWindow::on_action_Incremental_Search_Line_toggled(bool state) {
    if (showISearchLine_ == state) {
        return;
    }

    showISearchLine_ = state;

    ui.incrementalSearchFrame->setVisible(state);
}

void MainWindow::on_action_Show_Line_Numbers_toggled(bool state) {
   ShowLineNumbers(state);
}

void MainWindow::indentGroupTriggered(QAction *action) {

    if(auto document = currentDocument()) {
        if(action == ui.action_Indent_Off) {
            document->SetAutoIndent(NO_AUTO_INDENT);
		} else if(action == ui.action_Indent_On) {
            document->SetAutoIndent(AUTO_INDENT);
        } else if(action == ui.action_Indent_Smart) {
            document->SetAutoIndent(SMART_INDENT);
        } else {
            qWarning("NEdit: set_auto_indent invalid argument");
        }
    }
}

void MainWindow::wrapGroupTriggered(QAction *action) {
    if(auto document = currentDocument()) {
        if(action == ui.action_Wrap_None) {
            document->SetAutoWrap(NO_WRAP);
        } else if(action == ui.action_Wrap_Auto_Newline) {
            document->SetAutoWrap(NEWLINE_WRAP);
        } else if(action == ui.action_Wrap_Continuous) {
            document->SetAutoWrap(CONTINUOUS_WRAP);
        } else {
            qWarning("NEdit: set_wrap_text invalid argument");
        }
    }
}

void MainWindow::on_action_Wrap_Margin_triggered() {
    if(auto document = currentDocument()) {

		auto dialog = std::make_unique<DialogWrapMargin>(document, this);

        // Set default value
        int margin = document->firstPane()->getWrapMargin();

        dialog->ui.checkWrapAndFill->setChecked(margin == 0);
        dialog->ui.spinWrapAndFill->setValue(margin);

        dialog->exec();
    }
}

void MainWindow::on_action_Tab_Stops_triggered() {
    if(auto document = currentDocument()) {
		auto dialog = std::make_unique<DialogTabs>(document, this);
        dialog->exec();
    }
}

void MainWindow::on_action_Text_Fonts_triggered() {
    if(auto document = currentDocument()) {

        document->dialogFonts_ = new DialogFonts(document, true, this);
        document->dialogFonts_->exec();
        delete document->dialogFonts_;
    }
}

void MainWindow::on_action_Highlight_Syntax_toggled(bool state) {
    if(auto document = currentDocument()) {

        document->highlightSyntax_ = state;

        if (document->highlightSyntax_) {
            StartHighlightingEx(document, true);
        } else {
            document->StopHighlightingEx();
        }
    }
}

void MainWindow::on_action_Apply_Backlighting_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->SetBacklightChars(state ? GetPrefBacklightCharTypes() : QString());
    }
}

void MainWindow::on_action_Make_Backup_Copy_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->saveOldVersion_ = state;
    }
}

void MainWindow::on_action_Incremental_Backup_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->autoSave_ = state;
    }
}

void MainWindow::matchingGroupTriggered(QAction *action) {

    if(auto document = currentDocument()) {
        if(action == ui.action_Matching_Off) {
            document->SetShowMatching(NO_FLASH);
        } else if(action == ui.action_Matching_Delimiter) {
            document->SetShowMatching(FLASH_DELIMIT);
        } else if(action == ui.action_Matching_Range) {
            document->SetShowMatching(FLASH_RANGE);
        } else {
            qWarning("NEdit: Invalid argument for set_show_matching");
        }
    }
}

void MainWindow::on_action_Matching_Syntax_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->matchSyntaxBased_ = state;
    }
}

void MainWindow::on_action_Overtype_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->SetOverstrike(state);
    }
}

void MainWindow::on_action_Read_Only_toggled(bool state) {
    if(auto document = currentDocument()) {
        document->lockReasons_.setUserLocked(state);
        UpdateWindowTitle(document);
        UpdateWindowReadOnly(document);
    }
}

void MainWindow::on_action_Save_Defaults_triggered() {
    SaveNEditPrefsEx(this, false);
}

void MainWindow::on_action_Default_Language_Modes_triggered() {
    auto dialog = new DialogLanguageModes(this);
    dialog->show();
}

void MainWindow::defaultIndentGroupTriggered(QAction *action) {

    if(action == ui.action_Default_Indent_Off) {
        SetPrefAutoIndent(NO_AUTO_INDENT);
    } else if(action == ui.action_Default_Indent_On) {
        SetPrefAutoIndent(AUTO_INDENT);
    } else if(action == ui.action_Default_Indent_Smart) {
        SetPrefAutoIndent(SMART_INDENT);
    } else {
        qWarning("NEdit: invalid default indent");
    }
}

void MainWindow::on_action_Default_Program_Smart_Indent_triggered() {

    if (!SmartIndentDlg) {
        // We pass this document so that the dialog can show the information for the currently
        // active language mode
        if(auto document = currentDocument()) {
            SmartIndentDlg = new DialogSmartIndent(document, this);
        }
    }

    if (LanguageModeName(0).isNull()) {
        QMessageBox::warning(this, tr("Language Mode"), tr("No Language Modes defined"));
        return;
    }

    SmartIndentDlg->show();
    SmartIndentDlg->raise();
}

void MainWindow::defaultWrapGroupTriggered(QAction *action) {

    if(action == ui.action_Default_Wrap_None) {
        SetPrefWrap(NO_WRAP);
    } else if(action == ui.action_Default_Wrap_Auto_Newline) {
        SetPrefWrap(NEWLINE_WRAP);
    } else if(action == ui.action_Default_Wrap_Continuous) {
        SetPrefWrap(CONTINUOUS_WRAP);
    } else {
        qWarning("NEdit: invalid default wrap");
    }
}

void MainWindow::on_action_Default_Wrap_Margin_triggered() {

	auto dialog = std::make_unique<DialogWrapMargin>(nullptr, this);

    // Set default value
    int margin = GetPrefWrapMargin();

    dialog->ui.checkWrapAndFill->setChecked(margin == 0);
    dialog->ui.spinWrapAndFill->setValue(margin);

    dialog->exec();
}

void MainWindow::defaultTagCollisionsGroupTriggered(QAction *action) {
    if(action == ui.action_Default_Tag_Show_All) {
        SetPrefSmartTags(false);
    } else if(action == ui.action_Default_Tag_Smart) {
        SetPrefSmartTags(true);
    } else {
        qWarning("NEdit: invalid default collisions");
    }
}

void MainWindow::on_action_Default_Command_Shell_triggered() {
    bool ok;
    QString shell = QInputDialog::getText(this,
        tr("Command Shell"),
        tr("Enter shell path:"),
        QLineEdit::Normal,
        GetPrefShell(),
        &ok);

    if (ok && !shell.isEmpty()) {

		if(!QFile::exists(shell)) {
            int resp = QMessageBox::warning(this, tr("Command Shell"), tr("The selected shell is not available.\nDo you want to use it anyway?"), QMessageBox::Ok | QMessageBox::Cancel);
            if(resp == QMessageBox::Cancel) {
                return;
            }
        }

        SetPrefShell(shell);
    }
}

void MainWindow::on_action_Default_Tab_Stops_triggered() {
	auto dialog = std::make_unique<DialogTabs>(nullptr, this);
    dialog->exec();
}

void MainWindow::on_action_Default_Text_Fonts_triggered() {
    if(auto document = currentDocument()) {
        document->dialogFonts_ = new DialogFonts(nullptr, false, this);
        document->dialogFonts_->exec();
        delete document->dialogFonts_;
    }
}

void MainWindow::on_action_Default_Colors_triggered() {
    if(auto document = currentDocument()) {
        if(!document->dialogColors_) {
            document->dialogColors_ = new DialogColors(this);
        }

        document->dialogColors_->show();
        document->dialogColors_->raise();
    }
}

/*
** Present a dialog for editing the user specified commands in the shell menu
*/
void MainWindow::on_action_Default_Shell_Menu_triggered() {

    if(!WindowShellMenu) {
        WindowShellMenu = new DialogShellMenu(this);
    }

    WindowShellMenu->show();
    WindowShellMenu->raise();
}

/*
** Present a dialogs for editing the user specified commands in the Macro
** and background menus
*/
void MainWindow::on_action_Default_Macro_Menu_triggered() {

    if(!WindowMacros) {
        WindowMacros = new DialogMacros(this);
    }

    WindowMacros->show();
    WindowMacros->raise();
}

void MainWindow::on_action_Default_Window_Background_Menu_triggered() {

    if(!WindowBackgroundMenu) {
        WindowBackgroundMenu = new DialogWindowBackgroundMenu(this);
    }

    WindowBackgroundMenu->show();
    WindowBackgroundMenu->raise();
}

void MainWindow::on_action_Default_Sort_Open_Prev_Menu_toggled(bool state) {
    /* Set the preference, make the other windows' menus agree,
       and invalidate their Open Previous menus */
    SetPrefSortOpenPrevMenu(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Sort_Open_Prev_Menu)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Show_Path_In_Windows_Menu_toggled(bool state) {

    // Set the preference and make the other windows' menus agree
    SetPrefShowPathInWindowsMenu(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Show_Path_In_Windows_Menu)->setChecked(state);
    }

    InvalidateWindowMenus();
}

void MainWindow::on_action_Default_Customize_Window_Title_triggered() {
    if(auto document = currentDocument()) {
		auto dialog = std::make_unique<DialogWindowTitle>(document, this);
        dialog->exec();
    }
}

void MainWindow::on_action_Default_Search_Verbose_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefSearchDlogs(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Search_Verbose)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Search_Wrap_Around_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefSearchWraps(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Search_Wrap_Around)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Search_Beep_On_Search_Wrap_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefBeepOnSearchWrap(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Search_Beep_On_Search_Wrap)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Search_Keep_Dialogs_Up_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefKeepSearchDlogs(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Search_Keep_Dialogs_Up)->setChecked(state);
    }
}

#if defined(REPLACE_SCOPE)
void MainWindow::defaultReplaceScopeGroupTriggered(QAction *action) {

    if(action == replaceScopeInWindow_) {
        SetPrefReplaceDefScope(REPL_DEF_SCOPE_WINDOW);
        for(MainWindow *window : allWindows()) {
            no_signals(window->replaceScopeInWindow_)->setChecked(true);
        }
    } else if(action == replaceScopeInSelection_) {
        SetPrefReplaceDefScope(REPL_DEF_SCOPE_SELECTION);
        for(MainWindow *window : allWindows()) {
            no_signals(window->replaceScopeInSelection_)->setChecked(true);
        }
    } else if(action == replaceScopeSmart_) {
        SetPrefReplaceDefScope(REPL_DEF_SCOPE_SMART);
        for(MainWindow *window : allWindows()) {
            no_signals(window->replaceScopeSmart_)->setChecked(true);
        }
    }
}
#endif

void MainWindow::defaultSearchGroupTriggered(QAction *action) {

    if(action == ui.action_Default_Search_Literal) {
        SetPrefSearch(SEARCH_LITERAL);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Literal)->setChecked(true);
        }
    } else if(action == ui.action_Default_Search_Literal_Case_Sensitive) {
        SetPrefSearch(SEARCH_CASE_SENSE);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Literal_Case_Sensitive)->setChecked(true);
        }
    } else if(action == ui.action_Default_Search_Literal_Whole_Word) {
        SetPrefSearch(SEARCH_LITERAL_WORD);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Literal_Whole_Word)->setChecked(true);
        }
    } else if(action == ui.action_Default_Search_Literal_Case_Sensitive_Whole_Word) {
        SetPrefSearch(SEARCH_CASE_SENSE_WORD);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Literal_Case_Sensitive_Whole_Word)->setChecked(true);
        }
    } else if(action == ui.action_Default_Search_Regular_Expression) {
        SetPrefSearch(SEARCH_REGEX);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Regular_Expression)->setChecked(true);
        }
    } else if(action == ui.action_Default_Search_Regular_Expresison_Case_Insensitive) {
        SetPrefSearch(SEARCH_REGEX_NOCASE);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Search_Regular_Expresison_Case_Insensitive)->setChecked(true);
        }
    }
}

void MainWindow::defaultSyntaxGroupTriggered(QAction *action) {
    if(action == ui.action_Default_Syntax_Off) {
        SetPrefHighlightSyntax(false);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Syntax_Off)->setChecked(true);
        }
    } else if(action == ui.action_Default_Syntax_On) {
        SetPrefHighlightSyntax(true);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Syntax_On)->setChecked(true);
        }
    }
}

void MainWindow::on_action_Default_Syntax_Recognition_Patterns_triggered() {
    // TODO(eteran): eventually move this logic to be local
    EditHighlightPatterns(this);
}

void MainWindow::on_action_Default_Syntax_Text_Drawing_Styles_triggered() {
    // TODO(eteran): eventually move this logic to be local
    EditHighlightStyles(this, QString());
}

void MainWindow::on_action_Default_Apply_Backlighting_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefBacklightChars(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Apply_Backlighting)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Tab_Open_File_In_New_Tab_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefOpenInTab(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Tab_Open_File_In_New_Tab)->setChecked(state);

        if(!GetPrefOpenInTab()) {
            window->ui.action_New_Window->setText(tr("New &Tab"));
        } else {
            window->ui.action_New_Window->setText(tr("New &Window"));
        }
    }
}

void MainWindow::on_action_Default_Tab_Show_Tab_Bar_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefTabBar(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Tab_Show_Tab_Bar)->setChecked(state);
        window->ui.tabWidget->tabBar()->setVisible(state);
    }
}

void MainWindow::on_action_Default_Tab_Hide_Tab_Bar_When_Only_One_Document_is_Open_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefTabBarHideOne(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Tab_Hide_Tab_Bar_When_Only_One_Document_is_Open)->setChecked(state);
        window->ui.tabWidget->setTabBarAutoHide(state);
    }
}

void MainWindow::on_action_Default_Tab_Next_Prev_Tabs_Across_Windows_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefGlobalTabNavigate(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Tab_Next_Prev_Tabs_Across_Windows)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Tab_Sort_Tabs_Alphabetically_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefSortTabs(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Tab_Sort_Tabs_Alphabetically)->setChecked(state);
    }

    /* If we just enabled sorting, sort all tabs.  Note that this reorders
       the next pointers underneath us, which is scary, but SortTabBar never
       touches windows that are earlier in the window list so it's ok. */
    if (state) {
        for(MainWindow *window : allWindows()) {
            window->SortTabBar();
        }
    }
}



void MainWindow::on_action_Default_Show_Tooltips_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefToolTips(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Show_Tooltips)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Statistics_Line_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefStatsLine(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Statistics_Line)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Incremental_Search_Line_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefISearchLine(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Incremental_Search_Line)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Show_Line_Numbers_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefLineNums(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Show_Line_Numbers)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Make_Backup_Copy_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefSaveOldVersion(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Make_Backup_Copy)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Incremental_Backup_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefAutoSave(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Incremental_Backup)->setChecked(state);
    }
}

void MainWindow::defaultMatchingGroupTriggered(QAction *action) {
    if(action == ui.action_Default_Matching_Off) {
        SetPrefShowMatching(NO_FLASH);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Matching_Off)->setChecked(true);
        }
    } else if(action == ui.action_Default_Matching_Delimiter) {
        SetPrefShowMatching(FLASH_DELIMIT);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Matching_Delimiter)->setChecked(true);
        }
    } else if(action == ui.action_Default_Matching_Range) {
        SetPrefShowMatching(FLASH_RANGE);
        for(MainWindow *window : allWindows()) {
            no_signals(window->ui.action_Default_Matching_Range)->setChecked(true);
        }
    }
}

void MainWindow::on_action_Default_Matching_Syntax_Based_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefMatchSyntaxBased(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Matching_Syntax_Based)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Terminate_with_Line_Break_on_Save_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefAppendLF(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Terminate_with_Line_Break_on_Save)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Popups_Under_Pointer_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefRepositionDialogs(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Popups_Under_Pointer)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Auto_Scroll_Near_Window_Top_Bottom_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefAutoScroll(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Auto_Scroll_Near_Window_Top_Bottom)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Warnings_Files_Modified_Externally_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefWarnFileMods(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Warnings_Files_Modified_Externally)->setChecked(state);
        no_signals(window->ui.action_Default_Warnings_Check_Modified_File_Contents)->setEnabled(GetPrefWarnFileMods());
    }
}

void MainWindow::on_action_Default_Warnings_Check_Modified_File_Contents_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefWarnRealFileMods(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Warnings_Check_Modified_File_Contents)->setChecked(state);
    }
}

void MainWindow::on_action_Default_Warnings_On_Exit_toggled(bool state) {
    // Set the preference and make the other windows' menus agree
    SetPrefWarnExit(state);
    for(MainWindow *window : allWindows()) {
        no_signals(window->ui.action_Default_Warnings_On_Exit)->setChecked(state);
    }
}

void MainWindow::defaultSizeGroupTriggered(QAction *action) {
    if(action == ui.action_Default_Size_24_x_80) {
        setWindowSizeDefault(24, 80);
    } else if(action == ui.action_Default_Size_40_x_80) {
        setWindowSizeDefault(40, 80);
    } else if(action == ui.action_Default_Size_60_x_80) {
        setWindowSizeDefault(60, 80);
    } else if(action == ui.action_Default_Size_80_x_80) {
        setWindowSizeDefault(80, 80);
    } else if(action == ui.action_Default_Size_Custom) {
		auto dialog = std::make_unique<DialogWindowSize>(this);
        dialog->exec();
        updateWindowSizeMenus();
    }
}

void MainWindow::setWindowSizeDefault(int rows, int cols) {
    SetPrefRows(rows);
    SetPrefCols(cols);
    updateWindowSizeMenus();
}

void MainWindow::updateWindowSizeMenus() {
    for(MainWindow *window : allWindows()) {
        window->updateWindowSizeMenu();
    }
}

void MainWindow::updateWindowSizeMenu() {
    const int rows = GetPrefRows();
    const int cols = GetPrefCols();

    no_signals(ui.action_Default_Size_24_x_80)->setChecked(rows == 24 && cols == 80);
    no_signals(ui.action_Default_Size_40_x_80)->setChecked(rows == 40 && cols == 80);
    no_signals(ui.action_Default_Size_60_x_80)->setChecked(rows == 60 && cols == 80);
    no_signals(ui.action_Default_Size_80_x_80)->setChecked(rows == 80 && cols == 80);

    if ((rows != 24 && rows != 40 && rows != 60 && rows != 80) || cols != 80) {
        no_signals(ui.action_Default_Size_Custom)->setChecked(true);
        ui.action_Default_Size_Custom->setText(tr("Custom... (%1 x %2)").arg(rows).arg(cols));
    } else {
        ui.action_Default_Size_Custom->setText(tr("Custom..."));
    }
}

void MainWindow::action_Next_Document() {

    bool crossWindows = GetPrefGlobalTabNavigate();
    int currentIndex  = ui.tabWidget->currentIndex();
    int nextIndex     = currentIndex + 1;
    int tabCount      = ui.tabWidget->count();

    if(!crossWindows) {
        if(nextIndex == tabCount) {
            ui.tabWidget->setCurrentIndex(0);
        } else {
            ui.tabWidget->setCurrentIndex(nextIndex);
        }
    } else {
        if(nextIndex == tabCount) {

			QList<MainWindow *> windows = MainWindow::allWindows();
			int thisIndex  = windows.indexOf(this);
            int nextIndex = (thisIndex + 1);
			
            if(nextIndex == windows.size()) {
                nextIndex = 0;
			}

            // raise the window set the focus to the first document in it
            MainWindow *nextWindow = windows[nextIndex];
            QWidget *firstWidget = nextWindow->ui.tabWidget->widget(0);

            Q_ASSERT(qobject_cast<DocumentWidget *>(firstWidget));

            if(auto document = qobject_cast<DocumentWidget *>(firstWidget)) {
                document->RaiseFocusDocumentWindow(true);
                nextWindow->ui.tabWidget->setCurrentWidget(document);
            }
			
        } else {
            ui.tabWidget->setCurrentIndex(nextIndex);
        }	
	}
}

void MainWindow::action_Prev_Document() {
    bool crossWindows = GetPrefGlobalTabNavigate();
    int currentIndex  = ui.tabWidget->currentIndex();
    int prevIndex     = currentIndex - 1;
    int tabCount      = ui.tabWidget->count();

    if(!crossWindows) {
        if(currentIndex == 0) {
            ui.tabWidget->setCurrentIndex(tabCount - 1);
        } else {
            ui.tabWidget->setCurrentIndex(prevIndex);
        }
    } else {
        if(currentIndex == 0) {

            QList<MainWindow *> windows = MainWindow::allWindows();
            int thisIndex  = windows.indexOf(this);
            int nextIndex = (thisIndex - 1);

            if(thisIndex == 0) {
                nextIndex = windows.size() - 1;
            }

            // raise the window set the focus to the first document in it
            MainWindow *nextWindow = windows[nextIndex];
            QWidget *lastWidget = nextWindow->ui.tabWidget->widget(nextWindow->ui.tabWidget->count() - 1);

            Q_ASSERT(qobject_cast<DocumentWidget *>(lastWidget));

            if(auto document = qobject_cast<DocumentWidget *>(lastWidget)) {
                document->RaiseFocusDocumentWindow(true);
                nextWindow->ui.tabWidget->setCurrentWidget(document);
            }

        } else {
            ui.tabWidget->setCurrentIndex(prevIndex);
        }
    }
}

void MainWindow::action_Last_Document() {

    if(lastFocusDocument) {
        lastFocusDocument->RaiseFocusDocumentWindow(true);
    }
}

DocumentWidget *MainWindow::EditNewFileEx(MainWindow *inWindow, QString geometry, bool iconic, const QString &languageMode, const QString &defaultPath) {

    DocumentWidget *document;

    /*... test for creatability? */

    // Find a (relatively) unique name for the new file
    QString name = MainWindow::UniqueUntitledNameEx();

    // create new window/document
    if (inWindow) {
        document = inWindow->CreateDocument(name);
    } else {
        auto win = new MainWindow();
        document = win->CreateDocument(name);

        win->parseGeometry(geometry);

        if(iconic) {
            win->showMinimized();
        } else {
            win->show();
        }

        inWindow = win;
    }


    document->filename_ = name;
    document->path_     = !defaultPath.isEmpty() ? defaultPath : GetCurrentDirEx();

    // do we have a "/" at the end? if not, add one
    if (!document->path_.isEmpty() && !document->path_.endsWith(QLatin1Char('/'))) {
        document->path_.append(QLatin1Char('/'));
    }

    document->SetWindowModified(false);
    document->lockReasons_.clear();
    inWindow->UpdateWindowReadOnly(document);
    document->UpdateStatsLine(nullptr);
    inWindow->UpdateWindowTitle(document);
    document->RefreshTabState();


    if(languageMode.isNull()) {
        document->DetermineLanguageMode(true);
    } else {
        document->SetLanguageMode(FindLanguageMode(languageMode), true);
    }

    if (iconic && inWindow->isMinimized()) {
        document->RaiseDocument();
    } else {
        document->RaiseDocumentWindow();
    }

    inWindow->SortTabBar();
    return document;
}

void MainWindow::AllWindowsBusyEx(const QString &message) {

    if (!currentlyBusy) {
        busyStartTime = getRelTimeInTenthsOfSeconds();
        modeMessageSet = false;

        for(DocumentWidget *document : DocumentWidget::allDocuments()) {
            /* We don't the display message here yet, but defer it for
               a while. If the wait is short, we don't want
               to have it flash on and off the screen.  However,
               we can't use a time since in generally we are in
               a tight loop and only processing exposure events, so it's
               up to the caller to make sure that this routine is called
               at regular intervals.
            */
            document->setCursor(Qt::WaitCursor);
        }

    } else if (!modeMessageSet && !message.isNull() && getRelTimeInTenthsOfSeconds() - busyStartTime > 10) {

        // Show the mode message when we've been busy for more than a second
        for(DocumentWidget *document : DocumentWidget::allDocuments()) {
            document->SetModeMessageEx(message);
        }
        modeMessageSet = true;
    }

    BusyWaitEx();
    currentlyBusy = true;
}


void MainWindow::AllWindowsUnbusyEx() {

    for(DocumentWidget *document : DocumentWidget::allDocuments()) {
        document->ClearModeMessageEx();
        document->setCursor(Qt::ArrowCursor);
    }

    currentlyBusy  = false;
    modeMessageSet = false;
    busyStartTime  = 0;
}

void MainWindow::BusyWaitEx() {
    static const int timeout = 100000; /* 1/10 sec = 100 ms = 100,000 us */
    static struct timeval last = {0, 0};
    struct timeval current;
    gettimeofday(&current, nullptr);

    if ((current.tv_sec != last.tv_sec) || (current.tv_usec - last.tv_usec > timeout)) {
        QApplication::processEvents();
        last = current;
    }
}

void MainWindow::on_action_Save_triggered() {
    if(auto document = currentDocument()) {
        if (document->CheckReadOnly()) {
            return;
        }

        document->SaveWindow();
    }
}

/*
** Wrapper for HandleCustomNewFileSB which uses the current window's path
** (if set) as the default directory, and asks about embedding newlines
** to make wrapping permanent.
*/
QString MainWindow::PromptForNewFileEx(DocumentWidget *document, const QString &prompt, FileFormats *fileFormat, bool *addWrap) {

    *fileFormat = document->fileFormat_;

    QString filename;

    QFileDialog dialog(this, prompt);

    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(document->path_);
    dialog.setOptions(QFileDialog::DontUseNativeDialog);

    if(QGridLayout* const layout = qobject_cast<QGridLayout*>(dialog.layout())) {
        if(layout->rowCount() == 4 && layout->columnCount() == 3) {
            auto boxLayout = new QBoxLayout(QBoxLayout::LeftToRight);

            auto unixCheck = new QRadioButton(tr("&Unix"));
            auto dosCheck  = new QRadioButton(tr("D&OS"));
            auto macCheck  = new QRadioButton(tr("&Macintosh"));

            switch(document->fileFormat_) {
            case DOS_FILE_FORMAT:
                dosCheck->setChecked(true);
                break;
            case MAC_FILE_FORMAT:
                macCheck->setChecked(true);
                break;
            case UNIX_FILE_FORMAT:
                unixCheck->setChecked(true);
                break;
            }

            auto group = new QButtonGroup();
            group->addButton(unixCheck);
            group->addButton(dosCheck);
            group->addButton(macCheck);

            boxLayout->addWidget(unixCheck);
            boxLayout->addWidget(dosCheck);
            boxLayout->addWidget(macCheck);

            int row = layout->rowCount();

            layout->addWidget(new QLabel(tr("Format: ")), row, 0, 1, 1);
            layout->addLayout(boxLayout, row, 1, 1, 1, Qt::AlignLeft);

            ++row;

            auto wrapCheck = new QCheckBox(tr("&Add line breaks where wrapped"));
            if(*addWrap) {
                wrapCheck->setChecked(true);
            }

            QObject::connect(wrapCheck, &QCheckBox::toggled, [wrapCheck, this](bool checked) {
                if(checked) {
                    int ret = QMessageBox::information(this, tr("Add Wrap"),
                        tr("This operation adds permanent line breaks to\n"
                        "match the automatic wrapping done by the\n"
                        "Continuous Wrap mode Preferences Option.\n\n"
                        "*** This Option is Irreversable ***\n\n"
                        "Once newlines are inserted, continuous wrapping\n"
                        "will no longer work automatically on these lines"),
                        QMessageBox::Ok, QMessageBox::Cancel);

                    if(ret != QMessageBox::Ok) {
                        wrapCheck->setChecked(false);
                    }
                }
            });


            if (document->wrapMode_ == CONTINUOUS_WRAP) {
                layout->addWidget(wrapCheck, row, 1, 1, 1);
            }

            if(dialog.exec()) {
                if(dosCheck->isChecked()) {
                    document->fileFormat_ = DOS_FILE_FORMAT;
                } else if(macCheck->isChecked()) {
                    document->fileFormat_ = MAC_FILE_FORMAT;
                } else if(unixCheck->isChecked()) {
                    document->fileFormat_ = UNIX_FILE_FORMAT;
                }

                *addWrap = wrapCheck->isChecked();
                filename = dialog.selectedFiles()[0];
            }

        }
    }

    return filename;
}

void MainWindow::action_Save_As(const QString &filename, bool wrapped) {
	if(auto document = currentDocument()) {
		document->SaveWindowAs(filename, wrapped);
	}
}

/**
 * @brief MainWindow::on_action_Save_As_triggered
 */
void MainWindow::on_action_Save_As_triggered() {
    if(auto document = currentDocument()) {

		bool addWrap = false;
        FileFormats fileFormat;

        QString fullname = PromptForNewFileEx(document, tr("Save File As"), &fileFormat, &addWrap);
        if (fullname.isNull()) {
            return;
        }

        document->fileFormat_ = fileFormat;
		action_Save_As(fullname, addWrap);
    }
}

void MainWindow::on_action_Revert_to_Saved_triggered() {

    if(auto document = currentDocument()) {
        // re-reading file is irreversible, prompt the user first
        if (document->fileChanged_) {

			int result = QMessageBox::question(
			            this,
			            tr("Discard Changes"),
			            tr("Discard changes to\n%1%2?").arg(document->path_, document->filename_),
			            QMessageBox::Ok | QMessageBox::Cancel
			            );
			if(result == QMessageBox::Cancel) {
				return;
			}
        } else {

			QMessageBox messageBox(this);
            messageBox.setWindowTitle(tr("Reload File"));
            messageBox.setIcon(QMessageBox::Question);
            messageBox.setText(tr("Re-load file\n%1%2?").arg(document->path_, document->filename_));
            QPushButton *buttonOk   = messageBox.addButton(tr("Re-read"), QMessageBox::AcceptRole);
            QPushButton *buttonCancel = messageBox.addButton(QMessageBox::Cancel);
            Q_UNUSED(buttonOk);

            messageBox.exec();
            if(messageBox.clickedButton() == buttonCancel) {
                return;
            }
        }

        document->RevertToSaved();
    }
}

//------------------------------------------------------------------------------
// Name: on_action_New_Window_triggered
// Desc: whatever the setting is for what to do with "New",
//       this does the opposite
//------------------------------------------------------------------------------
void MainWindow::on_action_New_Window_triggered() {
    if(auto document = currentDocument()) {
        MainWindow::EditNewFileEx(GetPrefOpenInTab() ? nullptr : this, QString(), false, QString(), document->path_);
        CheckCloseDimEx();
    }
}

//------------------------------------------------------------------------------
// Name: on_action_Exit_triggered
//------------------------------------------------------------------------------
void MainWindow::on_action_Exit_triggered() {

    QList<DocumentWidget *> documents = DocumentWidget::allDocuments();

    if (!CheckPrefsChangesSavedEx()) {
        return;
    }

    /* If this is not the last window (more than one window is open),
       confirm with the user before exiting. */

    // NOTE(eteran): test if the current window is NOT the only window
    if (GetPrefWarnExit() && !(documents.size() < 2)) {

        auto exitMsg = tr("Editing: ");

        /* List the windows being edited and make sure the
           user really wants to exit */
        // This code assembles a list of document names being edited and elides as necessary
        for(int i = 0; i < documents.size(); ++i) {
            DocumentWidget *const document  = documents[i];

            auto filename = tr("%1%2").arg(document->filename_, document->fileChanged_ ? tr("*") : tr(""));

            constexpr int DF_MAX_MSG_LENGTH = 2048;

            if (exitMsg.size() + filename.size() + 30 >= DF_MAX_MSG_LENGTH) {
                exitMsg.append(tr("..."));
                break;
            }

            // NOTE(eteran): test if this is the last window
            if (i == (documents.size() - 1)) {
                exitMsg.append(tr("and %1.").arg(filename));
            } else {
                exitMsg.append(tr("%1, ").arg(filename));
            }
        }

        exitMsg.append(tr("\n\nExit NEdit?"));

        QMessageBox messageBox(this);
        messageBox.setWindowTitle(tr("Exit"));
        messageBox.setIcon(QMessageBox::Question);
        messageBox.setText(exitMsg);
        QPushButton *buttonExit   = messageBox.addButton(tr("Exit"), QMessageBox::AcceptRole);
        QPushButton *buttonCancel = messageBox.addButton(QMessageBox::Cancel);
        Q_UNUSED(buttonExit);

        messageBox.exec();
        if(messageBox.clickedButton() == buttonCancel) {
            return;
        }
    }

    // Close all files and exit when the last one is closed
    if (CloseAllFilesAndWindowsEx()) {
        QApplication::quit();
    }
}

/*
** Check if preferences have changed, and if so, ask the user if he wants
** to re-save.  Returns False if user requests cancelation of Exit (or whatever
** operation triggered this call to be made).
*/
bool MainWindow::CheckPrefsChangesSavedEx() {

    if (!PrefsHaveChanged) {
        return true;
    }

    QMessageBox messageBox(this);
    messageBox.setWindowTitle(tr("Default Preferences"));
    messageBox.setIcon(QMessageBox::Question);

    messageBox.setText((ImportedFile.isNull())
        ? tr("Default Preferences have changed.\nSave changes to NEdit preference file?")
        : tr("Default Preferences have changed.  SAVING \nCHANGES WILL INCORPORATE ADDITIONAL\nSETTINGS FROM FILE: %s").arg(ImportedFile));

    QPushButton *buttonSave     = messageBox.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton *buttonDontSave = messageBox.addButton(tr("Don't Save"), QMessageBox::AcceptRole);
    QPushButton *buttonCancel   = messageBox.addButton(QMessageBox::Cancel);
    Q_UNUSED(buttonCancel);

    messageBox.exec();
    if(messageBox.clickedButton() == buttonSave) {
        SaveNEditPrefsEx(this, true);
        return true;
    } else if(messageBox.clickedButton() == buttonDontSave) {
        return true;
    } else {
        return false;
    }

}

/*
** close all the documents in a window
*/
bool MainWindow::CloseAllDocumentInWindow() {

    if (TabCount() == 1) {
        // only one document in the window
        if(DocumentWidget *document = currentDocument()) {
            return document->CloseFileAndWindow(CloseMode::Prompt);
        }
    } else {

        // close all _modified_ documents belong to this window
        for(DocumentWidget *document : openDocuments()) {
            if (document->fileChanged_) {
                if (!document->CloseFileAndWindow(CloseMode::Prompt)) {
                    return false;
                }
            }
        }

        // if there's still documents left in the window...
        for(DocumentWidget *document : openDocuments()) {
            if (!document->CloseFileAndWindow(CloseMode::Prompt)) {
                return false;
            }
        }
    }

    return true;
}

void MainWindow::closeEvent(QCloseEvent *event) {


    QList<MainWindow *> windows = MainWindow::allWindows();
    if(windows.size() == 1) {
        // this is only window, then this is the same as exit
        event->ignore();
        on_action_Exit_triggered();

    } else {

        if (TabCount() == 1) {
            if(DocumentWidget *document = currentDocument()) {
                document->CloseFileAndWindow(CloseMode::Prompt);
            }
        } else {
            int resp = QMessageBox::Cancel;
            if (GetPrefWarnExit()) {
                // TODO(eteran): 2.0 this is probably better off with "Ok" "Cancel", but we are being consistant with the original UI for now
                resp = QMessageBox::question(this, tr("Close Window"), tr("Close ALL documents in this window?"), QMessageBox::Cancel, QMessageBox::Close);
            }

            if (resp == QMessageBox::Close) {
                CloseAllDocumentInWindow();
                delete this;
                event->accept();
            } else {
                event->ignore();
            }
        }
    }
}

void MainWindow::on_action_Execute_Command_Line_triggered() {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        doc->ExecCursorLineEx(lastFocus_, false);
    }
}

void MainWindow::on_action_Cancel_Shell_Command_triggered() {
    if(auto doc = currentDocument()) {
        doc->AbortShellCommandEx();
    }
}


void MainWindow::on_action_Learn_Keystrokes_triggered() {
    if(auto doc = currentDocument()) {
		doc->BeginLearnEx();
    }
}

void MainWindow::on_action_Finish_Learn_triggered() {
    if(auto doc = currentDocument()) {
        Q_UNUSED(doc);
        FinishLearnEx();
    }
}

void MainWindow::on_action_Replay_Keystrokes_triggered() {
    if(auto doc = currentDocument()) {
        ReplayEx(doc);
    }
}

void MainWindow::on_action_Cancel_Learn_triggered() {
    if(auto doc = currentDocument()) {
        CancelMacroOrLearnEx(doc);
    }
}

/*
** Close all files and windows, leaving one untitled window
*/
bool MainWindow::CloseAllFilesAndWindowsEx() {

    for(MainWindow *window : MainWindow::allWindows()) {

        // NOTE(eteran): the original code took measures to ensure that
        // if this was being called from a macro, that the window which
        // the macro was running in would be closed last to avoid an infinite
        // loop due to closing modifying the list of windows while it was being
        // iterated. Since this code operates on a COPY of the list of windows
        // I don't think such measures are necessary anymore. But I will
        // include the original comment just in case..
        //
        /*
         * When we're exiting through a macro, the document running the
         * macro does not disappear from the list, so we could get stuck
         * in an endless loop if we try to close it. Therefore, we close
         * other documents first. (Note that the document running the macro
         * may get closed because it is in the same window as another
         * document that gets closed, but it won't disappear; it becomes
         * Untitled.)
         */

        if (!window->CloseAllDocumentInWindow()) {
            return false;
        }

        delete window;
    }

    return true;
}

void MainWindow::on_action_Repeat_triggered() {

    QString LastCommand = CommandRecorder::getInstance()->lastCommand;

    if(LastCommand.isNull()) {
        QMessageBox::warning(this, tr("Repeat Macro"), tr("No previous commands or learn/replay sequences to repeat"));
        return;
    }

    int index = LastCommand.indexOf(QLatin1Char('('));
    if(index == -1) {
        return;
    }

    auto dialog = new DialogRepeat(currentDocument(), this);
    dialog->setCommand(LastCommand);
    dialog->show();
}


void MainWindow::focusChanged(QWidget *from, QWidget *to) {


    if(auto area = qobject_cast<TextArea *>(from)) {
        if(auto document = DocumentWidget::documentFrom(area)) {
            lastFocusDocument = document;
        }
    }

    if(auto area = qobject_cast<TextArea *>(to)) {
        if(auto document = DocumentWidget::documentFrom(area)) {
            // record which window pane last had the keyboard focus
            lastFocus_ = area;

            // update line number statistic to reflect current focus pane
            document->UpdateStatsLine(area);

            // finish off the current incremental search
            EndISearchEx();

            // Check for changes to read-only status and/or file modifications
            document->CheckForChangesToFileEx();
        }
    }
}

void MainWindow::on_action_Help_triggered() {
    Help::displayTopic(Help::Topic::HELP_START);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {

    if(qobject_cast<QTabBar*>(object)) {
        if(event->type() == QEvent::ToolTip) {
            if(!GetPrefToolTips()) {
                return true;
            }
        }
    }

    return false;
}

/*
** Dim/undim buttons for pasting replay macros into macro and bg menu dialogs
*/
void MainWindow::DimPasteReplayBtns(bool enabled) {

    if(WindowMacros) {
        WindowMacros->setPasteReplayEnabled(enabled);
    }

    if(WindowBackgroundMenu) {
        WindowBackgroundMenu->setPasteReplayEnabled(enabled);
    }
}

/**
 * @brief MainWindow::action_Find
 * @param string
 * @param direction
 * @param keepDialogs
 * @param type
 */
void MainWindow::action_Find(const QString &string, SearchDirection direction, SearchType type, WrapMode searchWrap) {

    SearchAndSelectEx(
        this,
        currentDocument(),
        lastFocus_,
        direction,
        string,
        type,
        searchWrap);
}

/**
 * @brief MainWindow::action_Find
 * @param string
 * @param direction
 * @param keepDialogs
 * @param type
 */
void MainWindow::action_Find_Dialog(SearchDirection direction, SearchType type, bool keepDialog) {
    DoFindDlogEx(
        this,
        currentDocument(),
        direction,
        keepDialog,
        type);
}

/**
 * @brief MainWindow::on_action_Find_triggered
 */
void MainWindow::on_action_Find_triggered() {
    action_Find_Dialog(
        SEARCH_FORWARD,
        GetPrefSearch(),
        GetPrefKeepSearchDlogs());
}

/**
 * @brief MainWindow::action_FindAgain
 * @param direction
 * @param wrap
 */
void MainWindow::action_Find_Again(SearchDirection direction, WrapMode wrap) {
    SearchAndSelectSameEx(
        this,
        currentDocument(),
        lastFocus_,
        direction,
        wrap);
}

/**
 * @brief MainWindow::on_action_Find_Again_triggered
 */
void MainWindow::on_action_Find_Again_triggered() {
    action_Find_Again(SEARCH_FORWARD, GetPrefSearchWraps());
}

/**
 * @brief MainWindow::action_Find_Selection
 */
void MainWindow::action_Find_Selection(SearchDirection direction, SearchType type, WrapMode wrap) {
    SearchForSelectedEx(
        this,
        currentDocument(),
        lastFocus_,
        direction,
        type,
        wrap);
}


/**
 * @brief MainWindow::on_action_Find_Selection_triggered
 */
void MainWindow::on_action_Find_Selection_triggered() {
    action_Find_Selection(
                SEARCH_FORWARD,
                GetPrefSearch(),
                GetPrefSearchWraps());
}

/**
 * @brief MainWindow::action_Replace
 * @param direction
 * @param searchString
 * @param replaceString
 * @param type
 * @param wrap
 */
void MainWindow::action_Replace(SearchDirection direction, const QString &searchString, const QString &replaceString, SearchType type, WrapMode wrap) {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        SearchAndReplaceEx(
                    this,
                    doc,
                    lastFocus_,
                    direction,
                    searchString,
                    replaceString,
                    type,
                    wrap);
    }
}

/**
 * @brief action_Replace_Dialog
 * @param direction
 * @param type
 * @param keepDialog
 */
void MainWindow::action_Replace_Dialog(SearchDirection direction, SearchType type, bool keepDialog) {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        DoFindReplaceDlogEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    direction,
                    keepDialog,
                    type);
    }
}

/**
 * @brief MainWindow::on_action_Replace_triggered
 */
void MainWindow::on_action_Replace_triggered() {
    action_Replace_Dialog(SEARCH_FORWARD, GetPrefSearch(), GetPrefKeepSearchDlogs());
}

/**
 * @brief MainWindow::action_Replace_All
 * @param searchString
 * @param replaceString
 * @param type
 */
void MainWindow::action_Replace_All(const QString &searchString, const QString &replaceString, SearchType type) {
    if(auto doc = currentDocument()) {
        if (doc->CheckReadOnly()) {
            return;
        }

        ReplaceAllEx(
                    this,
                    currentDocument(),
                    lastFocus_,
                    searchString,
                    replaceString,
                    type);
    }
}

void MainWindow::action_Show_Tip(const QString &argument) {
    if(auto doc = currentDocument()) {
        doc->FindDefCalltip(lastFocus_, argument);
    }
}

/**
 * @brief MainWindow::action_Find_Definition
 * @param argument
 */
void MainWindow::action_Find_Definition(const QString &argument) {
     if(auto doc = currentDocument()) {
         doc->FindDefinition(lastFocus_, argument);
     }
}

/**
 * @brief MainWindow::on_action_Find_Definition_triggered
 */
void MainWindow::on_action_Find_Definition_triggered() {
    action_Find_Definition(QString());
}

/**
 * @brief MainWindow::on_action_Show_Calltip_triggered
 */
void MainWindow::on_action_Show_Calltip_triggered() {
    action_Show_Tip(QString());
}

/**
 * @brief MainWindow::on_action_Filter_Selection_triggered
 */
void MainWindow::on_action_Filter_Selection_triggered() {

    // NOTE(eteran): if we change the order of operations here,
    // then we can remove the redundancies with this and action_Filter_Selection
    if(auto doc = currentDocument()) {
        static DialogFilter *dialog = nullptr;

        if (doc->CheckReadOnly()) {
            return;
        }

        if (!doc->buffer_->primary_.selected) {
            QApplication::beep();
            return;
        }

        if(!dialog) {
            dialog = new DialogFilter(this);
        }

        int r = dialog->exec();
        if(!r) {
            return;
        }

        QString filterText = dialog->ui.textFilter->text();
        action_Filter_Selection(filterText);
    }
}

/**
 * @brief action_Filter_Selection
 * @param filter
 */
void MainWindow::action_Filter_Selection(const QString &filter) {
    if(auto doc = currentDocument()) {

        if (doc->CheckReadOnly()) {
            return;
        }

        if (!doc->buffer_->primary_.selected) {
            QApplication::beep();
            return;
        }

        if(!filter.isEmpty()) {
            doc->filterSelection(filter);
        }
    }
}

/**
 * @brief MainWindow::action_Execute_Command
 * @param command
 */
void MainWindow::action_Execute_Command(const QString &command) {
    if(auto doc = currentDocument()) {

        if (doc->CheckReadOnly())
            return;

        if(!command.isEmpty()) {
            doc->execAP(lastFocus_, command);
        }
    }
}

/**
 * @brief MainWindow::on_action_Execute_Command_triggered
 */
void MainWindow::on_action_Execute_Command_triggered() {

    // NOTE(eteran): if we change the order of operations here,
    // then we can remove the redundancies with this and action_Execute_Command
    if(auto doc = currentDocument()) {
        static DialogExecuteCommand *dialog = nullptr;

        if (doc->CheckReadOnly())
            return;

        if(!dialog) {
            dialog = new DialogExecuteCommand(this);
        }

        int r = dialog->exec();
        if(!r) {
            return;
        }

        QString commandText = dialog->ui.textCommand->text();
        action_Execute_Command(commandText);
    }
}

/**
 * @brief MainWindow::shellTriggered
 * @param action
 */
void MainWindow::shellTriggered(QAction *action) {
    const int index = action->data().toInt();
    const QString name = ShellMenuData[index].item->name;
    action_Shell_Menu_Command(name);
}

/**
 * @brief MainWindow::action_Shell_Menu_Command
 * @param command
 */
void MainWindow::action_Shell_Menu_Command(const QString &name) {
    if(auto doc = currentDocument()) {
        doc->DoNamedShellMenuCmd(lastFocus_, name, false);
    }
}

void MainWindow::macroTriggered(QAction *action) {

    // TODO(eteran): implement what this comment says!
    /* Don't allow users to execute a macro command from the menu (or accel)
       if there's already a macro command executing, UNLESS the macro is
       directly called from another one.  NEdit can't handle
       running multiple, independent uncoordinated, macros in the same
       window.  Macros may invoke macro menu commands recursively via the
       macro_menu_command action proc, which is important for being able to
       repeat any operation, and to embed macros within eachother at any
       level, however, a call here with a macro running means that THE USER
       is explicitly invoking another macro via the menu or an accelerator,
       UNLESS the macro event marker is set */

    const int index = action->data().toInt();
    const QString name = MacroMenuData[index].item->name;
    action_Macro_Menu_Command(name);
}

/**
 * @brief MainWindow::action_Macro_Menu_Command
 * @param name
 */
void MainWindow::action_Macro_Menu_Command(const QString &name) {

    if(auto doc = currentDocument()) {
        doc->DoNamedMacroMenuCmd(lastFocus_, name, false);
    }
}

/**
 * @brief MainWindow::action_Repeat_Macro
 * @param macro
 * @param how
 */
void MainWindow::action_Repeat_Macro(const QString &macro, int how) {
    if(auto doc = currentDocument()) {
        doc->repeatMacro(macro, how);
    }
}

void MainWindow::on_action_Detach_Tab_triggered() {
    if(auto doc = currentDocument()) {
        action_Detach_Document(doc);
    }
}

void MainWindow::action_Detach_Document(DocumentWidget *document) {
    if(TabCount() > 1) {
        auto new_window = new MainWindow(nullptr);

        new_window->ui.tabWidget->addTab(document, document->filename_);
        new_window->show();
    }
}
