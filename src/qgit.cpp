/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSettings>
#include "common.h"
#include "defmac.h"
#include "mainimpl.h"

#if defined(_MSC_VER) && defined(NDEBUG)
	#pragma comment(linker,"/entry:mainCRTStartup")
	#pragma comment(linker,"/subsystem:windows")
#endif

using namespace QGit;

int main(int argc, char* argv[]) {

	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName(ORG_KEY);
	QCoreApplication::setApplicationName(APP_KEY);

	/* On Windows msysgit exec directory is set up
	 * during installation so to always find git.exe
	 * also if not in PATH
	 */
	QSettings set;
	GIT_DIR = set.value(GIT_DIR_KEY).toString();

	initMimePix();

	MainImpl* mainWin = new MainImpl;
	mainWin->show();
    chk_connect_a(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));
	bool ret = app.exec();

	freeMimePix();
	return ret;
}
