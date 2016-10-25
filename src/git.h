/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef GIT_H
#define GIT_H

#include "exceptionmanager.h"
#include "common.h"

template <class, class> struct QPair;
class QRegExp;
class QTextCodec;
class Annotate;
//class DataLoader;
class Domain;
class FileHistory;
class Lanes;
class MyProcess;


class Git : public QObject {
Q_OBJECT
public:
	explicit Git(QObject* parent);

	// used as self-documenting boolean parameters
	static const bool optSaveCache   = true;
        static const bool optGoDown      = true; //CT TODO private, enum
	static const bool optOnlyLoaded  = true;
	static const bool optDragDrop    = true;
	static const bool optFold        = true;
        static const bool optAmend       = true; //CT TODO enum
        static const bool optOnlyInIndex = true; //CT TODO private, enum

	enum RefType {
		TAG        = 1,
		BRANCH     = 2,
		RMT_BRANCH = 4,
		CUR_BRANCH = 8,
		REF        = 16,
		APPLIED    = 32,
		UN_APPLIED = 64,
		ANY_REF    = 127
	};

	struct TreeEntry {
		TreeEntry(const QString& n, const QString& s, const QString& t) : name(n), sha(s), type(t) {}
		bool operator<(const TreeEntry&) const;
		QString name;
		QString sha;
		QString type;
	};
	typedef QList<TreeEntry> TreeInfo;

	void setDefaultModel(FileHistory* fh) { revData = fh; }
	void checkEnvironment();
    void userInfo(QStringList& info);
	const QStringList getGitConfigList(bool global);
        bool getGitDBDir(const QString& wd, QString& gd, bool& changed);
        bool getBaseDir(const QString& wd, QString& bd, bool& changed);
	bool init(const QString& wd, bool range, const QStringList* args, bool overwrite, bool* quit);
	void stop(bool saveCache);
	void setThrowOnStop(bool b);
	bool isThrowOnStopRaised(int excpId, const QString& curContext);
	void setLane(const QString& sha, FileHistory* fh);
	Annotate* startAnnotate(FileHistory* fh, QObject* guiObj);
	const FileAnnotation* lookupAnnotation(Annotate* ann, const QString& sha);
	void cancelAnnotate(Annotate* ann);
	bool startFileHistory(const QString& sha, const QString& startingFileName, FileHistory* fh);
	void cancelDataLoading(const FileHistory* fh);
	void cancelProcess(MyProcess* p);
	bool isCommittingMerge() const { return isMergeHead; }
	bool isStGITStack() const { return isStGIT; }
	bool isPatchName(const QString& nm);
	bool isSameFiles(const QString& tree1Sha, const QString& tree2Sha);
	static bool isImageFile(const QString& file);
	static bool isBinaryFile(const QString& file);
	bool isNothingToCommit();
	bool isUnknownFiles() const { return (workingDirInfo.otherFiles.count() > 0); }
	bool isTextHighlighter() const { return isTextHighlighterFound; }
	const QString textHighlighterVersion() const { return textHighlighterVersionFound; }
	bool isMainHistory(const FileHistory* fh) { return (fh == revData); }
	bool isContiguous(const QStringList &revs);
	const QString getWorkDirDiff(const QString& fileName = "");
	MyProcess* getFile(const QString& fileSha, QObject* receiver, QByteArray* result, const QString& fileName);
	MyProcess* getHighlightedFile(const QString& fileSha, QObject* receiver, QString* result, const QString& fileName);
	const QString getFileSha(const QString& file, const QString& revSha);
	bool saveFile(const QString& fileSha, const QString& fileName, const QString& path);
	void getFileFilter(const QString& path, ShaSet& shaSet) const;
	bool getPatchFilter(const QString& exp, bool isRegExp, ShaSet& shaSet);
	const RevFile* getFiles(const QString& sha, const QString& sha2 = "", bool all = false, const QString& path = "");
	bool getTree(const QString& ts, TreeInfo& ti, bool wd, const QString& treePath);
	static const QString getLocalDate(const QString& gitDate);
	const QString getCurrentBranchName() const {return curBranchName;}
	const QString getDesc(const QString& sha, QRegExp& slogRE, QRegExp& lLogRE, bool showH, FileHistory* fh);
	const QString getLastCommitMsg();
	const QString getNewCommitMsg();
	const QString getLaneParent(const QString& fromSHA, int laneNum);
        const QStringList getChildren(const QString& parent);
	const QStringList getNearTags(bool goDown, const QString& sha);
	const QStringList getDescendantBranches(const QString& sha, bool shaOnly = false);
	const QString getShortLog(const QString& sha);
	const QString getTagMsg(const QString& sha);
	const Rev* revLookup(const QString& sha, const FileHistory* fh = NULL) const;
	uint checkRef(const ShaString& sha, uint mask = ANY_REF) const;
	uint checkRef(const QString& sha, uint mask = ANY_REF) const;
	const QString getRevInfo(const QString& sha);
	const QString getRefSha(const QString& refName, RefType type = ANY_REF, bool askGit = true);
	const QStringList getRefNames(SCRef sha, uint mask = ANY_REF) const;
	const QStringList getAllRefNames(uint mask, bool onlyLoaded);
	const QStringList getAllRefSha(uint mask);
	const QStringList sortShaListByIndex(const QStringList& shaList);
    void getWorkDirFiles(QStringList& files, QStringList& dirs, RevFile::StatusFlag status);
	QTextCodec* getTextCodec(bool* isGitArchive);
	bool formatPatch(const QStringList& shaList, const QString& dirPath, const QString& remoteDir = "");
	bool updateIndex(const QStringList& selFiles);
	bool commitFiles(const QStringList& files, const QString& msg, bool amend);
	bool applyPatchFile(const QString& patchPath, bool fold, bool sign);
	bool resetCommits(int parentDepth);
	bool merge(SCRef into, SCList sources, QString* error=NULL);
	bool stgPush(const QString& sha);
	bool stgPop(const QString& sha);
	void setTextCodec(QTextCodec* tc);
	void addExtraFileInfo(QString* rowName, const QString& sha, const QString& diffToSha, bool allMergeFiles);
	void removeExtraFileInfo(QString* rowName);
	void formatPatchFileHeader(QString* rowName, const QString& sha, const QString& dts, bool cmb, bool all);
	int findFileIndex(const RevFile& rf, const QString& name);
	const QString filePath(const RevFile& rf, uint i) const {

		return dirNamesVec[rf.dirAt(i)] + fileNamesVec[rf.nameAt(i)];
	}
	void setCurContext(Domain* d) { curDomain = d; }
	Domain* curContext() const { return curDomain; }

signals:
	void newRevsAdded(const FileHistory*, const QVector<ShaString>&);
	void loadCompleted(const FileHistory*, const QString&);
	void cancelLoading(const FileHistory*);
	void cancelAllProcesses();
	void annotateReady(Annotate*, bool, const QString&);
	void fileNamesLoad(int, int);
	void changeFont(const QFont&);

public slots:
	void procReadyRead(const QByteArray&);
	void procFinished();

private slots:
	void loadFileCache();
	void loadFileNames();
	void on_runAsScript_eof();
	void on_getHighlightedFile_eof();
	void on_newDataReady(const FileHistory*);
	void on_loaded(FileHistory*, ulong,int,bool,const QString&,const QString&);

private:
	friend class MainImpl;
	friend class DataLoader;
	friend class ConsoleImpl;
	friend class RevsView;

