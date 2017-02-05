/*******************************************************************************
*                                                                              *
* nedit.c -- Nirvana Editor main program                                       *
*                                                                              *
* Copyright (C) 1999 Mark Edel                                                 *
*                                                                              *
* This is free software; you can redistribute it and/or modify it under the    *
* terms of the GNU General Public License as published by the Free Software    *
* Foundation; either version 2 of the License, or (at your option) any later   *
* version. In addition, you may distribute versions of this program linked to  *
* Motif or Open Motif. See README for details.                                 *
*                                                                              *
* This software is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License        *
* for more details.                                                            *
*                                                                              *
* You should have received a copy of the GNU General Public License along with *
* software; if not, write to the Free Software Foundation, Inc., 59 Temple     *
* Place, Suite 330, Boston, MA  02111-1307 USA                                 *
*                                                                              *
* Nirvana Text Editor                                                          *
* May 10, 1991                                                                 *
*                                                                              *
* Written by Mark Edel                                                         *
*                                                                              *
* Modifications:                                                               *
*                                                                              *
*        8/18/93 - Mark Edel & Joy Kyriakopulos - Ported to VMS                *
*                                                                              *
*******************************************************************************/

#include <QApplication>
#include <QX11Info>
#include "ui/DialogPrint.h"
#include "ui/MainWindow.h"
#include "ui/DocumentWidget.h"
#include "ui/DialogAbout.h"

#include "nedit.h"
#include "util/fileUtils.h"
#include "file.h"
#include "interpret.h"
#include "macro.h"
#include "menu.h"
#include "parse.h"
#include "preferences.h"
#include "regularExp.h"
#include "selection.h"
#include "server.h"
#include "tags.h"
#include "util/misc.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/param.h>

static void nextArg(int argc, char **argv, int *argIndex);
static int checkDoMacroArg(const char *macro);

bool IsServer       = false;

static const char cmdLineHelp[] =
    "Usage:  nedit [-read] [-create] [-line n | +n] [-server] [-do command]\n\
	      [-tags file] [-tabs n] [-wrap] [-nowrap] [-autowrap]\n\
	      [-autoindent] [-noautoindent] [-autosave] [-noautosave]\n\
	      [-lm languagemode] [-rows n] [-columns n] [-font font]\n\
	      [-geometry geometry] [-iconic] [-noiconic] [-svrname name]\n\
	      [-display [host]:server[.screen] [-xrm resourcestring]\n\
	      [-import file] [-background color] [-foreground color]\n\
	      [-tabbed] [-untabbed] [-group] [-V|-version] [-h|-help]\n\
	      [--] [file...]\n";

int main(int argc, char *argv[]) {	

    Display *TheDisplay = nullptr;
	int lineNum;
	int nRead;
	bool fileSpecified = false;
	int editFlags = CREATE;
	bool gotoLine = false;
    bool macroFileReadEx = false;
	bool opts = true;
	bool iconic = false;
	int tabbed = -1;
	int group = 0;
	int isTabbed;
    QString geometry;
	char *langMode = nullptr;
	char filename[MAXPATHLEN];
	char pathname[MAXPATHLEN];	
    QPointer<DocumentWidget> lastFileEx = nullptr;
    char *toDoCommand = nullptr;

    // TODO(eteran): support non-X11 instance for things like -version again
    QApplication app(argc, argv);

#ifdef Q_OS_LINUX
    // temporary hack
    TheDisplay = QX11Info::display();

	if (!TheDisplay) {
		// Respond to -V or -version even if there is no display 
		for (int i = 1; i < argc && strcmp(argv[i], "--"); i++) {
			if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "-version") == 0) {
                QString infoString = DialogAbout::createInfoString();
                printf("%s", infoString.toLatin1().data());
				exit(EXIT_SUCCESS);
			}
		}
		fputs("NEdit: Can't open display\n", stderr);
		exit(EXIT_FAILURE);
	}
#endif



	// Initialize global symbols and subroutines used in the macro language 
	InitMacroGlobals();
	RegisterMacroSubroutines();


	/* Store preferences from the command line and .nedit file,
	   and set the appropriate preferences */
    RestoreNEditPrefs();

	// More preference stuff 
    DialogPrint::LoadPrintPreferencesEx(true);


#if 0 // TODO(eteran): I think that this feature likely has no equivalent in Qt's dialog
    SetPointerCenteredDialogs(GetPrefRepositionDialogs());
    SetDeleteRemap(GetPrefMapDelete());
	SetGetEFTextFieldRemoval(!GetPrefStdOpenDialog());
