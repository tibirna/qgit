/*
	Description: stdout viewer

	Author: Marco Costalba (C) 2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef CONSOLEIMPL_H
#define CONSOLEIMPL_H

#include <QCloseEvent>
#include <QPointer>
#include "ui_console.h"

class MyProcess;
class Git;

class ConsoleImpl : public QMainWindow, Ui_Console {
Q_OBJECT
public:
	ConsoleImpl(const QString& nm, Git* g);
	bool start(const QString& cmd,const QString& args);

signals:
	void customAction_exited(const QString& name);

public slots:
	void procReadyRead(const QString& data);
	void procFinished();

protected slots:
	void pushButtonTerminate_clicked();
	void pushButtonOk_clicked();
	void closeEvent(QCloseEvent* ce);

private:
	Git* git;
	QString name;
	QPointer<MyProcess> proc;
	QString inpBuf;
};

#endif