        struct Reference { // stores tag information associated to a revision
                Reference() : type(0) {}
                uint type;
                QStringList branches;
                QStringList remoteBranches;
                QStringList tags;
                QStringList refs;
                QString     tagObj; // TODO support more then one obj
                QString     tagMsg;
                QString     stgitPatch;
        };
        typedef QHash<ShaString, Reference> RefMap;

        struct WorkingDirInfo {
		void clear() { diffIndex = diffIndexCached = ""; otherFiles.clear(); }
		QString diffIndex;
		QString diffIndexCached;
		QStringList otherFiles;
	};
	WorkingDirInfo workingDirInfo;

	struct LoadArguments { // used to pass arguments to init2()
		QStringList args;
		bool filteredLoading;
		QStringList filterList;
	};
	LoadArguments loadArguments;

	struct FileNamesLoader {
		FileNamesLoader() : rf(NULL) {}

		RevFile* rf;
		QVector<int> rfDirs;
		QVector<int> rfNames;
	};
	FileNamesLoader fileLoader;

	void init2();
	bool run(const QString& cmd, QString* out = NULL, QObject* rcv = NULL, const QString& buf = "");
	bool run(QByteArray* runOutput, const QString& cmd, QObject* rcv = NULL, const QString& buf = "");
	MyProcess* runAsync(const QString& cmd, QObject* rcv, const QString& buf = "");
	MyProcess* runAsScript(const QString& cmd, QObject* rcv = NULL, const QString& buf = "");
	const QStringList getArgs(bool* quit, bool repoChanged);
	bool getRefs();
	void parseStGitPatches(const QStringList& patchNames, const QStringList& patchShas);
	void clearRevs();
	void clearFileNames();
	bool startRevList(const QStringList& args, FileHistory* fh);
	bool startUnappliedList();
	bool startParseProc(const QStringList& initCmd, FileHistory* fh, const QString& buf);
	bool tryFollowRenames(FileHistory* fh);
	bool populateRenamedPatches(const QString& sha, const QStringList& nn, FileHistory* fh, QStringList* on, bool bt);
	bool filterEarlyOutputRev(FileHistory* fh, Rev* rev);
	int addChunk(FileHistory* fh, const QByteArray& ba, int ofs);
	void parseDiffFormat(RevFile& rf, const QString& buf, FileNamesLoader& fl);
	void parseDiffFormatLine(RevFile& rf, const QString& line, int parNum, FileNamesLoader& fl);
	void getDiffIndex();
	Rev* fakeRevData(const QString& sha, const QStringList& parents, const QString& author, const QString& date, const QString& log,
                         const QString& longLog, const QString& patch, int idx, FileHistory* fh);
	const Rev* fakeWorkDirRev(const QString& parent, const QString& log, const QString& longLog, int idx, FileHistory* fh);
	const RevFile* fakeWorkDirRevFile(const WorkingDirInfo& wd);
	bool copyDiffIndex(FileHistory* fh, const QString& parent);
	const RevFile* insertNewFiles(const QString& sha, const QString& data);
	const RevFile* getAllMergeFiles(const Rev* r);
	bool runDiffTreeWithRenameDetection(const QString& runCmd, QString* runOutput);
	bool isParentOf(const QString& par, const QString& child);
	bool isTreeModified(const QString& sha);
	void indexTree();
	void updateDescMap(const Rev* r, uint i, QHash<QPair<uint, uint>,bool>& dm,
	                   QHash<uint, QVector<int> >& dv);
	void mergeNearTags(bool down, Rev* p, const Rev* r, const QHash<QPair<uint, uint>, bool>&dm);
	void mergeBranches(Rev* p, const Rev* r);
	void updateLanes(Rev& c, Lanes& lns, const QString& sha);
	bool mkPatchFromWorkDir(const QString& msg, const QString& patchFile, const QStringList& files);
	const QStringList getOthersFiles();
	const QStringList getOtherFiles(const QStringList& selFiles, bool onlyInIndex);
	const QString getNewestFileName(const QStringList& args, const QString& fileName);
	static const QString colorMatch(const QString& txt, QRegExp& regExp);
	void appendFileName(RevFile& rf, const QString& name, FileNamesLoader& fl);
	void flushFileNames(FileNamesLoader& fl);
	void populateFileNamesMap();
	const QString formatList(const QStringList& sl, const QString& name, bool inOneLine = true);
	static const QString quote(const QString& nm);
	static const QString quote(const QStringList& sl);
	static const QStringList noSpaceSepHack(const QString& cmd);
	void removeDeleted(const QStringList& selFiles);
	void setStatus(RevFile& rf, const QString& rowSt);
	void setExtStatus(RevFile& rf, const QString& rowSt, int parNum, FileNamesLoader& fl);
	void appendNamesWithId(QStringList& names, const QString& sha, const QStringList& data, bool onlyLoaded);
        Reference* lookupReference(const ShaString& sha);
        Reference* lookupOrAddReference(const ShaString& sha);

	EM_DECLARE(exGitStopped);

	Domain* curDomain;
	QString workDir; // workDir is always without trailing '/'
	QString gitDir;
	QString filesLoadingPending;
	QString filesLoadingCurSha;
	QString curBranchName;
	int filesLoadingStartOfs;
	bool cacheNeedsUpdate;
	bool errorReportingEnabled;
	bool isMergeHead;
	bool isStGIT;
	bool isGIT;
	bool isTextHighlighterFound;
	QString textHighlighterVersionFound;
	bool loadingUnAppliedPatches;
	bool fileCacheAccessed;
	int patchesStillToFind;
	QString firstNonStGitPatch;
	RevFileMap revsFiles;
	RefMap refsShaMap;
	StrVect fileNamesVec;
	StrVect dirNamesVec;
	QHash<QString, int> fileNamesMap; // quick lookup file name
	QHash<QString, int> dirNamesMap;  // quick lookup directory name
	FileHistory* revData;
};

#endif