#endif

	// Install word delimiters for regular expression matching 
	SetREDefaultWordDelimiters(GetPrefDelimiters().toLatin1().data());

	/* Read the nedit dynamic database of files for the Open Previous
	   command (and eventually other information as well) */
    MainWindow::ReadNEditDB();

	/* Process -import command line argument before others which might
	   open windows (loading preferences doesn't update menu settings,
	   which would then be out of sync with the real preference settings) */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			break; // treat all remaining arguments as filenames 
		} else if (!strcmp(argv[i], "-import")) {
			nextArg(argc, argv, &i);
			ImportPrefFile(argv[i], false);
		} else if (!strcmp(argv[i], "-importold")) {
			nextArg(argc, argv, &i);
			ImportPrefFile(argv[i], true);
		}
	}

	/* Load the default tags file. Don't complain if it doesn't load, the tag
	   file resource is intended to be set and forgotten.  Running nedit in a
	   directory without a tags should not cause it to spew out errors. */
    if (!GetPrefTagFile().isEmpty()) {
        AddTagsFileEx(GetPrefTagFile(), TAG);
	}

    if (!GetPrefServerName().isEmpty()) {
		IsServer = true;
	}

	/* Process any command line arguments (-tags, -do, -read, -create,
	   +<line_number>, -line, -server, and files to edit) not already
	   processed by RestoreNEditPrefs. */
	fileSpecified = false;
	for (int i = 1; i < argc; i++) {
		if (opts && !strcmp(argv[i], "--")) {
			opts = false; // treat all remaining arguments as filenames 
			continue;
		} else if (opts && !strcmp(argv[i], "-tags")) {
			nextArg(argc, argv, &i);
			if (!AddTagsFile(argv[i], TAG))
				fprintf(stderr, "NEdit: Unable to load tags file\n");
		} else if (opts && !strcmp(argv[i], "-do")) {
			nextArg(argc, argv, &i);
			if (checkDoMacroArg(argv[i]))
				toDoCommand = argv[i];
		} else if (opts && !strcmp(argv[i], "-read")) {
			editFlags |= PREF_READ_ONLY;
		} else if (opts && !strcmp(argv[i], "-create")) {
			editFlags |= SUPPRESS_CREATE_WARN;
		} else if (opts && !strcmp(argv[i], "-tabbed")) {
			tabbed = 1;
			group = 0; // override -group option 
		} else if (opts && !strcmp(argv[i], "-untabbed")) {
			tabbed = 0;
			group = 0; // override -group option 
		} else if (opts && !strcmp(argv[i], "-group")) {
			group = 2; // 2: start new group, 1: in group 
		} else if (opts && !strcmp(argv[i], "-line")) {
			nextArg(argc, argv, &i);
			nRead = sscanf(argv[i], "%d", &lineNum);
			if (nRead != 1)
				fprintf(stderr, "NEdit: argument to line should be a number\n");
			else
				gotoLine = true;
		} else if (opts && (*argv[i] == '+')) {
			nRead = sscanf((argv[i] + 1), "%d", &lineNum);
			if (nRead != 1)
				fprintf(stderr, "NEdit: argument to + should be a number\n");
			else
				gotoLine = true;
		} else if (opts && !strcmp(argv[i], "-server")) {
			IsServer = true;
		} else if (opts && !strcmp(argv[i], "-xwarn")) {
#if 0
			XtAppSetWarningHandler(context, showWarningFilter);
#endif
		} else if (opts && (!strcmp(argv[i], "-iconic") || !strcmp(argv[i], "-icon"))) {
			iconic = true;
		} else if (opts && !strcmp(argv[i], "-noiconic")) {
			iconic = false;
		} else if (opts && (!strcmp(argv[i], "-geometry") || !strcmp(argv[i], "-g"))) {
			nextArg(argc, argv, &i);
            geometry = QLatin1String(argv[i]);
		} else if (opts && !strcmp(argv[i], "-lm")) {
			nextArg(argc, argv, &i);
			langMode = argv[i];
		} else if (opts && !strcmp(argv[i], "-import")) {
			nextArg(argc, argv, &i); // already processed, skip 
		} else if (opts && (!strcmp(argv[i], "-V") || !strcmp(argv[i], "-version"))) {
            QString infoString = DialogAbout::createInfoString();
            printf("%s", infoString.toLatin1().data());
			exit(EXIT_SUCCESS);
		} else if (opts && (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-help"))) {
			fprintf(stderr, "%s", cmdLineHelp);
			exit(EXIT_SUCCESS);
		} else if (opts && (*argv[i] == '-')) {

			fprintf(stderr, "nedit: Unrecognized option %s\n%s", argv[i], cmdLineHelp);
			exit(EXIT_FAILURE);
		} else {

			if (ParseFilename(argv[i], filename, pathname) == 0) {
				/* determine if file is to be openned in new tab, by
				   factoring the options -group, -tabbed & -untabbed */
				if (group == 2) {
					isTabbed = 0; // start a new window for new group 
					group = 1;    // next file will be within group 
				} else if (group == 1) {
					isTabbed = 1; // new tab for file in group 
				} else {          // not in group 
					isTabbed = (tabbed == -1) ? GetPrefOpenInTab() : tabbed;
				}

				/* Files are opened in background to improve opening speed
				   by defering certain time  consuiming task such as syntax
				   highlighting. At the end of the file-opening loop, the
				   last file opened will be raised to restore those deferred
				   items. The current file may also be raised if there're
				   macros to execute on. */

                QPointer<DocumentWidget> documentEx = nullptr;
                QList<MainWindow *> windows = MainWindow::allWindows();
                if(!windows.empty()) {
                    documentEx = DocumentWidget::EditExistingFileEx(windows[0]->currentDocument(), QLatin1String(filename), QLatin1String(pathname), editFlags, geometry, iconic, langMode, isTabbed, true);
                } else {
                    documentEx = DocumentWidget::EditExistingFileEx(nullptr,                       QLatin1String(filename), QLatin1String(pathname), editFlags, geometry, iconic, langMode, isTabbed, true);
                }

                fileSpecified = true;

                if (documentEx) {

                    // raise the last tab of previous window
                    if (lastFileEx && lastFileEx->toWindow() != documentEx->toWindow()) {
                        lastFileEx->RaiseDocument();
                    }

                    if (!macroFileReadEx) {
                        ReadMacroInitFileEx(documentEx);
                        macroFileReadEx = true;
                    }
                    if (gotoLine) {
                        SelectNumberedLineEx(documentEx, documentEx->firstPane(), lineNum);
                    }

                    if (toDoCommand) {
                        DoMacroEx(documentEx, toDoCommand, "-do macro");
                        toDoCommand = nullptr;
                    }
                }

                // register last opened file for later use
                if (documentEx) {
                    lastFileEx = documentEx;
                }

			} else {
				fprintf(stderr, "nedit: file name too long: %s\n", argv[i]);
			}

			// -line/+n does only affect the file following this switch 
			gotoLine = false;
		}
	}

	// Raise the last file opened 
    if (lastFileEx) {
        lastFileEx->RaiseDocument();
    }
    MainWindow::CheckCloseDimEx();

    // If no file to edit was specified, open a window to edit "Untitled"
	if (!fileSpecified) {
        DocumentWidget *documentEx = MainWindow::EditNewFileEx(nullptr, geometry, iconic, langMode, QString());

        ReadMacroInitFileEx(documentEx);
        MainWindow::CheckCloseDimEx();

        if (toDoCommand) {
            DoMacroEx(documentEx, toDoCommand, "-do macro");
        }
	}

#if 0
	// Begin remembering last command invoked for "Repeat" menu item 
	AddLastCommandActionHook(context);
#endif
	// Set up communication port and write ~/.nedit_server_process file 
	if (IsServer) {
		InitServerCommunication();
	}

	// Process events. 
	if (IsServer) {
#if 0
		ServerMainLoop(context);
#endif
	} else {
		return app.exec();
	}
}

static void nextArg(int argc, char **argv, int *argIndex) {
	if (*argIndex + 1 >= argc) {
		fprintf(stderr, "NEdit: %s requires an argument\n%s", argv[*argIndex], cmdLineHelp);
		exit(EXIT_FAILURE);
	}
	(*argIndex)++;
}

/*
** Return True if -do macro is valid, otherwise write an error on stderr
*/
static int checkDoMacroArg(const char *macro) {

	const char *errMsg;
	const char *stoppedAt;
	int macroLen;

	/* Add a terminating newline (which command line users are likely to omit
	   since they are typically invoking a single routine) */
	macroLen = strlen(macro);
	auto tMacro = new char[strlen(macro) + 2];
	strncpy(tMacro, macro, macroLen);
	tMacro[macroLen] = '\n';
	tMacro[macroLen + 1] = '\0';

	// Do a test parse 
	Program *const prog = ParseMacro(tMacro, &errMsg, &stoppedAt);

	if(!prog) {        
		ParseError(nullptr, tMacro, stoppedAt, "argument to -do", errMsg);
        delete [] tMacro;
		return False;
	}
    delete [] tMacro;
	FreeProgram(prog);
	return True;
}
