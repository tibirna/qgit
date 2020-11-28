/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution


 Definitions of complex namespace constants

 Complex constant objects are not folded in like integral types, so they
 are declared 'extern' in namespace to avoid duplicating them as file scope
 data in each file where QGit namespace is included.

*/
#include <QDir>
#include <QHash>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryFile>
#include <QTextStream>
#include <QWidget>
#include <QTextCodec>
#include "common.h"
#include "git.h"
#include "annotate.h"

#ifdef Q_OS_WIN32 // *********  platform dependent code ******

const QString QGit::SCRIPT_EXT = ".bat";

static void adjustPath(QStringList& args, bool* winShell) {
/*
   To run an application/script under Windows you need
   to wrap the command line in the shell interpreter.
   You need this also to start native commands as 'dir'.
   An exception is if application is 'git' in that case we
   call with absolute path to be sure to find it.
*/
	if (args.first() == "git" || args.first().startsWith("git-")) {

		if (!QGit::GIT_DIR.isEmpty()) // application built from sources
			args.first().prepend(QGit::GIT_DIR + '/');

		if (winShell)
			*winShell = false;

	} else if (winShell) {
		args.prepend("/c");
		args.prepend("cmd.exe");
		*winShell = true;
	}
}

#elif defined(Q_OS_MACX) // MacOS X specific code

#include <sys/types.h> // used by chmod()
#include <sys/stat.h>  // used by chmod()

const QString QGit::SCRIPT_EXT = ".sh";

static void adjustPath(QStringList& args, bool*) {
/*
    Under MacOS X, git typically doesn't live in the PATH
    So use GIT_DIR from the settings if available

    Note: I (OC) think that this should be the default behaviour,
          but I don't want to break other platforms, so I introduced
          the MacOS X special case. Feel free to make this the default if
          you do feel the same.
*/
	if (args.first() == "git" || args.first().startsWith("git-")) {

		if (!QGit::GIT_DIR.isEmpty()) // application built from sources
			args.first().prepend(QGit::GIT_DIR + '/');

	}
}

#else

#include <sys/types.h> // used by chmod()
#include <sys/stat.h>  // used by chmod()

const QString QGit::SCRIPT_EXT = ".sh";

static void adjustPath(QStringList&, bool*) {}

#endif // *********  end of platform dependent code ******

// definition of an optimized sha hash function
static inline uint hexVal(const uchar* ch) {

	return (*ch < 64 ? *ch - 48 : *ch - 87);
}

uint qHash(const ShaString& s) { // fast path, called 6-7 times per revision

	const uchar* ch = reinterpret_cast<const uchar*>(s.latin1());
	return (hexVal(ch     ) << 24)
	     + (hexVal(ch +  2) << 20)
	     + (hexVal(ch +  4) << 16)
	     + (hexVal(ch +  6) << 12)
	     + (hexVal(ch +  8) <<  8)
	     + (hexVal(ch + 10) <<  4)
	     +  hexVal(ch + 12);
}

/* Value returned by this function should be used only as function argument,
 * and not stored in a variable because 'ba' value is overwritten at each
 * call so the returned ShaString could became stale very quickly
 */
const ShaString QGit::toTempSha(const QString& sha) {

	static QByteArray ba;
	ba = sha.toLatin1();
	return ShaString(sha.isEmpty() ? NULL : ba.constData());
}

const ShaString QGit::toPersistentSha(const QString& sha, QVector<QByteArray>& v) {

	v.append(sha.toLatin1());
	return ShaString(v.last().constData());
}

// minimum git version required
const QString QGit::GIT_VERSION = "1.5.5";

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
QFont QGit::STD_FONT("Helvetica");
QFont QGit::TYPE_WRITER_FONT("Helvetica");

// patches drag and drop
const QString QGit::PATCHES_DIR  = "/.qgit_patches_copy";
const QString QGit::PATCHES_NAME = "qgit_import";

// git index parameters
const QString QGit::ZERO_SHA        = "0000000000000000000000000000000000000000";
const QString QGit::CUSTOM_SHA      = "*** CUSTOM * CUSTOM * CUSTOM * CUSTOM **";
const QString QGit::ALL_MERGE_FILES = "ALL_MERGE_FILES";

