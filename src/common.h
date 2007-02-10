/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef COMMON_H
#define COMMON_H

#include <QLinkedList>
#include <QHash>
#include <QVector>
#include <QEvent>
#include <QColor>
#include <QFont>
#include <QVariant>

/*
   QVariant does not support size_t type used in Qt containers, this is
   a problem on 64bit systems where size_t != uint and when using debug
   macros on size_t variables, as example dbg(vector.count()), a compile
   error occurs.
   Workaround this using a function template and a specialization.
   Function _valueOf() is used by debug macros
*/
template<typename T> inline const QString _valueOf(const T& x) { return QVariant(x).toString(); }
inline const QString& _valueOf(const QString& x) { return x; }
inline const QString  _valueOf(size_t x) { return QString::number((uint)x); }

// some debug macros
#define dbg(x)    qDebug(#x " is <%s>", _valueOf(x).latin1())
#define dbs(x)    qDebug(_valueOf(x))
#define dbp(s, x) qDebug(QString(s).arg(_valueOf(x)))
#define db1       qDebug("Mark Nr. 1")
#define db2       qDebug("Mark Nr. 2")
#define db3       qDebug("Mark Nr. 3")
#define db4       qDebug("Mark Nr. 4")
#define dbStart   dbs("Starting timer..."); QTime _t; _t.start()
#define dbRestart dbp("Elapsed time is %1 ms", _t.restart())

// some syntactic sugar
#define FOREACH(type, i, c) for (type::const_iterator i((c).constBegin()),    \
                                 _e##i##_((c).constEnd()); i != _e##i##_; ++i)

#define FOREACH_SL(i, c)    FOREACH(QStringList, i, c)

// type shortcuts
typedef const QString&              SCRef;
typedef QStringList&                SList;
typedef const QStringList&          SCList;
typedef QLinkedList<QString>&       SLList;
typedef const QLinkedList<QString>& SCLList;
typedef QVector<QString>            StrVect;

class QProcess;

namespace QGit {

	// minimum git version required
	extern const QString GIT_VERSION;

	// key bindings
	enum KeyType {
		KEY_UP,
		KEY_DOWN,
		SHIFT_KEY_UP,
		SHIFT_KEY_DOWN,
		KEY_LEFT,
		KEY_RIGHT,
		CTRL_PLUS,
		CTRL_MINUS,
		KEY_U,
		KEY_D,
		KEY_DELETE,
		KEY_B,
		KEY_BCKSPC,
		KEY_SPACE,
		KEY_R,
		KEY_P,
		KEY_F
	};

	// tab pages
	enum TabType {
		TAB_REV,
		TAB_PATCH,
		TAB_FILE
	};

	// graph elements
	enum LaneType {
		EMPTY,
		ACTIVE,
		NOT_ACTIVE,
		MERGE_FORK,
		MERGE_FORK_R,
		MERGE_FORK_L,
		JOIN,
		JOIN_R,
		JOIN_L,
		HEAD,
		HEAD_R,
		HEAD_L,
		TAIL,
		TAIL_R,
		TAIL_L,
		CROSS,
		CROSS_EMPTY,
		INITIAL,
		BRANCH,
		UNAPPLIED,
		APPLIED,
		BOUNDARY,
		BOUNDARY_C, // corresponds to MERGE_FORK
		BOUNDARY_R, // corresponds to MERGE_FORK_R
		BOUNDARY_L, // corresponds to MERGE_FORK_L

		LANE_TYPES_NUM
	};
	const int COLORS_NUM = 8;

	// graph helpers
	inline bool isHead(int x) { return (x == HEAD || x == HEAD_R || x == HEAD_L); }
	inline bool isTail(int x) { return (x == TAIL || x == TAIL_R || x == TAIL_L); }
	inline bool isJoin(int x) { return (x == JOIN || x == JOIN_R || x == JOIN_L); }
	inline bool isFreeLane(int x) { return (x == NOT_ACTIVE || x == CROSS || isJoin(x)); }
	inline bool isBoundary(int x) { return (x == BOUNDARY || x == BOUNDARY_C ||
	                                        x == BOUNDARY_R || x == BOUNDARY_L); }
	inline bool isMerge(int x) { return (x == MERGE_FORK || x == MERGE_FORK_R ||
	                                     x == MERGE_FORK_L || isBoundary(x)); }
	// custom events
	enum EventType {
		ERROR_EV      = 65432,
		POPUP_LIST_EV = 65433,
		POPUP_FILE_EV = 65434,
		POPUP_TREE_EV = 65435,
		MSG_EV        = 65436,
		ANN_PRG_EV    = 65437,
		UPD_DM_EV     = 65438,
		UPD_DM_MST_EV = 65439
	};

