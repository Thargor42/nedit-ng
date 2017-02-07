/*******************************************************************************
*                                                                              *
* preference.h -- Nirvana Editor Preferences Header File                       *
*                                                                              *
* Copyright 2004 The NEdit Developers                                          *
*                                                                              *
* This is free software; you can redistribute it and/or modify it under the    *
* terms of the GNU General Public License as published by the Free Software    *
* Foundation; either version 2 of the License, or (at your option) any later   *
* version. In addition, you may distribute versions of this program linked to  *
* Motif or Open Motif. See README for details.                                 *
*                                                                              *
* This software is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for    *
* more details.                                                                *
*                                                                              *
* You should have received a copy of the GNU General Public License along with *
* software; if not, write to the Free Software Foundation, Inc., 59 Temple     *
* Place, Suite 330, Boston, MA  02111-1307 USA                                 *
*                                                                              *
* Nirvana Text Editor                                                          *
* July 31, 2001                                                                *
*                                                                              *
*******************************************************************************/

#ifndef PREFERENCES_H_
#define PREFERENCES_H_

#include "util/string_view.h"
#include "IndentStyle.h"
#include "SearchType.h"
#include <X11/Intrinsic.h>


class QWidget;
class QString;
class LanguageMode;
class DocumentWidget;
class QFont;

constexpr const int PLAIN_LANGUAGE_MODE = -1;

/* maximum number of language modes allowed */
#define MAX_LANGUAGE_MODES 127

#define MAX_TITLE_FORMAT_LEN 50

#define MAX_FONT_LEN 100    /* maximum length for a font name */
#define MAX_COLOR_LEN 30    /* maximum length for a color name */

/* Identifiers for individual fonts in the help fonts list */
enum HelpFonts {
	HELP_FONT,
	BOLD_HELP_FONT,
	ITALIC_HELP_FONT,
	BOLD_ITALIC_HELP_FONT,
	FIXED_HELP_FONT,
	BOLD_FIXED_HELP_FONT,
	ITALIC_FIXED_HELP_FONT,
	BOLD_ITALIC_FIXED_HELP_FONT,
	HELP_LINK_FONT,
	H1_HELP_FONT,
	H2_HELP_FONT,
	H3_HELP_FONT,
	NUM_HELP_FONTS
};

bool GetPrefFocusOnRaise();
bool GetPrefForceOSConversion();
bool GetPrefHighlightSyntax();
bool GetPrefHonorSymlinks();
bool GetPrefUndoModifiesSelection();
QString GetPrefBacklightCharTypes();
QString GetPrefBoldFontName();
QString GetPrefBoldItalicFontName();
QString GetPrefColorName(int colorIndex);
QString GetPrefDelimiters();
QString GetPrefFontName();
QString GetPrefGeometry();
QString GetPrefItalicFontName();
QString GetPrefServerName();
QString GetPrefTagFile();
QString GetPrefTooltipBgColor();
QString GetWindowDelimitersEx(const DocumentWidget *window);
QString LanguageModeName(int mode);
char *ReadSymbolicField(const char **inPtr);
QString GetPrefShell();
QString GetPrefTitleFormat();
int FindLanguageMode(const QString &languageName);
int GetPrefAlwaysCheckRelTagsSpecs();
int GetPrefAppendLF();
IndentStyle GetPrefAutoIndent(int langMode);
int GetPrefAutoSave();
int GetPrefAutoScroll();
int GetPrefBacklightChars();
int GetPrefBeepOnSearchWrap();
int GetPrefCols();
int GetPrefEmTabDist(int langMode);
int GetPrefFindReplaceUsesSelection();
int GetPrefGlobalTabNavigate();
int GetPrefInsertTabs();
int GetPrefISearchLine();
int GetPrefKeepSearchDlogs();
int GetPrefLineNums();
int GetPrefMapDelete();
int GetPrefMatchSyntaxBased();
int GetPrefMaxPrevOpenFiles();
int GetPrefOpenInTab();
int GetPrefOverrideVirtKeyBindings();
int GetPrefRepositionDialogs();
int GetPrefRows();
int GetPrefSaveOldVersion();
int GetPrefSearchDlogs();
SearchType GetPrefSearch();
int GetPrefSearchWraps();
int GetPrefShowMatching();
int GetPrefShowPathInWindowsMenu();
int GetPrefSmartTags();
int GetPrefSortOpenPrevMenu();
int GetPrefSortTabs();
int GetPrefStatsLine();
int GetPrefStdOpenDialog();
int GetPrefStickyCaseSenseBtn();
int GetPrefTabBarHideOne();
int GetPrefTabBar();
int GetPrefTabDist(int langMode);
int GetPrefToolTips();
int GetPrefTruncSubstitution();
int GetPrefTypingHidesPointer();
int GetPrefWarnExit();
int GetPrefWarnFileMods();
int GetPrefWarnRealFileMods();
int GetPrefWrap(int langMode);
int GetPrefWrapMargin();
int GetVerticalAutoScroll();
int ParseError(Widget toDialog, const char *stringStart, const char *stoppedAt, const char *errorIn, const char *message);
bool ParseErrorEx(QWidget *toDialog, const QString &stringStart, int stoppedAt, const QString &errorIn, const QString &message);