const QByteArray QGit::ZERO_SHA_BA(QGit::ZERO_SHA.toLatin1());
const ShaString  QGit::ZERO_SHA_RAW(QGit::ZERO_SHA_BA.constData());

// settings keys
const QString QGit::ORG_KEY         = "qgit";
const QString QGit::APP_KEY         = "qgit4";
const QString QGit::GIT_DIR_KEY     = "msysgit_exec_dir";
const QString QGit::EXT_DIFF_KEY    = "external_diff_viewer";
const QString QGit::EXT_EDITOR_KEY  = "external_editor";
const QString QGit::REC_REP_KEY     = "recent_open_repos";
const QString QGit::STD_FNT_KEY     = "standard_font";
const QString QGit::TYPWRT_FNT_KEY  = "typewriter_font";
const QString QGit::FLAGS_KEY       = "flags";
const QString QGit::PATCH_DIR_KEY   = "Patch/last_dir";
const QString QGit::FMT_P_OPT_KEY   = "Patch/args";
const QString QGit::AM_P_OPT_KEY    = "Patch/args_2";
const QString QGit::EX_KEY          = "Working_dir/exclude_file_path";
const QString QGit::EX_PER_DIR_KEY  = "Working_dir/exclude_per_directory_file_name";
const QString QGit::CON_GEOM_KEY    = "Console/geometry";
const QString QGit::CMT_GEOM_KEY    = "Commit/geometry";
const QString QGit::MAIN_GEOM_KEY   = "Top_window/geometry";
const QString QGit::REV_GEOM_KEY    = "Rev_List_view/geometry";
const QString QGit::REV_COLS_KEY    = "Rev_List_view/columns";
const QString QGit::FILE_COLS_KEY   = "File_List_view/columns";
const QString QGit::CMT_TEMPL_KEY   = "Commit/template_file_path";
const QString QGit::CMT_ARGS_KEY    = "Commit/args";
const QString QGit::RANGE_FROM_KEY  = "RangeSelect/from";
const QString QGit::RANGE_TO_KEY    = "RangeSelect/to";
const QString QGit::RANGE_OPT_KEY   = "RangeSelect/options";
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
const QString QGit::EXT_EDITOR_DEF  = "emacs";

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
	pm = new QPixmap(*pm);
	mimePixMap.insert("hpp", pm);
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
	if (w && w->isVisible())
		settings.setValue(name + "_window", w->saveGeometry());

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
	}
	if (!svPtr)
		return;

	int cnt = 0;
	FOREACH (splitVect, it, *svPtr) {

		cnt++;
		nm = name + "_splitter_" + QString::number(cnt);
		QVariant v = settings.value(nm);
		if (!v.isValid())
			continue;

		(*it)->restoreState(v.toByteArray());
	}
}

// misc helpers
bool QGit::stripPartialParaghraps(const QByteArray& ba, QString* dst, QString* prev) {

    QTextCodec* tc = QTextCodec::codecForLocale();

	if (ba.endsWith('\n')) { // optimize common case
        *dst = tc->toUnicode(ba);

		// handle rare case of a '\0' inside content
        while (dst->size() < ba.size() && ba.at(dst->size()) == '\0') {
            QString s = tc->toUnicode(ba.mid(dst->size() + 1)); // sizes should match
            dst->append(" ").append(s);
        }

		dst->truncate(dst->size() - 1); // strip trailing '\n'
		if (!prev->isEmpty()) {
			dst->prepend(*prev);
			prev->clear();
		}
		return true;
	}
    QString src = tc->toUnicode(ba);
	// handle rare case of a '\0' inside content
    while (src.size() < ba.size() && ba.at(src.size()) == '\0') {
        QString s = tc->toUnicode(ba.mid(src.size() + 1));
        src.append(" ").append(s);
    }

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
	data2.replace("\r\n", "\n"); // change windows CRLF to linux
	data2.replace("\n", "\r\n"); // then change all linux CRLF to windows
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
	adjustPath(arguments, winShell);

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
	QStringList env = QProcess::systemEnvironment();
	env << "GIT_TRACE=0"; // avoid choking on debug traces
	env << "GIT_FLUSH=0"; // skip the fflush() in 'git log'
	proc->setEnvironment(env);

	proc->start(prog, arguments); // TODO test QIODevice::Unbuffered
	return proc->waitForStarted();
}