	// list views columns
	enum ColumnType {
		GRAPH_COL   = 0,
		ANN_ID_COL  = 1,
		LOG_COL     = 2,
		AUTH_COL    = 3,
		TIME_COL    = 4,
		COMMIT_COL  = 97, // dummy col used for sha searching
		LOG_MSG_COL = 98, // dummy col used for log messages searching
		SHA_MAP_COL = 99  // dummy col used when filter output is a set of matching sha
	};

	inline bool isInfoCol(int x) { return (x == TIME_COL || x == LOG_COL || x == AUTH_COL); }

	// default list view widths
	const int DEF_GRAPH_COL_WIDTH = 80;
	const int DEF_LOG_COL_WIDTH   = 500;
	const int DEF_AUTH_COL_WIDTH  = 230;
	const int DEF_TIME_COL_WIDTH  = 160;

	// colors
	extern const QColor BROWN;
	extern const QColor ORANGE;
	extern const QColor DARK_ORANGE;
	extern const QColor LIGHT_ORANGE;
	extern const QColor LIGHT_BLUE;
	extern const QColor PURPLE;
	extern const QColor DARK_GREEN;

	// initialized at startup according to system wide settings
	extern QColor  ODD_LINE_COL;
	extern QColor  EVEN_LINE_COL;
	extern QFont   TYPE_WRITER_FONT;
	extern QString GIT_DIR;

	// patches drag and drop
	extern const QString PATCHES_DIR;
	extern const QString PATCHES_NAME;

	// git index parameters
	extern const QString ZERO_SHA;
	extern const QString CUSTOM_SHA;
	extern const QChar IN_INDEX;
	extern const QChar NOT_IN_INDEX;
	extern const QString ALL_MERGE_FILES;

	// settings keys
	extern const QString ORG_KEY;
	extern const QString APP_KEY;
	extern const QString PATCH_DIR_KEY;
	extern const QString PATCH_ARGS_KEY;
	extern const QString TYPWRT_FNT_KEY;
	extern const QString FLAGS_KEY;
	extern const QString CON_GEOM_KEY;
	extern const QString CMT_GEOM_KEY;
	extern const QString CMT_SPLIT_KEY;
	extern const QString CMT_TEMPL_KEY;
	extern const QString CMT_ARGS_KEY;
	extern const QString EX_KEY;
	extern const QString EX_PER_DIR_KEY;
	extern const QString EXT_DIFF_KEY;
	extern const QString REC_REP_KEY;
	extern const QString ACT_LIST_KEY;
	extern const QString ACT_GEOM_KEY;
	extern const QString ACT_GROUP_KEY;
	extern const QString ACT_TEXT_KEY;
	extern const QString ACT_FLAGS_KEY;

	// settings default values
	extern const QString CMT_TEMPL_DEF;
	extern const QString EX_DEF;
	extern const QString EX_PER_DIR_DEF;
	extern const QString EXT_DIFF_DEF;

	// settings booleans
	enum FlagType {
		// removed obsolete option
		ACT_REFRESH_F   = 2,
		NUMBERS_F       = 4,
		// removed obsolete option
		ACT_CMD_LINE_F  = 16,
		DIFF_INDEX_F    = 32,
		SIGN_PATCH_F    = 64,
		SIGN_CMT_F      = 128,
		VERIFY_CMT_F    = 256,
		// removed obsolete option
		REL_DATE_F      = 1024,
		ALL_BRANCHES_F  = 2048,
		WHOLE_HISTORY_F = 4096,
		RANGE_SELECT_F  = 8192
	};
	const int FLAGS_DEF = 8512;

	// settings helpers
	uint flags(SCRef flagsVariable);
	bool testFlag(uint f, SCRef fv = FLAGS_KEY);
	void setFlag(uint f, bool b, SCRef fv = FLAGS_KEY);

	// tree view icons helpers
	void initMimePix();
	void freeMimePix();
	const QPixmap* mimePix(SCRef fileName);

	// misc helpers
	bool stripPartialParaghraps(SCRef src, QString* dst, QString* prev);
	bool writeToFile(SCRef fileName, SCRef data, bool setExecutable = false);
	bool readFromFile(SCRef fileName, QString& data);
	bool startProcess(QProcess* proc, SCList args, SCRef buf = "", bool* winShell = NULL);
	void compat_usleep(int us);

	// cache file
	const uint C_MAGIC  = 0xA0B0C0D0;
	const int C_VERSION = 13;

	extern const QString BAK_EXT;
	extern const QString C_DAT_FILE;

	// misc
	const int MAX_DICT_SIZE    = 100003; // must be a prime number see QDict docs
	const int MAX_MENU_ENTRIES = 15;
	const int MAX_RECENT_REPOS = 7;
	extern const QString QUOTE_CHAR;
	extern const QString SCRIPT_EXT;
}