int ReadNumericField(const char **inPtr, int *value);
int ReadQuotedString(const char **inPtr, const char **errMsg, char **string);
int ReadQuotedStringEx(const char **inPtr, const char **errMsg, QString *string);
int SkipDelimiter(const char **inPtr, const char **errMsg);
int SkipOptSeparator(char separator, const char **inPtr);
QString MakeQuotedStringEx(const QString &string);
QString ReadSymbolicFieldEx(const char **inPtr);
void ImportPrefFile(const char *filename, int convertOld);
void MarkPrefsChanged();
void RestoreNEditPrefs();
void SaveNEditPrefsEx(QWidget *parent, bool quietly);
void SetPrefAppendLF(int state);
void SetPrefAutoIndent(int state);
void SetPrefAutoSave(int state);
void SetPrefAutoScroll(int state);
void SetPrefBacklightChars(int state);
void SetPrefBeepOnSearchWrap(int state);
void SetPrefBoldFont(const QString &fontName);
void SetPrefBoldItalicFont(const QString &fontName);
void SetPrefColorName(int colorIndex, const QString &color);
void SetPrefCols(int nCols);
void SetPrefEmTabDist(int tabDist);
void SetPrefFindReplaceUsesSelection(int state);
void SetPrefFocusOnRaise(bool);
void SetPrefFont(const QString &fontName);
void SetPrefGlobalTabNavigate(int state);
void SetPrefHighlightSyntax(bool state);
void SetPrefInsertTabs(int state);
void SetPrefISearchLine(int state);
void SetPrefItalicFont(const QString &fontName);
void SetPrefKeepSearchDlogs(int state);
void SetPrefLineNums(int state);
void SetPrefMatchSyntaxBased(int state);
void SetPrefOpenInTab(int state);
void SetPrefRepositionDialogs(int state);
void SetPrefRows(int nRows);
void SetPrefSaveOldVersion(int state);
void SetPrefSearchDlogs(int state);
void SetPrefSearch(SearchType searchType);
void SetPrefSearchWraps(int state);
void SetPrefShell(const QString &shell);
void SetPrefShowMatching(int state);
void SetPrefShowPathInWindowsMenu(int state);
void SetPrefSmartTags(int state);
void SetPrefSortOpenPrevMenu(int state);
void SetPrefSortTabs(int state);
void SetPrefStatsLine(int state);
void SetPrefTabBarHideOne(int state);
void SetPrefTabBar(int state);
void SetPrefTabDist(int tabDist);
void SetPrefTitleFormat(const QString &format);
void SetPrefToolTips(int state);
void SetPrefUndoModifiesSelection(bool);
void SetPrefWarnExit(int state);
void SetPrefWarnFileMods(int state);
void SetPrefWarnRealFileMods(int state);
void SetPrefWrap(int state);
void SetPrefWrapMargin(int margin);

QFont GetPrefBoldFont();
QFont GetPrefBoldItalicFont();
QFont GetPrefItalicFont();



#ifdef REPLACE_SCOPE
void SetPrefReplaceDefScope(int scope);
int GetPrefReplaceDefScope();
#endif

extern int NLanguageModes;
extern QString ImportedFile;
extern bool PrefsHaveChanged;
extern LanguageMode *LanguageModes[MAX_LANGUAGE_MODES];


#endif
