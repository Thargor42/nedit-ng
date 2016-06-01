
#include <QtDebug>
#include "DialogPrint.h"
#include <QRegExpValidator>
#include <QMessageBox>


#include "preferences.h"
#include "prefFile.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>

namespace {

/* Separator between directory references in PATH environmental variable */
const char SEPARATOR = ':';

/* Maximum text string lengths */
const int MAX_QUEUE_STR = 60u;
const int MAX_HOST_STR  = 100u;


const int N_PRINT_PREFS = 7; /* must agree with number of preferences below */

const int PRINT_COMMAND_INDEX = 0;
const int COPIES_OPTION_INDEX = 1;
const int QUEUE_OPTION_INDEX  = 2;
const int NAME_OPTION_INDEX   = 3;
const int HOST_OPTION_INDEX   = 4;
const int DEFAULT_QUEUE_INDEX = 5;
const int DEFAULT_HOST_INDEX  = 6;

/* Maximum length of an error returned by IssuePrintCommand() */
const int MAX_PRINT_ERROR_LENGTH = 1024;


QString PrintCommand;  /* print command string */
QString CopiesOption;  /* # of copies argument string */
QString QueueOption;   /* queue name argument string */
QString NameOption;    /* print job name argument string */
QString HostOption;    /* host name argument string */
QString DefaultQueue;  /* default print queue */
QString DefaultHost;   /* default host name */

int Copies;                       /* # of copies last entered by user */
QString Queue;                    /* queue name last entered by user */
QString Host;                     /* host name last entered by user */
QString CmdText;                  /* print command last entered by user */
bool CmdFieldModified = false;    /* user last changed the print command field, so don't trust the rest */

}

bool DialogPrint::PreferencesLoaded = false;

static PrefDescripRec PrintPrefDescrip[N_PRINT_PREFS] = {
    {"printCommand",      "PrintCommand",      PREF_QSTRING, nullptr, &PrintCommand, nullptr, false},
    {"printCopiesOption", "PrintCopiesOption", PREF_QSTRING, nullptr, &CopiesOption, nullptr, false},
    {"printQueueOption",  "PrintQueueOption",  PREF_QSTRING, nullptr, &QueueOption,  nullptr, false},
    {"printNameOption",   "PrintNameOption",   PREF_QSTRING, nullptr, &NameOption,   nullptr, false},
    {"printHostOption",   "PrintHostOption",   PREF_QSTRING, nullptr, &HostOption,   nullptr, false},
    {"printDefaultQueue", "PrintDefaultQueue", PREF_QSTRING, nullptr, &DefaultQueue, nullptr, false},
    {"printDefaultHost",  "PrintDefaultHost",  PREF_QSTRING, nullptr, &DefaultHost,  nullptr, false},
};