class Rev {
	// prevent implicit C++ compiler defaults
	Rev();
	Rev(const Rev&);
	Rev& operator=(const Rev&);
public:
	Rev(const QByteArray& b, uint s, int idx, int* next)
	    : orderIdx(idx), ba(b), start(s) {

		isDiffCache = isApplied = isUnApplied = false;
		descRefsMaster = ancRefsMaster = descBrnMaster = -1;
		*next = indexData();
	}
	bool isBoundary() const { return (boundaryOfs == 1); }
	uint parentsCount() const { return parentsCnt; }
	const QString parent(int idx) const;
	const QStringList parents() const;
	const QString sha() const { return mid(start + boundaryOfs, 40); }
	const QString author() const { return mid(autStart, autLen); }
	const QString authorDate() const { return mid(autDateStart, autDateLen); }
	const QString shortLog() const { return mid(sLogStart, sLogLen); }
	const QString longLog() const { return mid(lLogStart, lLogLen); }

	QVector<int> lanes, childs;
	bool isDiffCache, isApplied, isUnApplied;
	int orderIdx;
	QVector<int> descRefs;     // list of descendant refs index, normally tags
	QVector<int> ancRefs;      // list of ancestor refs index, normally tags
	QVector<int> descBranches; // list of descendant branches index
	int descRefsMaster; // in case of many Rev have the same descRefs, ancRefs or
	int ancRefsMaster;  // descBranches these are stored only once in a Rev pointed
	int descBrnMaster;  // by corresponding index xxxMaster

private:
	int indexData();
	const QString mid(int start, int len) const;

	const QByteArray& ba; // reference here!
	const uint start;
	uint parentsCnt, boundaryOfs;
	int autStart, autLen, autDateStart, autDateLen;
	int sLogStart, sLogLen, lLogStart, lLogLen;
};
typedef QHash<QString, const Rev*> RevMap;  // faster then a map

class RevFile {

	friend class Cache; // to directly load status
	friend class Git;

	// Status information is splitted in a flags vector and in a string
	// vector in 'status' are stored flags according to the info returned
	// by 'git diff-tree' without -C option.
	// In case of a working dir file an IN_INDEX flag is or-ed togheter in
	// case file is present in git index.
	// If file is renamed or copied an entry in 'extStatus' stores the
	// value returned by 'git diff-tree -C' plus source and destination
	// files info.
	// When status of all the files is 'modified' then onlyModified is
	// set, this let us to do some optimization in this common case
	bool onlyModified;
	QVector<int> status;
	QVector<QString> extStatus;

	// prevent implicit C++ compiler defaults
	RevFile(const RevFile&);
	RevFile& operator=(const RevFile&);
public:

	enum StatusFlag {
		MODIFIED = 1,
		DELETED  = 2,
		NEW      = 4,
		RENAMED  = 8,
		COPIED   = 16,
		UNKNOWN  = 32,
		IN_INDEX = 64,
		ANY      = 128
	};

	RevFile() : onlyModified(true) {}
	QVector<int> dirs; // index of a string vector
	QVector<int> names;
	QVector<int> mergeParent;

	// helper functions
	int count() const { return dirs.count(); }
	bool statusCmp(int idx, StatusFlag sf) const {

		return ((onlyModified ? MODIFIED : status.at(idx)) & sf);
	}
	const QString extendedStatus(int idx) const {

		return (!extStatus.isEmpty() ? extStatus.at(idx) : "");
	}
};
typedef QHash<QString, const RevFile*> RevFileMap;

class FileAnnotation {
public:
	explicit FileAnnotation(int id) : isValid(false), annId(id) {}
	FileAnnotation() : isValid(false) {}
	QLinkedList<QString> lines;
	bool isValid;
	int annId;
	QString fileSha;
};

class BaseEvent: public QEvent {
public:
	BaseEvent(SCRef d, int id) : QEvent((QEvent::Type)id), payLoad(d) {}
	const QString myData() const { return payLoad; }
private:
	const QString payLoad; // passed by copy
};

#define DEF_EVENT(X, T) class X : public BaseEvent { public:        \
                        explicit X (SCRef d) : BaseEvent(d, T) {} }

DEF_EVENT(MessageEvent, QGit::MSG_EV);
DEF_EVENT(AnnotateProgressEvent, QGit::ANN_PRG_EV);

class DeferredPopupEvent : public BaseEvent {
public:
	DeferredPopupEvent(SCRef msg, int type) : BaseEvent(msg, type) {}
};

class MainExecErrorEvent : public BaseEvent {
public:
	MainExecErrorEvent(SCRef c, SCRef e) : BaseEvent("", QGit::ERROR_EV), cmd(c), err(e) {}
	const QString command() const { return cmd; }
	const QString report() const { return err; }
private:
	const QString cmd, err;
};

#endif
