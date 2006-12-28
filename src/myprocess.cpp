/*
	Description: interface to sync and async external program execution

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qapplication.h>
#include <qeventloop.h>
#include "exceptionmanager.h"
#include "common.h"
#include "domain.h"
#include "myprocess.h"

MyProcess::MyProcess(QObject *go, Git* g, const QString& wd, bool err) : Q3Process(g) {

	guiObject = go;
	git = g;
	workDir = wd;
	receiver = NULL;
	errorReportingEnabled = err;
	canceling = async = false;
	exitStatus = true;
}

bool MyProcess::runAsync(SCRef rc, QObject* rcv, SCRef buf) {

	async = true;
	runCmd = rc;
	receiver = rcv;
	setupSignals();
	if (!launchMe(runCmd, buf))
		return false; // caller will delete us

	return true;
}

bool MyProcess::runSync(SCRef rc, QString* ro, QObject* rcv, SCRef buf) {

	async = false;
	runCmd = rc;
	runOutput = ro;
	receiver = rcv;
	if (runOutput != NULL)
		*runOutput = "";

	setupSignals();
	if (!launchMe(runCmd, buf))
		return false;

	busy = true; // we have to wait here until we exit

	// we are now leaving our safe domain context and
	// jump right in to processEvents() hyperspace
	Domain* d = git->curContext();
	git->setCurContext(NULL);

	EM_BEFORE_PROCESS_EVENTS;

	while (busy) {
		// without this filter on patch does not work
		// FIXME definitely needs more understanding
		qApp->processEvents();
		QGit::compat_usleep(20000); // suspend 20ms to let OS reschedule
		isRunning();
	}

	EM_AFTER_PROCESS_EVENTS;

	if (git->curContext() != NULL)
		qDebug("ASSERT in MyProcess::runSync, context is %p "
		       "instead of NULL", (void*)git->curContext());

	git->setCurContext(d); // restore our context

	return exitStatus;
}

void MyProcess::setupSignals() {

	connect(git, SIGNAL(cancelAllProcesses()), this, SLOT(on_cancel()));
	connect(this, SIGNAL(readyReadStdout()), this, SLOT(on_readyReadStdout()));
	connect(this, SIGNAL(processExited()), this, SLOT(on_processExited()));
	if (receiver != NULL) {
		connect(this, SIGNAL(readyReadStderr()), this, SLOT(on_readyReadStderr()));
		connect(this, SIGNAL(procDataReady(const QString&)),
		        receiver, SLOT(procReadyRead(const QString&)));
		connect(this, SIGNAL(eof()), receiver, SLOT(procFinished()));
	}
	Domain* d = git->curContext();
	if (d)
		connect(d, SIGNAL(cancelDomainProcesses()), this, SLOT(on_cancel()));
}

void MyProcess::sendErrorMsg(bool notStarted) {

	if (!errorReportingEnabled)
		return;

	QString errorDesc(readStderr());
	if (notStarted)
		errorDesc = QString::fromAscii("Unable to start the process!");

	const QString cmd(arguments().join(" ")); // hide any QUOTE_CHAR or related stuff
	MainExecErrorEvent* e = new MainExecErrorEvent(cmd, errorDesc);
	QApplication::postEvent(guiObject, e);
}

bool MyProcess::launchMe(SCRef runCmd, SCRef buf) {

	const QStringList sl(splitArgList(runCmd));
	setArguments(sl);
	setWorkingDirectory(workDir);
	bool ok = launch(buf);
	if (!ok)
		sendErrorMsg(true);

	return ok;
}

void MyProcess::on_readyReadStdout() {
// workaround pipe buffer size limit. In case of big output, buffer
// can became full and an hang occurs, so we read all data as soon
// as possible and store it in runOutput

	const QString tmp(readStdout());
	if (canceling)
		return;

	if (receiver != NULL)
		emit procDataReady(tmp);

	else if (runOutput != NULL)
		runOutput->append(tmp);
}

void MyProcess::on_readyReadStderr() {

	const QString tmp(readStderr());
	if (canceling)
		return;

	if (receiver != NULL)
		emit procDataReady(tmp); // redirect to stdout
	else {
		dbs("ASSERT in myReadFromStderr: NULL receiver");
		return;
	}
}

void MyProcess::on_processExited() {

	if (canceling || !normalExit() || canReadLineStderr())
		exitStatus = false;

	if (!canceling) { // no more noise after cancel

		if (receiver)
			emit eof();

		if (!exitStatus)
			sendErrorMsg();
	}
	busy = false;
	if (async)
		deleteLater();
}

void MyProcess::on_cancel() {

	canceling = true;
	Q3Process::tryTerminate();
}

void MyProcess::parseArgs(SCRef cmd, SList args) {

	args = splitArgList(cmd);
}

const QStringList MyProcess::splitArgList(SCRef cmd) {
// return argument list handling quotes and double quotes
// substring, as example from:
// cmd arg1 "some thing" arg2='some value'
// to
// sl = <cmd/arg1/some thing/arg2='some value'>

	// early exit the common case
	if (!(   cmd.contains(QGit::QUOTE_CHAR)
	      || cmd.contains("\"")
	      || cmd.contains("\'")))
		return QStringList::split(' ', cmd);

	// we have some work to do...
	// first find a possible separator
	const QString sepList("#%&!?"); // separator candidates
	int i = 0;
	while (cmd.contains(sepList[i]) && i < sepList.length())
		i++;

	if (i == sepList.length()) {
		dbs("ASSERT no unique separator found");
		return QStringList();
	}
	const QChar& sepChar(sepList[i]);

	// remove all spaces
	QString newCmd(cmd);
	newCmd.replace(QChar(' '), sepChar);

	// re-add spaces in quoted sections
	restoreSpaces(newCmd, sepChar);

	// QUOTE_CHAR is used internally to delimit arguments
	// with quoted text wholly inside as
	// arg1 = <[patch] cool patch on "cool feature">
	// and should be removed before to feed QProcess
	newCmd.remove(QGit::QUOTE_CHAR);

	// QProcess::setArguments doesn't want quote
	// delimited arguments, so remove trailing quotes
	QStringList sl(QStringList::split(sepChar, newCmd));
	QStringList::iterator it(sl.begin());
	for ( ; it != sl.end(); ++it) {
		if (((*it).left(1) == "\"" && (*it).right(1) == "\"") ||
		   ((*it).left(1) == "\'" && (*it).right(1) == "\'"))
			*it = (*it).mid(1, (*it).length() - 2);
	}
	return sl;
}

void MyProcess::restoreSpaces(QString& newCmd, const QChar& sepChar) {
// restore spaces inside quoted text, supports nested quote types

	QChar quoteChar;
	bool replace = false;
	for (int i = 0; i < newCmd.length(); i++) {

		const QChar& c = newCmd[i];

		if (    !replace
		    && (c == QGit::QUOTE_CHAR[0] || c == '\"' || c == '\'')
		    && (newCmd.count(c) % 2 == 0)) {

				replace = true;
				quoteChar = c;
				continue;
		}
		if (replace && (c == quoteChar)) {
			replace = false;
			continue;
		}
		if (replace && c == sepChar)
			newCmd[i] = QChar(' ');
	}
}
