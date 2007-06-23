/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution


 Definitions of complex namespace constants

 Complex constant objects are not folded in like integral types, so they
 are declared 'extern' in namespace to avoid duplicating them as file scope
 data in each file where QGit namespace is included.

*/
#include <QHash>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryFile>
#include <QTextStream>
#include <QWidget>
#include "common.h"

#ifdef Q_OS_WIN32 // *********  platform dependent code ******

const QString QGit::SCRIPT_EXT = ".bat";

static bool addShellWrapper(QStringList& args) {
/*
   To run an application/script under Windows you need
   to wrap the command line in the shell interpreter.
   You need this also to start native commands as 'dir'.
   An exception is if application is in path as 'git' must
   always be.
*/
	if (!args.first().startsWith("git-") && args.first() != "git") {
		args.prepend("/c");
		args.prepend("cmd.exe");
		return true;
	}
	return false;
}

#else

#include <sys/types.h> // used by chmod()
#include <sys/stat.h>  // used by chmod()

const QString QGit::SCRIPT_EXT = ".sh";

static bool addShellWrapper(QStringList&) {

	return false;
}

#endif // *********  end of platform dependent code ******


// minimum git version required
const QString QGit::GIT_VERSION = "1.4.4";

// colors
const QColor QGit::BROWN        = QColor(150, 75, 0);
const QColor QGit::ORANGE       = QColor(255, 160, 50);
const QColor QGit::DARK_ORANGE  = QColor(216, 144, 0);
const QColor QGit::LIGHT_ORANGE = QColor(255, 221, 170);
const QColor QGit::LIGHT_BLUE   = QColor(85, 255, 255);
const QColor QGit::PURPLE       = QColor(221, 221, 255);
const QColor QGit::DARK_GREEN   = QColor(0, 205, 0);

// initialized at startup according to system wide settings
QColor QGit::ODD_LINE_COL;
QColor QGit::EVEN_LINE_COL;
QString QGit::GIT_DIR;

/*
   Default QFont c'tor calls static method QApplication::font() that could
   be still NOT initialized at this time, so set a dummy font family instead,
   it will be properly changed later, at startup
*/
QFont QGit::TYPE_WRITER_FONT("Helvetica");

// patches drag and drop
const QString QGit::PATCHES_DIR  = "/.qgit_patches_copy";
const QString QGit::PATCHES_NAME = "qgit_import";

// git index parameters
const QString QGit::ZERO_SHA        = "0000000000000000000000000000000000000000";
const QString QGit::CUSTOM_SHA      = "CUSTOM";
const QString QGit::ALL_MERGE_FILES = "ALL_MERGE_FILES";

// settings keys
const QString QGit::ORG_KEY         = "qgit";
const QString QGit::APP_KEY         = "qgit4";
const QString QGit::EXT_DIFF_KEY    = "external_diff_viewer";
const QString QGit::REC_REP_KEY     = "recent_open_repos";
const QString QGit::TYPWRT_FNT_KEY  = "typewriter_font";
const QString QGit::FLAGS_KEY       = "flags";
const QString QGit::PATCH_DIR_KEY   = "Patch/last_dir";
const QString QGit::PATCH_ARGS_KEY  = "Patch/args";
const QString QGit::EX_KEY          = "Working_dir/exclude_file_path";
const QString QGit::EX_PER_DIR_KEY  = "Working_dir/exclude_per_directory_file_name";
const QString QGit::CON_GEOM_KEY    = "Console/geometry";
const QString QGit::CMT_GEOM_KEY    = "Commit/geometry";
const QString QGit::MAIN_GEOM_KEY   = "Top_window/geometry";
const QString QGit::REV_GEOM_KEY    = "Rev_List_view/geometry";
const QString QGit::CMT_TEMPL_KEY   = "Commit/template_file_path";
const QString QGit::CMT_ARGS_KEY    = "Commit/args";
const QString QGit::ACT_GEOM_KEY    = "Custom_actions/geometry";
const QString QGit::ACT_LIST_KEY    = "Custom_actions/list";
const QString QGit::ACT_GROUP_KEY   = "Custom_action_list/";
const QString QGit::ACT_TEXT_KEY    = "/commands";
const QString QGit::ACT_FLAGS_KEY   = "/flags";

