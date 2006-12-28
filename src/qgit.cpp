/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qapplication.h>
#include "common.h"
#include "mainimpl.h"

int main(int argc, char* argv[]) {

	QApplication app(argc, argv);
	QGit::initMimePix();

	MainImpl* mainWin = new MainImpl;
	mainWin->show();
	QObject::connect(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));
	bool ret = app.exec();

	QGit::freeMimePix();
	return ret;
}
