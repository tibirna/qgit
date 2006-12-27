/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef MYPROCESS_H
#define MYPROCESS_H

#include <q3process.h>
#include "git.h"

class Git;

//custom process used to run shell commands in parallel

class MyProcess : public Q3Process {
Q_OBJECT
public:
	MyProcess(QObject *go, Git* g, const QString& wd, bool reportErrors);
	bool runSync(SCRef runCmd, QString* runOutput, QObject* rcv, SCRef buf);
	bool runAsync(SCRef rc, QObject* rcv, SCRef buf);
	static void parseArgs(SCRef cmd, SList args);

signals:
	void procDataReady(const QString&);
	void eof();

public slots:
	void on_cancel();

private slots:
	void on_readyReadStdout();
	void on_readyReadStderr();
	void on_processExited();

private:
	void setupSignals();
	bool launchMe(SCRef runCmd, SCRef buf);
	void sendErrorMsg(bool notStarted = false);
	static void restoreSpaces(QString& newCmd, const QChar& sepChar);
	static const QStringList splitArgList(SCRef cmd);

	QObject* guiObject;
	Git* git;
	QString runCmd;
	QString* runOutput;
	QString workDir;
	QObject* receiver;
	bool errorReportingEnabled;
	bool canceling;
	bool busy;
	bool exitStatus;
	bool async;
};

#endif