// settings default values
const QString QGit::CMT_TEMPL_DEF   = ".git/commit-template";
const QString QGit::EX_DEF          = ".git/info/exclude";
const QString QGit::EX_PER_DIR_DEF  = ".gitignore";
const QString QGit::EXT_DIFF_DEF    = "kompare";

// cache file
const QString QGit::BAK_EXT          = ".bak";
const QString QGit::C_DAT_FILE       = "/qgit_cache.dat";

// misc
const QString QGit::QUOTE_CHAR = "$";


using namespace QGit;

// settings helpers
uint QGit::flags(SCRef flagsVariable) {

	QSettings settings;
	return settings.value(flagsVariable, FLAGS_DEF).toUInt();
}

bool QGit::testFlag(uint f, SCRef flagsVariable) {

	return (flags(flagsVariable) & f);
}

void QGit::setFlag(uint f, bool b, SCRef flagsVariable) {

	QSettings settings;
	uint flags = settings.value(flagsVariable, FLAGS_DEF).toUInt();
	flags = b ? flags | f : flags & ~f;
	settings.setValue(flagsVariable, flags);
}

// tree view icons helpers
static QHash<QString, const QPixmap*> mimePixMap;

void QGit::initMimePix() {

	if (!mimePixMap.empty()) // only once
		return;

	QPixmap* pm = new QPixmap(QString::fromUtf8(":/icons/resources/folder.png"));
	mimePixMap.insert("#folder_closed", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/folder_open.png"));
	mimePixMap.insert("#folder_open", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/misc.png"));
	mimePixMap.insert("#default", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_c.png"));
	mimePixMap.insert("c", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_cpp.png"));
	mimePixMap.insert("cpp", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_h.png"));
	mimePixMap.insert("h", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/txt.png"));
	mimePixMap.insert("txt", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/shellscript.png"));
	mimePixMap.insert("sh", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_pl.png"));
	mimePixMap.insert("perl", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("pl", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_py.png"));
	mimePixMap.insert("py", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_java.png"));
	mimePixMap.insert("java", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("jar", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/tar.png"));
	mimePixMap.insert("tar", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("gz", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("tgz", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("zip", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("bz", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("bz2", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/html.png"));
	mimePixMap.insert("html", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("xml", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/image.png"));
	mimePixMap.insert("bmp", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("gif", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("jpg", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("jpeg", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("png", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("pbm", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("pgm", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("ppm", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("svg", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("tiff", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("xbm", pm);
	pm = new QPixmap(*pm);
	mimePixMap.insert("xpm", pm);
}

void QGit::freeMimePix() {

	qDeleteAll(mimePixMap);
}

const QPixmap* QGit::mimePix(SCRef fileName) {

	SCRef ext = fileName.section('.', -1, -1).toLower();
	if (mimePixMap.contains(ext))
		return mimePixMap.value(ext);

	return mimePixMap.value("#default");
}

// geometry settings helers
void QGit::saveGeometrySetting(SCRef name, QWidget* w, splitVect* svPtr) {

	QSettings settings;
	if (w && w->isVisible()) {
		bool max = w->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
		if (!max) // workaround a X11 issue
			settings.setValue(name + "_window", w->saveGeometry());
	}
	if (!svPtr)
		return;

	int cnt = 0;
	FOREACH (splitVect, it, *svPtr) {

		cnt++;
		if ((*it)->sizes().contains(0))
			continue;

		QString nm(name + "_splitter_" + QString::number(cnt));
		settings.setValue(nm, (*it)->saveState());
	}
}

void QGit::restoreGeometrySetting(SCRef name, QWidget* w, splitVect* svPtr) {

	QSettings settings;
	QString nm;
	if (w) {
		nm = name + "_window";
		QVariant v = settings.value(nm);
		if (v.isValid())
			w->restoreGeometry(v.toByteArray());
		else
			dbp("WARNING in restoreGeometrySetting(), %1 not found", nm);
	}
	if (!svPtr)
		return;

	int cnt = 0;
	FOREACH (splitVect, it, *svPtr) {

		cnt++;
		nm = name + "_splitter_" + QString::number(cnt);
		QVariant v = settings.value(nm);
		if (!v.isValid()) {
			dbp("WARNING in restoreGeometrySetting(), %1 not found", nm);
			continue;
		}
		(*it)->restoreState(v.toByteArray());
	}
}

// misc helpers
const QStringList QGit::abbrevSha(SCList shaList) {
// An hack to fit long list of sha in command line, the solution
// will be a '--stdin' option for 'git log', but for now we use this trick.

	QStringList abbrevList;
	FOREACH_SL (it, shaList)
		abbrevList.append((*it).right(7));

	return abbrevList;
}

bool QGit::stripPartialParaghraps(const QByteArray& ba, QString* dst, QString* prev) {

	if (ba.endsWith('\n')) { // optimize common case
		*dst = ba;
		dst->truncate(dst->size() - 1); // strip trailing '\n'
		if (!prev->isEmpty()) {
			dst->prepend(*prev);
			prev->clear();
		}
		return true;
	}
	const QString src(ba);
	int idx = src.lastIndexOf('\n');
	if (idx == -1) {
		prev->append(src);
		dst->clear();
		return false;
	}
	*dst = src.left(idx).prepend(*prev); // strip trailing '\n'
	*prev = src.mid(idx + 1); // src[idx] is '\n', skip it
	return true;
}

bool QGit::writeToFile(SCRef fileName, SCRef data, bool setExecutable) {

	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly)) {
		dbp("ERROR: unable to write file %1", fileName);
		return false;
	}
	QString data2(data);
	QTextStream stream(&file);

#ifdef Q_OS_WIN32
	data2.replace("\n", "\r\n");
#endif
	stream << data2;
	file.close();

#ifndef Q_OS_WIN32
	if (setExecutable)
		chmod(fileName.toLatin1().constData(), 0755);
#endif
	return true;
}

bool QGit::writeToFile(SCRef fileName, const QByteArray& data, bool setExecutable) {

	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly)) {
		dbp("ERROR: unable to write file %1", fileName);
		return false;
	}
	QDataStream stream(&file);
	stream.writeRawData(data.constData(), data.size());
	file.close();

#ifndef Q_OS_WIN32
	if (setExecutable)
		chmod(fileName.toLatin1().constData(), 0755);
#endif
	return true;
}

bool QGit::readFromFile(SCRef fileName, QString& data) {

	data = "";
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly)) {
		dbp("ERROR: unable to read file %1", fileName);
		return false;
	}
	QTextStream stream(&file);
	data = stream.readAll();
	file.close();
	return true;
}

bool QGit::startProcess(QProcess* proc, SCList args, SCRef buf, bool* winShell) {

	if (!proc || args.isEmpty())
		return false;

	QStringList arguments(args);
	if (winShell)
		*winShell = addShellWrapper(arguments);

	QString prog(arguments.first());
	arguments.removeFirst();
	if (!buf.isEmpty()) {
	/*
	   On Windows buffer size of QProcess's standard input
	   pipe is quite limited and a crash can occur in case
	   a big chunk of data is written to process stdin.
	   As a workaround we use a temporary file to store data.
	   Process stdin will be redirected to this file
	*/
		QTemporaryFile* bufFile = new QTemporaryFile(proc);
		bufFile->open();
		QTextStream stream(bufFile);
		stream << buf;
		proc->setStandardInputFile(bufFile->fileName());
		bufFile->close();
	}
	proc->start(prog, arguments); // TODO test QIODevice::Unbuffered
	return proc->waitForStarted();
}
