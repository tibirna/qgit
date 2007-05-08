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

class ConsoleImpl : public QMainWindow, Ui_Console { // we need a statusbar
Q_OBJECT
public:
	ConsoleImpl(const QString& nm, Git* g);
	bool start(const QString& cmd,const QString& args);

signals:
	void customAction_exited(const QString& name);

public slots:
	void typeWriterFontChanged();
	void procReadyRead(const QByteArray& data);
	void procFinished();

protected slots:
	virtual void closeEvent(QCloseEvent* ce);
	void pushButtonTerminate_clicked();
	void pushButtonOk_clicked();

private:
	Git* git;
	QString actionName;
	QPointer<MyProcess> proc;
	QString inpBuf;
};

#endif
