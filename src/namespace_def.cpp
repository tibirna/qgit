/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution


 Definitions of complex namespace constants

 Complex constant objects are not folded in like integral types, so they
 are declared 'extern' in namespace to avoid duplicating them as file scope
 data in each file where QGit namespace is included.

*/

#ifdef ON_WINDOWS
#include <windows.h>
#else
#include <unistd.h> // usleep()
#include <sys/types.h> // used by chmod()
#include <sys/stat.h>  // used by chmod()
#endif

#include <qsettings.h>
#include <qfile.h>
#include <qtextstream.h>
#include <QHash>
#include <QPixmap>
#include "common.h"

// minimum git version required
const QString QGit::GIT_VERSION = "1.4.4";

// colors
const QColor QGit::BROWN       = QColor(150, 75, 0);
const QColor QGit::ORANGE      = QColor(255, 160, 50);
const QColor QGit::DARK_ORANGE = QColor(216, 144, 0);
const QColor QGit::LIGHT_BLUE  = QColor(85, 255, 255);
const QColor QGit::PURPLE      = QColor(221, 221, 255);
const QColor QGit::DARK_GREEN  = QColor(0, 205, 0);

// initialized at startup according to system wide settings
QColor QGit::ODD_LINE_COL;
QColor QGit::EVEN_LINE_COL;

/*
   Default QFont c'tor calls static method QApplication::font() that could
   be still NOT initialized at this time, so set a dummy font family instead,
   it will be properly changed later, at startup
*/
QFont QGit::TYPE_WRITER_FONT("Helvetica");

// patches drag and drop
const QString QGit::PATCHES_DIR  = "/.qgit_patches_copy";
const QString QGit::PATCHES_NAME = "qgit_import";

// files status
const QChar QGit::MODIFIED = 'M';
const QChar QGit::DELETED  = 'D';
const QChar QGit::NEW      = 'A';
const QChar QGit::RENAMED  = 'R';
const QChar QGit::COPIED   = 'C';
const QChar QGit::UNKNOWN  = '?';

// git index parameters
const QString QGit::ZERO_SHA        = "0000000000000000000000000000000000000000";
const QString QGit::CUSTOM_SHA      = "CUSTOM";
const QString QGit::ALL_MERGE_FILES = "ALL_MERGE_FILES";
const QChar QGit::IN_INDEX          = 'I';
const QChar QGit::NOT_IN_INDEX      = 'N';

// settings keys
const QString QGit::ORG_KEY         = "qgit";
const QString QGit::APP_KEY         = "qgit4";
const QString QGit::EXT_DIFF_KEY    = "external_diff_viewer";
const QString QGit::REC_REP_KEY     = "recent_open_repos";
const QString QGit::TYPWRT_FNT_KEY  = "typewriter_font";
const QString QGit::FLAGS_KEY       = "flags";
const QString QGit::ACT_LIST_KEY    = "custom_actions_list";
const QString QGit::PATCH_DIR_KEY   = "Patch/last_dir";
const QString QGit::PATCH_ARGS_KEY  = "Patch/args";
const QString QGit::EX_KEY          = "Working_dir/exclude_file_path";
const QString QGit::EX_PER_DIR_KEY  = "Working_dir/exclude_per_directory_file_name";
const QString QGit::CMT_GEOM_KEY    = "Commit/geometry";
const QString QGit::CMT_SPLIT_KEY   = "Commit/splitter_sizes";
const QString QGit::CMT_TEMPL_KEY   = "Commit/template_file_path";
const QString QGit::CMT_ARGS_KEY    = "Commit/args";
const QString QGit::ACT_GROUP_KEY   = "Custom_actions/";
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
	flags = (b) ? flags | f : flags & ~f;
	settings.setValue(flagsVariable, flags);
}

// tree view icons helpers
static QHash<QString, const QPixmap*> mimePixMap;

void QGit::initMimePix() {

	if (!mimePixMap.empty()) // only once
		return;

	QPixmap* pm = new QPixmap(QString::fromUtf8(":/icons/resources/folder.png"));
	mimePixMap.insert("#FOLDER_CLOSED", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/folder_open.png"));
	mimePixMap.insert("#FOLDER_OPEN", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/misc.png"));
	mimePixMap.insert("#DEFAULT", pm);
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
	mimePixMap.insert("pl", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_py.png"));
	mimePixMap.insert("py", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/source_java.png"));
	mimePixMap.insert("java", pm);
	mimePixMap.insert("jar", pm);
	pm = new QPixmap(QString::fromUtf8(":/icons/resources/tar.png"));
	mimePixMap.insert("tar", pm);
	mimePixMap.insert("gz", pm);
	mimePixMap.insert("zip", pm);
	mimePixMap.insert("bz", pm);
	mimePixMap.insert("bz2", pm);
}

void QGit::freeMimePix() {
return;
	QHash<QString, const QPixmap*>::const_iterator i = mimePixMap.constBegin();
	while (i != mimePixMap.constEnd()) {
     		delete i.value();
		++i;
	}
}

const QPixmap* QGit::mimePix(SCRef fileName) {

	SCRef ext = fileName.section('.', -1, -1);
	return (mimePixMap.contains(ext) ? mimePixMap.value(ext) : mimePixMap["#DEFAULT"]);
}

// misc helpers
bool QGit::stripPartialParaghraps(SCRef src, QString* dst, QString* prev) {

	int idx = src.findRev('\n');
	if (idx == -1) {
		prev->append(src);
		*dst = "";
		return false;
	}
	*dst = src.left(idx).prepend(*prev); // strip trailing '\n'
	*prev = src.mid(idx + 1); // src[idx] is '\n', skip it
	return true;
}

void QGit::compat_usleep(int us) {

#ifdef ON_WINDOWS
	Sleep(us);
#else
	usleep(us);
#endif
}

bool QGit::writeToFile(SCRef fileName, SCRef data, bool setExecutable) {

	QFile file(QFile::encodeName(fileName));
	if (!file.open(QIODevice::WriteOnly)) {
		dbp("ERROR: unable to write file %1", fileName);
		return false;
	}
	QTextStream stream(&file);
	stream << data;
	file.close();

#ifndef ON_WINDOWS
	if (setExecutable)
		chmod(fileName, 0755);
#endif
	return true;
}

bool QGit::readFromFile(SCRef fileName, QString& data) {

	data = "";
	QFile file(QFile::encodeName(fileName));
	if (!file.open(QIODevice::ReadOnly)) {
		dbp("ERROR: unable to read file %1", fileName);
		return false;
	}
	QTextStream stream(&file);
	data = stream.read();
	file.close();
	return true;
}