//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
DialogPrint::DialogPrint(const QString &PrintFileName, const QString &jobName, QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f) {
	ui.setupUi(this);
	
	ui.spinCopies->setSpecialValueText(tr(" "));
	
	// Prohibit a relatively random sampling of characters that will cause
	// problems on command lines
	auto validator = new QRegExpValidator(QRegExp(QLatin1String("[^ \t,;|<>()\\[\\]{}!@?]*")), this);
	
	ui.editHost->setValidator(validator);
	ui.editQueue->setValidator(validator);
	
	// In case the program hasn't called LoadPrintPreferences, 
	// set up the default values for the print preferences
	if (!PreferencesLoaded) {
		LoadPrintPreferencesEx(nullptr, "", "", true);
	}

	// Make the PrintFile information available to the callback routines
	PrintFileName_ = PrintFileName;
	PrintJobName_  = jobName;
	
	if (!DefaultQueue.isEmpty()) {
		ui.labelQueue->setText(tr("Queue (%1)").arg(DefaultQueue));
	} else {
		ui.labelQueue->setText(tr("Queue"));
	}
	
	if (HostOption.isEmpty()) {
		ui.labelHost->setVisible(false);
		ui.editHost->setVisible(false);
	} else {
		ui.labelHost->setVisible(true);
		ui.editHost->setVisible(true);

		if (!DefaultHost.isEmpty()) {
			ui.labelHost->setText(tr("Host (%1)").arg(DefaultHost));
		} else {
			ui.labelHost->setText(tr("Host"));
		}
	}

	resize(width(), minimumSizeHint().height());
	
	if(CmdFieldModified) {
		// if they printed in the past, restore the command they used
		ui.editCommand->setText(CmdText);	
	} else {
		updatePrintCmd();
	}
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
DialogPrint::~DialogPrint() {
}

/*
** LoadPrintPreferences
**
** Read an X database to obtain print dialog preferences.
**
**	prefDB		X database potentially containing print preferences
**	appName		Application name which can be used to qualify
**			resource names for database lookup.
**	appClass	Application class which can be used to qualify
**			resource names for database lookup.
**	lookForFlpr	Check if the flpr print command is installed
**			and use that for the default if it's found.
**			(flpr is a Fermilab utility for printing on
**			arbitrary systems that support the lpr protocol)
*/
void DialogPrint::LoadPrintPreferencesEx(XrmDatabase prefDB, const std::string &appName, const std::string &appClass, bool lookForFlpr) {
	
	static char defaultQueue[MAX_QUEUE_STR];
	static char defaultHost[MAX_HOST_STR];

	/* check if flpr is installed, and otherwise choose appropriate
	   printer per system type */
	if (lookForFlpr && flprPresent()) {
	
		getFlprQueueDefault(defaultQueue);
		getFlprHostDefault(defaultHost);
		
		PrintPrefDescrip[PRINT_COMMAND_INDEX].defaultString = "flpr";
		PrintPrefDescrip[COPIES_OPTION_INDEX].defaultString = "";
		PrintPrefDescrip[QUEUE_OPTION_INDEX].defaultString  = "-q";
		PrintPrefDescrip[NAME_OPTION_INDEX].defaultString   = "-j ";
		PrintPrefDescrip[HOST_OPTION_INDEX].defaultString   = "-h";
		PrintPrefDescrip[DEFAULT_QUEUE_INDEX].defaultString = defaultQueue;
		PrintPrefDescrip[DEFAULT_HOST_INDEX].defaultString  = defaultHost;
	} else {
	
		getLprQueueDefault(defaultQueue);
		
		PrintPrefDescrip[PRINT_COMMAND_INDEX].defaultString = "lpr";
		PrintPrefDescrip[COPIES_OPTION_INDEX].defaultString = "-# ";
		PrintPrefDescrip[QUEUE_OPTION_INDEX].defaultString  = "-P ";
		PrintPrefDescrip[NAME_OPTION_INDEX].defaultString   = "-J ";
		PrintPrefDescrip[HOST_OPTION_INDEX].defaultString   = "";
		PrintPrefDescrip[DEFAULT_QUEUE_INDEX].defaultString = defaultQueue;
		PrintPrefDescrip[DEFAULT_HOST_INDEX].defaultString  = "";
	}

	/* Read in the preferences from the X database using the mechanism from
	   prefFile.c (this allows LoadPrintPreferences to work before any
	   widgets are created, which is more convenient than XtGetApplication-
	   Resources for applications which have no main window) */
	RestorePreferences(nullptr, prefDB, appName, appClass, PrintPrefDescrip, N_PRINT_PREFS);

	PreferencesLoaded = true;
}

/*
** Is flpr present in the search path and is it executable ?
*/
bool DialogPrint::flprPresent() {
	/* Is flpr present in the search path and is it executable ? */
	return fileInPath("flpr", 0111);
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::getFlprQueueDefault(char *defqueue) {

	if (!foundEnv("FLPQUE", defqueue)) {
		if (!foundTag("/usr/local/etc/flp.defaults", "queue", defqueue)) {
			strcpy(defqueue, "");
		}
	}
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::getFlprHostDefault(char *defhost) {

	if (!foundEnv("FLPHOST", defhost)) {
		if (!foundTag("/usr/local/etc/flp.defaults", "host", defhost)) {
			strcpy(defhost, "");
		}
	}
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::getLprQueueDefault(char *defqueue) {

	if (!foundEnv("PRINTER", defqueue)) {
		strcpy(defqueue, "");
	}
}

/*
** Is the filename file in the environment path directories
** and does it have at least some of the mode_flags enabled ?
*/
bool DialogPrint::fileInPath(const char *filename, uint16_t mode_flags) {
	char path[MAXPATHLEN];
	char *pathstring, *lastchar;

	/* Get environmental value of PATH */
	pathstring = getenv("PATH");
	if(!pathstring)
		return false;

	/* parse the pathstring and search on each directory found */
	do {
		/* if final path in list is empty, don't search it */
		if (!strcmp(pathstring, ""))
			return false;
		/* locate address of next : character */
		lastchar = strchr(pathstring, SEPARATOR);
		if (lastchar) {
			/* if more directories remain in pathstring, copy up to : */
			strncpy(path, pathstring, lastchar - pathstring);
			path[lastchar - pathstring] = '\0';
		} else {
			/* if it's the last directory, just copy it */
			strcpy(path, pathstring);
		}
		/* search for the file in this path */
		if (fileInDir(filename, path, mode_flags))
			return true; /* found it !! */
		/* point pathstring to start of new dir string */
		pathstring = lastchar + 1;
	} while (lastchar != nullptr);
	
	return false;
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
bool DialogPrint::foundEnv(const char *EnvVarName, char *result) {

	if (char *dqstr = getenv(EnvVarName)) {
		strcpy(result, dqstr);
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
bool DialogPrint::foundTag(const char *tagfilename, const char *tagname, char *result) {
	FILE *tfile;
	char tagformat[512];

	strcpy(tagformat, tagname);
	strcat(tagformat, " %s");

	tfile = fopen(tagfilename, "r");
	if (tfile) {
		while (!feof(tfile)) {
			
			char line[512];
			
			if(fgets(line, sizeof(line), tfile)) {
				if (sscanf(line, tagformat, result) != 0) {
					fclose(tfile);
					return true;
				}
			}
		}
		fclose(tfile);
	}
	return false;
}

/*
** Is the filename file in the directory dirpath
** and does it have at least some of the mode_flags enabled ?
*/
bool DialogPrint::fileInDir(const char *filename, const char *dirpath, uint16_t mode_flags) {
	
	struct stat statbuf;
	
	DIR *dfile = opendir(dirpath);
	if (dfile) {
		
		struct dirent *DirEntryPtr;	
		
		while ((DirEntryPtr = readdir(dfile))) {
			if (!strcmp(DirEntryPtr->d_name, filename)) {
				char fullname[MAXPATHLEN];
				
				snprintf(fullname, sizeof(fullname), "%s/%s", dirpath, filename);

				stat(fullname, &statbuf);
				closedir(dfile);
				return statbuf.st_mode & mode_flags;
			}
		}
		closedir(dfile);
	}
	return false;
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::on_spinCopies_valueChanged(int n) {
	Q_UNUSED(n);
	updatePrintCmd();
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::on_editQueue_textChanged(const QString &text) {
	Q_UNUSED(text);
	updatePrintCmd();
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::on_editHost_textChanged(const QString &text) {
	Q_UNUSED(text);
	updatePrintCmd();
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::updatePrintCmd() {

	
	QString copiesArg;
	QString queueArg;	
	QString hostArg;
	QString jobArg;


	// read each text field in the dialog and generate the corresponding
	// command argument
	if (!CopiesOption.isEmpty()) {
		const int copiesValue = ui.spinCopies->value();
		if(copiesValue != 0) {
			copiesArg = tr(" %1%2").arg(CopiesOption).arg(copiesValue);
		}
	}
	
	
	if (!QueueOption.isEmpty()) {
		const QString str = ui.editQueue->text();
		if (str.isEmpty()) {
		} else {
			queueArg = tr(" %1%2").arg(QueueOption, str);
		}
	}
	

	if (!HostOption.isEmpty()) {
		const QString str = ui.editHost->text();
		if (str.isEmpty()) {
		} else {
			hostArg = tr(" %1%2").arg(HostOption, str);
		}
	}


	if (!NameOption.isEmpty()) {
		jobArg = tr(" %1\"%2\"").arg(NameOption, PrintJobName_);
	}

	// Compose the command from the options determined above
	ui.editCommand->setText(tr("%1%2%3%4%5").arg(PrintCommand, copiesArg, queueArg, hostArg, jobArg));


	// Indicate that the command field was synthesized from the other fields,
	// so future dialog invocations can safely re-generate the command without
	// overwriting commands specifically entered by the user
	CmdFieldModified = false;
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::on_editCommand_textEdited(const QString &text) {
	Q_UNUSED(text);
	CmdFieldModified = true;
}

//------------------------------------------------------------------------------
// Name: 
//------------------------------------------------------------------------------
void DialogPrint::on_buttonPrint_clicked() {

	// get the print command from the command text area
	const QString str = ui.editCommand->text();

	// add the file name and output redirection to the print command
	QString command = tr("cat %1 | %2 2>&1").arg(PrintFileName_, str);

	// Issue the print command using a popen call and recover error messages
	// from the output stream of the command.
	FILE *pipe = popen(command.toLatin1().data(), "r");
	if(!pipe) {
		QMessageBox::warning(this, tr("Print Error"), tr("Unable to Print:\n%1").arg(QLatin1String(strerror(errno))));
		return;
	}

	char errorString[MAX_PRINT_ERROR_LENGTH] = {};
	int nRead = fread(errorString, 1, MAX_PRINT_ERROR_LENGTH - 1, pipe);
	
	// Make sure that the print command doesn't get stuck when trying to
	// write a lot of output on stderr (pipe may fill up). We discard
	// the additional output, though.
	char discarded[1024];
	while (fread(discarded, 1, sizeof(discarded), pipe) > 0) {
		;
	}

	if (!ferror(pipe)) {
		errorString[nRead] = '\0';
	}

	if (pclose(pipe)) {
		QMessageBox::warning(this, tr("Print Error"), tr("Unable to Print:\n%1").arg(QLatin1String(errorString)));
		return;
	}
	
	// Print command succeeded, so retain the current print parameters
	if (!CopiesOption.isEmpty()) {
		Copies = ui.spinCopies->value();
	}
	
	if (!QueueOption.isEmpty()) {
		Queue = ui.editQueue->text();
	}
	
	if (!HostOption.isEmpty()) {
		Host = ui.editHost->text();
	}

	CmdText = ui.editCommand->text();	
	accept();
}
