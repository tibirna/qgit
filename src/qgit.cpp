/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include "common.h"
#include "mainimpl.h"

using namespace QGit;

int main(int argc, char* argv[]) {

	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName(ORG_KEY);
	QCoreApplication::setApplicationName(APP_KEY);
	initMimePix();

	MainImpl* mainWin = new MainImpl;
	mainWin->show();
	QObject::connect(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));
	bool ret = app.exec();

	freeMimePix();
	return ret;
}
