/*
	Author: Marco Costalba (C) 2005-2007
	Author: Sebastian Pipping (C) 2021

	Copyright: See COPYING file that comes with this distribution

*/

#include <QSettings>
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
	#include <QCommandLineParser>
#else
	#include <QApplication>
	#include <QCoreApplication>
	#include <QStringList>
	#include <iostream>
#endif
#include "config.h" // defines PACKAGE_VERSION
#include "common.h"
#include "mainimpl.h"

#if defined(_MSC_VER) && defined(NDEBUG)
	#pragma comment(linker,"/entry:mainCRTStartup")
	#pragma comment(linker,"/subsystem:windows")
#endif

using namespace QGit;

int main(int argc, char* argv[]) {

	QApplication app(argc, argv);
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
    QCoreApplication::setOrganizationName(ORG_KEY);
	QCoreApplication::setApplicationName(APP_KEY);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
	QCommandLineParser parser;
	QCoreApplication::setApplicationVersion(PACKAGE_VERSION);
	parser.addHelpOption();
	parser.addVersionOption();
	parser.setApplicationDescription("QGit, a Git GUI viewer");
	parser.addPositionalArgument("git-log-args",
		"Arguments forwarded to \"git log\"; for example:\n"
		"   qgit --no-merges\n"
		"   qgit v2.6.18.. include/scsi \\\n"
		"                  drivers/scsi\n"
		"   qgit --since=\"2 weeks ago\" -- kernel/\n"
		"   qgit -r --name-status release..test\n"
		"See \"man git-log\" for details.",
		"[git-log-args]");

	parser.parse(app.arguments());

	if (parser.isSet("help")
			|| parser.isSet("help-all")
			|| parser.isSet("version")) {
		QCoreApplication::setApplicationName("QGit");
		parser.process(app.arguments());  // exits the process
	}
#else
	QStringList arguments = QCoreApplication::arguments();
	bool showHelp = false;
	bool showVersion = false;
	QStringList gitLogArgs;

	for (int i = 1; i < arguments.size(); ++i) {
		QString arg = arguments.at(i);
		if (arg == "--help" || arg == "-h") {
			showHelp = true;
		} else if (arg == "--version" || arg == "-v") {
			showVersion = true;
		} else {
			gitLogArgs.append(arg); // Collect positional arguments
		}
	}

	if (showHelp) {
		std::cout << "Usage: qgit [options] [git-log-args]\n"
			<< "Options:\n"
			<< "  --help, -h          Show this help message\n"
			<< "  --version, -v       Show application version\n"
			<< "\n"
			<< "Arguments:\n"
			<< "  git-log-args        Arguments forwarded to \"git log\"\n"
			<< "                      See \"man git-log\" for details.\n";
		return 0; // Exit after showing help
	}

	if (showVersion) {
		std::cout << "QGit version: " << PACKAGE_VERSION << "\n";
		return 0; // Exit after showing version
	}

	// Process git-log-args as needed
	foreach (const QString& arg, gitLogArgs) {
		std::cout << "Git log argument: " << arg.toStdString() << "\n";
	}
#endif

	/* On Windows msysgit exec directory is set up
	 * during installation so to always find git.exe
	 * also if not in PATH
	 */
	QSettings set;
	GIT_DIR = set.value(GIT_DIR_KEY).toString();

	initMimePix();

	MainImpl* mainWin = new MainImpl;
	mainWin->show();
	QObject::connect(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));
	bool ret = app.exec();

	freeMimePix();
	return ret;
}
