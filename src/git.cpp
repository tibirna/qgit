/*
	Description: interface to git programs

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QDateTime>
#include <QRegExp>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QSettings>
#include <QTextDocument>
#include <QTextCodec>
#include <QImageReader>
#include "lanes.h"
#include "myprocess.h"
#include "annotate.h"
#include "cache.h"
#include "git.h"

using namespace QGit;

FileHistory::FileHistory(QObject* p, Git* g) : QAbstractItemModel(p), git(g) {

	_headerInfo << "Graph" << "Id" << "Short Log" << "Author" << "Author Date";
	lns = new Lanes();
	revs.reserve(QGit::MAX_DICT_SIZE);
	clear(); // after _headerInfo is set

	connect(git, SIGNAL(newRevsAdded(const FileHistory*, const QVector<QString>&)),
	        this, SLOT(on_newRevsAdded(const FileHistory*, const QVector<QString>&)));
}

FileHistory::~FileHistory() {

	clear();
	delete lns;
}

int FileHistory::rowCount(const QModelIndex& parent) const {

	return (!parent.isValid() ? _rowCnt : 0);
}

bool FileHistory::hasChildren(const QModelIndex & parent) const {

	return !parent.isValid();
}

int FileHistory::row(SCRef sha) const {

	const Rev* r = git->revLookup(sha, this);
	return (r ? r->orderIdx : -1);
}

const QString FileHistory::sha(int row) const {

	return (row < 0 || row >= _rowCnt ? "" : revOrder.at(row));
}

void FileHistory::clear() {

	git->cancelDataLoading(this);
	qDeleteAll(revs);
	revs.clear();
	revOrder.clear();
	firstFreeLane = 0;
	lns->clear();
	_fileName = "";
	qDeleteAll(rowData);
	rowData.clear();

	if (testFlag(REL_DATE_F)) {
		_secs = QDateTime::currentDateTime().toTime_t();
		_headerInfo[4] = "Last Change";
	} else {
		_secs = 0;
		_headerInfo[4] = "Author Date";
	}
	_rowCnt = revOrder.count();
	_annIdValid = false;
	reset();
}

void FileHistory::on_newRevsAdded(const FileHistory* f, const QVector<QString>& shaVec) {

	if (f != this) // signal newRevsAdded() is broadcast
		return;

	beginInsertRows(QModelIndex(), _rowCnt, shaVec.count());
	_rowCnt = shaVec.count();
	endInsertRows();
}

Qt::ItemFlags FileHistory::flags(const QModelIndex& index) const {

	if (!index.isValid())
		return Qt::ItemIsEnabled;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable; // read only
}

QVariant FileHistory::headerData(int section, Qt::Orientation orientation, int role) const {

	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
		return _headerInfo.at(section);

	return QVariant();
}

QModelIndex FileHistory::index(int row, int column, const QModelIndex&) const {
/*
	index() is called much more then data(), also by a 100X factor on
	big archives, so we use just the row number as QModelIndex payload
	and defer the revision lookup later, inside data().
	Because row and column info are	stored anyway in QModelIndex we
	don't need to add any additional data.
*/
	if (row < 0 || row >= _rowCnt)
		return QModelIndex();

	return createIndex(row, column, 0);
}

QModelIndex FileHistory::parent(const QModelIndex&) const {

	return QModelIndex();
}

const QString FileHistory::timeDiff(unsigned long secs) const {

	uint days  =  secs / (3600 * 24);
	uint hours = (secs - days * 3600 * 24) / 3600;
	uint min   = (secs - days * 3600 * 24 - hours * 3600) / 60;
	uint sec   =  secs - days * 3600 * 24 - hours * 3600 - min * 60;
	QString tmp;
	if (days > 0)
		tmp.append(QString::number(days) + "d ");

	if (hours > 0 || !tmp.isEmpty())
		tmp.append(QString::number(hours) + "h ");

	if (min > 0 || !tmp.isEmpty())
		tmp.append(QString::number(min) + "m ");

	tmp.append(QString::number(sec) + "s");
	return tmp;
}

QVariant FileHistory::data(const QModelIndex& index, int role) const {

	if (!index.isValid() || role != Qt::DisplayRole)
		return QVariant();

	const Rev* r = git->revLookup(revOrder.at(index.row()), this);
	if (!r)
		return QVariant();

	int col = index.column();

	// calculate lanes
	if (r->lanes.count() == 0)
		git->setLane(r->sha(), const_cast<FileHistory*>(this));

	if (col == QGit::ANN_ID_COL)
		return (_annIdValid ? _rowCnt - index.row() : QVariant());

	if (col == QGit::LOG_COL)
		return r->shortLog();

	if (col == QGit::AUTH_COL)
		return r->author();

	if (col == QGit::TIME_COL && r->sha() != QGit::ZERO_SHA) {

		if (_secs != 0) // secs is 0 for absolute date
			return timeDiff(_secs - r->authorDate().toULong());
		else
			return git->getLocalDate(r->authorDate());
	}
	return QVariant();
}

// ****************************************************************************

Git::Git(QObject* p) : QObject(p) {

	EM_INIT(exGitStopped, "Stopping connection with git");

	fileCacheAccessed = cacheNeedsUpdate = isMergeHead = false;
	isStGIT = isGIT = loadingUnAppliedPatches = isTextHighlighterFound = false;
	errorReportingEnabled = true; // report errors if run() fails
	curDomain = NULL;
	revData = NULL;
	revsFiles.reserve(MAX_DICT_SIZE);
}

void Git::checkEnvironment() {

	QString version;
	if (!run("git --exec-path", &version)) {
		dbs("Cannot found git files");
		return;
	}
	QGit::GIT_DIR = version.trimmed();

	if (run("git --version", &version)) {

		version = version.section(' ', -1, -1).section('.', 0, 2);
		if (version < GIT_VERSION) {

			// simply send information, the 'not compatible version'
			// policy should be implemented upstream
			const QString cmd("Current git version is " + version +
			      " but is required " + GIT_VERSION + " or better");

			const QString errorDesc("Your installed git is too old."
			      "\nPlease upgrade to avoid possible misbehaviours.");

			MainExecErrorEvent* e = new MainExecErrorEvent(cmd, errorDesc);
			QApplication::postEvent(parent(), e);
		}
	}
	errorReportingEnabled = false;
	isTextHighlighterFound = run("source-highlight -V", &version);
	errorReportingEnabled = true;
	if (isTextHighlighterFound)
		dbp("Found %1", version.section('\n', 0, 0));
}

void Git::userInfo(SList info) {
/*
  git looks for commit user information in following order:

	- GIT_AUTHOR_NAME and GIT_AUTHOR_EMAIL environment variables
	- repository config file
	- global config file
	- your name, hostname and domain
*/
	const QString env(QProcess::systemEnvironment().join(","));
	QString user(env.section("GIT_AUTHOR_NAME", 1).section(",", 0, 0).section("=", 1).trimmed());
	QString email(env.section("GIT_AUTHOR_EMAIL", 1).section(",", 0, 0).section("=", 1).trimmed());

	info.clear();
	info << "Environment" << user << email;

	errorReportingEnabled = false; // 'git repo-config' could fail, see docs

	run("git repo-config user.name", &user);
	run("git repo-config user.email", &email);
	info << "Local config" << user << email;

	run("git repo-config --global user.name", &user);
	run("git repo-config --global user.email", &email);
	info << "Global config" << user << email;

	errorReportingEnabled = true;
}

bool Git::isImageFile(SCRef file) {

	const QString ext(file.section('.', -1).toLower());
	return QImageReader::supportedImageFormats().contains(ext.toAscii());
}

bool Git::isBinaryFile(SCRef file) {

	static const char* binaryFileExtensions[] = {"bmp", "gif", "jpeg", "jpg",
	                   "png", "svg", "tiff", "pcx", "xcf", "xpm",
	                   "bz", "bz2", "rar", "tar", "z", "gz", "tgz", "zip", 0};

	if (isImageFile(file))
		return true;

	const QString ext(file.section('.', -1).toLower());
	int i = 0;
	while (binaryFileExtensions[i] != 0)
		if (ext == binaryFileExtensions[i++])
			return true;
	return false;
}

void Git::setThrowOnStop(bool b) {

	if (b)
		EM_REGISTER(exGitStopped);
	else
		EM_REMOVE(exGitStopped);
}

bool Git::isThrowOnStopRaised(int excpId, SCRef curContext) {

	return EM_MATCH(excpId, exGitStopped, curContext);
}

void Git::setTextCodec(QTextCodec* tc) {

	QTextCodec::setCodecForCStrings(tc); // works also with tc == 0 (Latin1)
	QString name(tc ? tc->name() : "Latin1");

	// workaround Qt issue of mime name different from
	// standard http://www.iana.org/assignments/character-sets
	if (name == "Big5-HKSCS")
		name = "Big5";

	run("git repo-config i18n.commitencoding " + name);
}

QTextCodec* Git::getTextCodec(bool* isGitArchive) {

	*isGitArchive = isGIT;
	if (!isGIT) // can be called also when not in an archive
		return NULL;

	QString runOutput;
	if (!run("git repo-config --get i18n.commitencoding", &runOutput))
		return NULL;

	if (runOutput.isEmpty()) // git docs says default is utf-8
		return QTextCodec::codecForName(QByteArray("utf8"));

	return QTextCodec::codecForName(runOutput.trimmed().toLatin1());
}

const QString Git::quote(SCRef nm) {

	return (QUOTE_CHAR + nm + QUOTE_CHAR);
}

const QString Git::quote(SCList sl) {

	QString q(sl.join(QUOTE_CHAR + ' ' + QUOTE_CHAR));
	q.prepend(QUOTE_CHAR).append(QUOTE_CHAR);
	return q;
}

const QString Git::getLocalDate(SCRef gitDate) {

	QDateTime d;
	d.setTime_t(gitDate.toULong());
	return d.toString(Qt::LocalDate);
}

uint Git::checkRef(SCRef sha, uint mask) const {

	QMap<QString, Reference>::const_iterator it(refsShaMap.find(sha));
	return (it != refsShaMap.constEnd() ? (*it).type & mask : 0);
}

const QStringList Git::getRefName(SCRef sha, RefType type, QString* curBranch) const {

	if (!checkRef(sha, type))
		return QStringList();

	const Reference& rf = refsShaMap[sha];

	if (curBranch)
		*curBranch = rf.currentBranch;

	if (type == TAG)
		return rf.tags;

	else if (type == BRANCH)
		return rf.branches;

	else if (type == RMT_BRANCH)
		return rf.remoteBranches;

	else if (type == REF)
		return rf.refs;

	else if (type == APPLIED || type == UN_APPLIED)
		return QStringList(rf.stgitPatch);

	return QStringList();
}

const QStringList Git::getAllRefSha(uint mask) {

	QStringList shas;
	FOREACH (RefMap, it, refsShaMap)
		if ((*it).type & mask)
			shas.append(it.key());
	return shas;
}

const QString Git::getRefSha(SCRef refName, RefType type, bool askGit) {

	bool any = (type == ANY_REF);

	FOREACH (RefMap, it, refsShaMap) {

	        const Reference& rf = *it;

		if ((any || type == TAG) && rf.tags.contains(refName))
			return it.key();

		else if ((any || type == BRANCH) && rf.branches.contains(refName))
			return it.key();

		else if ((any || type == RMT_BRANCH) && rf.remoteBranches.contains(refName))
			return it.key();

		else if ((any || type == REF) && rf.refs.contains(refName))
			return it.key();

		else if ((any || type == APPLIED || type == UN_APPLIED) && rf.stgitPatch == refName)
			return it.key();
	}
	if (!askGit)
		return "";

	// if a ref was not found perhaps is an abbreviated form
	QString runOutput;
	errorReportingEnabled = false;
	bool ok = run("git rev-parse " + refName, &runOutput);
	errorReportingEnabled = true;
	return (ok ? runOutput.trimmed() : "");
}

void Git::appendNamesWithId(QStringList& names, SCRef sha, SCList data, bool onlyLoaded) {

	const Rev* r = revLookup(sha);
	if (onlyLoaded && !r)
		return;

	if (onlyLoaded) { // prepare for later sorting
		SCRef cap = QString("%1 ").arg(r->orderIdx, 6);
		FOREACH_SL (it, data)
			names.append(cap + *it);
	} else
		names += data;
}

const QStringList Git::getAllRefNames(uint mask, bool onlyLoaded) {
// returns reference names sorted by loading order if 'onlyLoaded' is set

	QStringList names;
	FOREACH (RefMap, it, refsShaMap) {

		if (mask & TAG)
			appendNamesWithId(names, it.key(), (*it).tags, onlyLoaded);

		if (mask & BRANCH)
			appendNamesWithId(names, it.key(), (*it).branches, onlyLoaded);

		if (mask & RMT_BRANCH)
			appendNamesWithId(names, it.key(), (*it).remoteBranches, onlyLoaded);

		if (mask & REF)
			appendNamesWithId(names, it.key(), (*it).refs, onlyLoaded);

		if ((mask & (APPLIED | UN_APPLIED)) && !onlyLoaded)
			names.append((*it).stgitPatch); // doesn't work with 'onlyLoaded'
        }
        if (onlyLoaded) {
		names.sort();
		QStringList::iterator itN(names.begin());
		for ( ; itN != names.end(); ++itN) // strip 'idx'
			(*itN) = (*itN).section(' ', -1, -1);
	}
	return names;
}

const QString Git::getRevInfo(SCRef sha) {

	uint type = checkRef(sha);
	if (type == 0)
		return "";

	QString refsInfo;
	if (type & BRANCH) {
		const QString cap(type & CUR_BRANCH ? "Head: " : "Branch: ");
		refsInfo =  cap + getRefName(sha, BRANCH).join(" ");
	}
	if (type & RMT_BRANCH)
		refsInfo.append("   Remote branch: " + getRefName(sha, RMT_BRANCH).join(" "));

	if (type & TAG)
		refsInfo.append("   Tag: " + getRefName(sha, TAG).join(" "));

	if (type & REF)
		refsInfo.append("   Ref: " + getRefName(sha, REF).join(" "));

	if (type & APPLIED)
		refsInfo.append("   Patch: " + getRefName(sha, APPLIED).join(" "));

	if (type & UN_APPLIED)
		refsInfo.append("   Patch: " + getRefName(sha, UN_APPLIED).join(" "));

	if (type & TAG) {
		SCRef msg(getTagMsg(sha));
		if (!msg.isEmpty())
			refsInfo.append("  [" + msg + "]");
	}
	return refsInfo.trimmed();
}

const QString Git::getTagMsg(SCRef sha) {

	if (!checkRef(sha, TAG)) {
		dbs("ASSERT in Git::getTagMsg, tag not found");
		return "";
	}
	Reference& rf = refsShaMap[sha];

	if (!rf.tagMsg.isEmpty())
		return rf.tagMsg;

	QRegExp pgp("-----BEGIN PGP SIGNATURE*END PGP SIGNATURE-----",
	            Qt::CaseSensitive, QRegExp::Wildcard);

	if (!rf.tagObj.isEmpty()) {
		QString ro;
		if (run("git cat-file tag " + rf.tagObj, &ro))
			rf.tagMsg = ro.section("\n\n", 1).remove(pgp).trimmed();
	}
	return rf.tagMsg;
}

bool Git::isPatchName(SCRef nm) {

	if (!getRefSha(nm, UN_APPLIED, false).isEmpty())
		return true;

	return !getRefSha(nm, APPLIED, false).isEmpty();
}

void Git::addExtraFileInfo(QString* rowName, SCRef sha, SCRef diffToSha, bool allMergeFiles) {

	const RevFile* files = getFiles(sha, diffToSha, allMergeFiles);
	if (!files)
		return;

	int idx = findFileIndex(*files, *rowName);
	if (idx == -1)
		return;

	QString extSt(files->extendedStatus(idx));
	if (extSt.isEmpty())
		return;

	*rowName = extSt;
}

void Git::removeExtraFileInfo(QString* rowName) {

	if (rowName->contains(" --> ")) // return destination file name
		*rowName = rowName->section(" --> ", 1, 1).section(" (", 0, 0);
}

void Git::formatPatchFileHeader(QString* rowName, SCRef sha, SCRef diffToSha,
                                bool combined, bool allMergeFiles) {
	if (combined) {
		rowName->prepend("diff --combined ");
		return; // TODO rename/copy still not supported in this case
	}
	// let's see if it's a rename/copy...
	addExtraFileInfo(rowName, sha, diffToSha, allMergeFiles);

	if (rowName->contains(" --> ")) { // ...it is!

		SCRef destFile(rowName->section(" --> ", 1, 1).section(" (", 0, 0));
		SCRef origFile(rowName->section(" --> ", 0, 0));
		*rowName = "diff --git a/" + origFile + " b/" + destFile;
	} else
		*rowName = "diff --git a/" + *rowName + " b/" + *rowName;
}

MyProcess* Git::startPatchLoading(SCList shaList, SCRef fileName, QObject* receiver) {

	QString runCmd("git diff-tree -r -m --patch-with-raw --no-commit-id --stdin -- ");
	runCmd.append(quote(fileName));
	return runAsync(runCmd, receiver, shaList.join("\n").append('\n'));
}

Annotate* Git::startAnnotate(FileHistory* fh, QObject* guiObj) { // non blocking

	Annotate* ann = new Annotate(this, guiObj);
	if (!ann->start(fh)) // non blocking call
		return NULL; // ann will delete itself when done

	return ann; // caller will delete with Git::cancelAnnotate()
}

void Git::cancelAnnotate(Annotate* ann) {

	if (ann)
		ann->deleteWhenDone();
}

void Git::annotateFinished(Annotate* ann) {

	SCRef msg = QString("Annotated %1 files in %2 ms")
	            .arg(ann->count()).arg(ann->elapsed());

	emit annotateReady(ann, ann->file(), ann->isValid(), msg);
}

const FileAnnotation* Git::lookupAnnotation(Annotate* ann, SCRef fileName, SCRef sha) {

	return (ann ? ann->lookupAnnotation(sha, fileName) : NULL);
}

void Git::cancelDataLoading(const FileHistory* fh) {
// normally called when closing file viewer

	emit cancelLoading(fh); // non blocking
}

const Rev* Git::revLookup(SCRef sha, const FileHistory* fh) const {

	const RevMap& r = (fh ? fh->revs : revData->revs);
	return (r.contains(sha) ? r[sha] : NULL);
}

bool Git::run(SCRef runCmd, QString* runOutput, QObject* receiver, SCRef buf) {

	QByteArray ba;
	bool ret = run(runOutput ? &ba : NULL, runCmd, receiver, buf);
	if (runOutput)
		*runOutput = ba;

	return ret;
}

bool Git::run(QByteArray* runOutput, SCRef runCmd, QObject* receiver, SCRef buf) {

	MyProcess p(parent(), this, workDir, errorReportingEnabled);
	return p.runSync(runCmd, runOutput, receiver, buf);
}

MyProcess* Git::runAsync(SCRef runCmd, QObject* receiver, SCRef buf) {

	MyProcess* p = new MyProcess(parent(), this, workDir, errorReportingEnabled);
	if (!p->runAsync(runCmd, receiver, buf)) {
		delete p;
		p = NULL;
	}
	return p; // auto-deleted when done
}

MyProcess* Git::runAsScript(SCRef runCmd, QObject* receiver, SCRef buf) {

	const QString scriptFile(workDir + "/qgit_script" + QGit::SCRIPT_EXT);
	if (!writeToFile(scriptFile, runCmd, true))
		return NULL;

	MyProcess* p = runAsync(scriptFile, receiver, buf);
	if (p)
		connect(p, SIGNAL(eof()), this, SLOT(on_runAsScript_eof()));
	return p;
}

void Git::on_runAsScript_eof() {

	QDir dir(workDir);
	dir.remove("qgit_script" + QGit::SCRIPT_EXT);
}

void Git::cancelProcess(MyProcess* p) {

	if (p)
		p->on_cancel(); // non blocking call
}

int Git::findFileIndex(const RevFile& rf, SCRef name) {

	if (name.isEmpty())
		return -1;

	int idx = name.lastIndexOf('/') + 1;
	SCRef dr = name.left(idx);
	SCRef nm = name.mid(idx);

	for (uint i = 0, cnt = rf.count(); i < cnt; ++i) {
		if (fileNamesVec[rf.names[i]] == nm && dirNamesVec[rf.dirs[i]] == dr)
			return i;
	}
	return -1;
}

const QString Git::getLaneParent(SCRef fromSHA, int laneNum) {

	const Rev* rs = revLookup(fromSHA);
	if (!rs)
		return "";

	for (int idx = rs->orderIdx - 1; idx >= 0; idx--) {

		const Rev* r = revLookup(revData->revOrder[idx]);
		if (laneNum >= r->lanes.count())
			return "";

		if (!isFreeLane(r->lanes[laneNum])) {

			int type = r->lanes[laneNum], parNum = 0;
			while (!isMerge(type) && type != ACTIVE) {

				if (isHead(type))
					parNum++;

				type = r->lanes[--laneNum];
			}
			return r->parent(parNum);
		}
	}
	return "";
}

const QStringList Git::getChilds(SCRef parent) {

	QStringList childs;
	const Rev* r = revLookup(parent);
	if (!r)
		return childs;

	for (int i = 0; i < r->childs.count(); i++)
		childs.append(revData->revOrder[r->childs[i]]);

	// reorder childs by loading order
	QStringList::iterator itC(childs.begin());
	for ( ; itC != childs.end(); ++itC) {
		const Rev* r = revLookup(*itC);
		(*itC).prepend(QString("%1 ").arg(r->orderIdx, 6));
	}
	childs.sort();
	for (itC = childs.begin(); itC != childs.end(); ++itC)
		(*itC) = (*itC).section(' ', -1, -1);

	return childs;
}

const QString Git::getShortLog(SCRef sha) {

	const Rev* r = revLookup(sha);
	return (r ? r->shortLog() : "");
}

MyProcess* Git::getDiff(SCRef sha, QObject* receiver, SCRef diffToSha, bool combined) {

	if (sha.isEmpty())
		return NULL;

	QString runCmd;
	if (sha != ZERO_SHA) {
		runCmd = "git diff-tree -r --patch-with-stat ";
		runCmd.append(combined ? "-c " : "-C -m "); // TODO rename for combined
		runCmd.append(diffToSha + " " + sha); // diffToSha could be empty
	} else
		runCmd = "git diff-index -r -m --patch-with-stat HEAD";

	return runAsync(runCmd, receiver);
}

const QString Git::getWorkDirDiff(SCRef fileName) {

	QString runCmd("git diff-index -r -m -p HEAD"), runOutput;
	if (!fileName.isEmpty())
		runCmd.append(" -- " + quote(fileName));

	if (!run(runCmd, &runOutput))
		return "";

	return 	runOutput;
}

const QString Git::getFileSha(SCRef file, SCRef revSha) {

	if (revSha == ZERO_SHA) {
		QStringList files, dummy;
		getWorkDirFiles(files, dummy, RevFile::ANY);
		if (files.contains(file))
			return ZERO_SHA; // it is unknown to git
	}
	const QString sha(revSha == ZERO_SHA ? "HEAD" : revSha);
	QString runCmd("git ls-tree -r " + sha + " " + quote(file)), runOutput;
	if (!run(runCmd, &runOutput))
		return "";

	return runOutput.mid(12, 40); // could be empty, deleted file case
}

MyProcess* Git::getFile(SCRef file, SCRef revSha, QObject* receiver, QByteArray* result, QString* fSha) {

	QString runCmd;
	/*
	  symlinks in git are one line files with just the name of the target,
	  not the target content. Instead 'cat' command resolves symlinks and
	  returns target content. So we use 'cat' only if the file is modified
	  in working dir, to let annotation work for changed files, otherwise
	  we go with a safe 'git cat-file blob HEAD' instead.
	  NOTE: This fails if the modified file is a new symlink, converted
	  from an old plain file. In this case annotation will fail until
	  change is committed.
	*/
	const QString fileSha(getFileSha(file, revSha));
	if (fileSha == ZERO_SHA)
		runCmd = "cat " + quote(file);
	else {
		if (fileSha.isEmpty()) // deleted
			runCmd = "git diff-tree HEAD HEAD"; // fake an empty file reading
		else
			runCmd = "git cat-file blob " + fileSha;
	}
	if (fSha)
		*fSha = fileSha;

	if (!receiver) {
		run(result, runCmd);
		return NULL; // in case of sync call we ignore run() return value
	}
	return runAsync(runCmd, receiver);
}

MyProcess* Git::getHighlightedFile(SCRef file, SCRef sha, QObject* receiver, QString* result) {

	if (!isTextHighlighter()) {
		dbs("ASSERT in getHighlightedFile: highlighter not found");
		return NULL;
	}
	QString ext(file.section('.', -1, -1, QString::SectionIncludeLeadingSep));
	QString inputFile(workDir + "/qgit_hlght_input" + ext);
	if (!saveFile(file, sha, inputFile))
		return NULL;

	QString runCmd("source-highlight --failsafe -f html -i " + quote(inputFile));

	if (!receiver) {
		run(runCmd, result);
		on_getHighlightedFile_eof();
		return NULL; // in case of sync call we ignore run() return value
	}
	MyProcess* p = runAsync(runCmd, receiver);
	if (p)
		connect(p, SIGNAL(eof()), this, SLOT(on_getHighlightedFile_eof()));
	return p;
}

void Git::on_getHighlightedFile_eof() {

	QDir dir(workDir);
	const QStringList sl(dir.entryList(QStringList() << "qgit_hlght_input*"));
	FOREACH_SL (it, sl)
		dir.remove(*it);
}

bool Git::saveFile(SCRef file, SCRef sha, SCRef path) {

	QByteArray fileData;
	getFile(file, sha, NULL, &fileData); // sync call
	if (isBinaryFile(file))
		return writeToFile(path, fileData);

	return writeToFile(path, QString(fileData));
}

bool Git::getTree(SCRef treeSha, SList names, SList shas,
                  SList types, bool isWorkingDir, SCRef treePath) {

	QStringList newFiles, unfiles, delFiles, dummy;
	if (isWorkingDir) { // retrieve unknown and deleted files under treePath

		getWorkDirFiles(unfiles, dummy, RevFile::UNKNOWN);
		FOREACH_SL (it, unfiles) { // don't add unknown files under other directories
			QFileInfo f(*it);
			SCRef d(f.dir().path());
			if (d == treePath || (treePath.isEmpty() && d == "."))
				newFiles.append(f.fileName());
		}
		getWorkDirFiles(delFiles, dummy, RevFile::DELETED);
	}
	// if needed fake a working directory tree starting from HEAD tree
	const QString tree(treeSha == ZERO_SHA ? "HEAD" : treeSha);
	QString runOutput;
	if (!run("git ls-tree " + tree, &runOutput))
		return false;

	const QStringList sl(runOutput.split('\n', QString::SkipEmptyParts));
	FOREACH_SL (it, sl) {
		// insert in order any good unknown file to the list,
		// newFiles must be already sorted
		SCRef fn((*it).section('\t', 1, 1));
		while (!newFiles.empty() && newFiles.first() < fn) {
			names.append(newFiles.first());
			shas.append("");
			types.append("?"); // FIXME test
			newFiles.pop_front();
		}
		// append any not deleted file
		SCRef fp(treePath.isEmpty() ? fn : treePath + '/' + fn);
		if (delFiles.empty() || (delFiles.indexOf(fp) == -1)) {
			names.append(fn);
			shas.append((*it).mid(12, 40));
			types.append((*it).mid(7, 4));
		}
	}
	while (!newFiles.empty()) { // append any remaining unknown file
		names.append(newFiles.first());
		shas.append("");
		types.append("?"); // FIXME test
		newFiles.pop_front();
	}
	return true;
}

void Git::getWorkDirFiles(SList files, SList dirs, RevFile::StatusFlag status) {

	files.clear();
	dirs.clear();
	const RevFile* f = getFiles(ZERO_SHA);
	if (!f)
		return;

	for (int i = 0; i < f->count(); i++) {

		if (f->statusCmp(i, status)) {

			SCRef fp(filePath(*f, i));
			files.append(fp);
			for (int j = 0, cnt = fp.count('/'); j < cnt; j++) {

				SCRef dir(fp.section('/', 0, j));
				if (dirs.indexOf(dir) == -1)
					dirs.append(dir);
			}
		}
	}
}

bool Git::isNothingToCommit() {

	if (!revsFiles.contains(ZERO_SHA))
		return true;

	const RevFile* rf = revsFiles[ZERO_SHA];
	return (rf->count() == _wd.otherFiles.count());
}

bool Git::isTreeModified(SCRef sha) {

	const RevFile* f = getFiles(sha);
	if (!f)
		return true; // no files info, stay on the safe side

	for (int i = 0; i < f->count(); ++i)
		if (!f->statusCmp(i, RevFile::MODIFIED))
			return true;

	return false;
}

bool Git::isParentOf(SCRef par, SCRef child) {

	const Rev* c = revLookup(child);
	return (c && c->parentsCount() == 1 && c->parent(0) == par); // no merges
}

bool Git::isSameFiles(SCRef tree1Sha, SCRef tree2Sha) {

	// early skip common case of browsing with up and down arrows, i.e.
	// going from parent(child) to child(parent). In this case we can
	// check RevFileMap and skip a costly 'git diff-tree' call.
	if (isParentOf(tree1Sha, tree2Sha))
		return !isTreeModified(tree2Sha);

	if (isParentOf(tree2Sha, tree1Sha))
		return !isTreeModified(tree1Sha);

	const QString runCmd("git diff-tree -r " + tree1Sha + " " + tree2Sha);
	QString runOutput;
	if (!run(runCmd, &runOutput))
		return false;

	bool isChanged = (runOutput.indexOf(" A\t") != -1 || runOutput.indexOf(" D\t") != -1);
	return !isChanged;
}

const QStringList Git::getDescendantBranches(SCRef sha) {

	QStringList tl;
	const Rev* r = revLookup(sha);
	if (!r || (r->descBrnMaster == -1))
		return tl;

	const QVector<int>& nr = revLookup(revData->revOrder[r->descBrnMaster])->descBranches;

	for (int i = 0; i < nr.count(); i++) {

		SCRef sha = revData->revOrder[nr[i]];
		SCRef cap = " (" + sha + ") ";
		RefMap::const_iterator it(refsShaMap.find(sha));
		if (it == refsShaMap.constEnd())
			continue;

		if (!(*it).branches.empty())
			tl.append((*it).branches.join(cap).append(cap));

		if (!(*it).remoteBranches.empty())
			tl.append((*it).remoteBranches.join(cap).append(cap));
	}
	return tl;
}

const QStringList Git::getNearTags(bool goDown, SCRef sha) {

	QStringList tl;
	const Rev* r = revLookup(sha);
	if (!r)
		return tl;

	int nearRefsMaster = (goDown ? r->descRefsMaster : r->ancRefsMaster);
	if (nearRefsMaster == -1)
		return tl;

	const QVector<int>& nr = goDown ? revLookup(revData->revOrder[nearRefsMaster])->descRefs :
	                                  revLookup(revData->revOrder[nearRefsMaster])->ancRefs;

	for (int i = 0; i < nr.count(); i++) {

		SCRef sha = revData->revOrder[nr[i]];
		SCRef cap = " (" + sha + ")";
		RefMap::const_iterator it(refsShaMap.find(sha));
		if (it != refsShaMap.constEnd())
			tl.append((*it).tags.join(cap).append(cap));
	}
	return tl;
}

const QString Git::getDefCommitMsg() {

	QString sha(ZERO_SHA);
	if (isStGIT && !getAllRefSha(APPLIED).isEmpty()) {
		QString top;
		if (run("stg top", &top))
			sha = getRefSha(top.trimmed(), APPLIED, false);
	}
	const Rev* c = revLookup(sha);
	if (!c) {
		dbp("ASSERT: getDefCommitMsg sha <%1> not found", sha);
		return "";
	}
	if (sha == ZERO_SHA)
		return c->longLog();

	return c->shortLog() + '\n' + c->longLog().trimmed();
}

const QString Git::colorMatch(SCRef txt, QRegExp& regExp) {

	QString text;

	text = Qt::escape( txt );

	if (regExp.isEmpty())
		return text;

	SCRef startCol(QString::fromLatin1("<b><font color=\"red\">"));
	SCRef endCol(QString::fromLatin1("</font></b>"));
	int pos = 0;
	while ((pos = text.indexOf(regExp, pos)) != -1) {

		SCRef match(regExp.cap(0));
		const QString coloredText(startCol + match + endCol);
		text.replace(pos, match.length(), coloredText);
		pos += coloredText.length();
	}
	return text;
}

const QString Git::getDesc(SCRef sha, QRegExp& shortLogRE, QRegExp& longLogRE, bool showHeader) {

	if (sha.isEmpty())
		return "";

	const Rev* c = revLookup(sha);
	if (!c)            // sha of a not loaded revision, as
		return ""; // example asked from file history

	QString text;
	if (c->isDiffCache)
		text = Qt::convertFromPlainText( c->longLog() );
	else {
		QTextStream ts(&text);
		ts << "<html><head><style type=\"text/css\">"
		        "tr.head { background-color: #a0a0e0 }\n"
		        "td.h { font-weight: bold; }\n"
		        "table { background-color: #e0e0f0; }\n"
		        "span.h { font-weight: bold; font-size: medium; }\n"
		        "div.l { white-space: pre; "
		        "font-family: " << TYPE_WRITER_FONT.family() << ";"
		        "font-size: " << TYPE_WRITER_FONT.pointSize() << "pt;}\n"
		        "</style></head><body><div class='t'>\n"
		        "<table border=0 cellspacing=0 cellpadding=2>";

		if (showHeader) {

			ts << "<tr class='head'> <th></th> <th><span class='h'>"
				<< colorMatch(c->shortLog(), shortLogRE)
				<< "</span></th></tr>";

			ts << "<tr> <td class='h'>Author</td><td>"
				<< Qt::escape(c->author()) << "</td>"
				"</tr><tr> <td class='h'>Date</td><td>"
				<< getLocalDate(c->authorDate()) << "</td></tr>";

			if (!c->isUnApplied && !c->isApplied) {
				ts << "<tr><td class='h'>Parent</td><td>"
				<< c->parents().join("</td></tr>\n<tr><td class='h'>Parent</td> <td>");
				ts << "</td></tr>\n";

				QStringList sl = getChilds(sha);
				if (!sl.isEmpty()) {
					ts << "<tr><td class='h'>Child</td><td>"
					<< sl.join("</td></tr>\n<tr><td class='h'>Child</td> <td>")
					<< "</td></tr>\n";
				}
				sl = getDescendantBranches(sha);
				if (!sl.empty()) {
					ts << "<tr><td class='h'>Branch</td><td>"
					<< sl.join("</td> </tr>\n<tr><td class='h'>Branch</td> <td>")
					<< "</td></tr>\n";
				}
				sl = getNearTags(!optGoDown, sha);
				if (!sl.isEmpty()) {
					ts << "<tr><td class='h'>Follows</td> <td>"
					<< sl.join(", ") << "</td></tr>\n";
				}
				sl = getNearTags(optGoDown, sha);
				if (!sl.isEmpty()) {
					ts << "<tr><td class='h'>Precedes</td> <td>"
					<< sl.join(", ") << "</td></tr>\n";
				}
			}
		}
		QString log(colorMatch(c->longLog(), longLogRE));
		if (!showHeader && log.trimmed().isEmpty())
			log = colorMatch(c->shortLog(), shortLogRE);

		ts << "</table></div><div class='l'>" << log << "</div></body></html>";
	}
	// highlight SHA's
	//
	// added to commit logs, we avoid to call git rev-parse for a possible abbreviated
	// sha if there isn't a leading trailing space or an open parenthesis and,
	// in that case, before the space must not be a ':' character.
	// It's an ugly heuristic, but seems to work in most cases.
	QRegExp reSHA("..[0-9a-f]{21,40}|[^:][\\s(][0-9a-f]{6,20}", Qt::CaseInsensitive);
	reSHA.setMinimal(false);
	int pos = 0;
	while ((pos = text.indexOf(reSHA, pos)) != -1) {

		SCRef ref = reSHA.cap(0).mid(2);
		const Rev* r = (ref.length() == 40 ? revLookup(ref) : revLookup(getRefSha(ref)));
		if (r) {
			QString slog(r->shortLog());
			if (slog.isEmpty()) // very rare but possible
				slog = r->sha();
			if (slog.length() > 60)
				slog = slog.left(57).trimmed().append("...");

			slog = Qt::escape(slog);
			const QString link("<a href=\"" + r->sha() + "\">" + slog + "</a>");
			text.replace(pos + 2, ref.length(), link);
			pos += link.length();
		} else
			pos += reSHA.cap(0).length();
	}
	return text;
}

const RevFile* Git::insertNewFiles(SCRef sha, SCRef data) {

	RevFile* rf = new RevFile();
	parseDiffFormat(*rf, data);
	revsFiles.insert(sha, rf);
	return rf;
}

const RevFile* Git::getAllMergeFiles(const Rev* r) {

	SCRef mySha(ALL_MERGE_FILES + r->sha());
	if (revsFiles.contains(mySha))
		return revsFiles[mySha];

	QString runCmd("git diff-tree -r -m -C " + r->sha()), runOutput;
	if (!run(runCmd, &runOutput))
		return NULL;

	return insertNewFiles(mySha, runOutput);
}

const RevFile* Git::getFiles(SCRef sha, SCRef diffToSha, bool allFiles, SCRef path) {

	const Rev* r = revLookup(sha);
	if (!r)
		return NULL;

	if (r->parentsCount() == 0) // skip initial rev
		return NULL;

	if (r->parentsCount() > 1 && diffToSha.isEmpty() && allFiles)
		return getAllMergeFiles(r);

	if (!diffToSha.isEmpty() && (sha != ZERO_SHA)) {

		QString runCmd("git diff-tree -r -m -C ");
		runCmd.append(diffToSha + " " + sha);
		if (!path.isEmpty())
			runCmd.append(" " + path);

		QString runOutput;
		if (!run(runCmd, &runOutput))
			return NULL;

		// we insert a dummy revision file object. It will be
		// overwritten at each request but we don't care.
		return insertNewFiles(CUSTOM_SHA, runOutput);
	}
	if (revsFiles.contains(sha))
		return revsFiles[sha]; // ZERO_SHA search arrives here

	if (sha == ZERO_SHA) {
		dbs("ASSERT in Git::getFiles, ZERO_SHA not found");
		return NULL;
	}
	QString runCmd("git diff-tree -r -c -C " + sha), runOutput;
	if (!run(runCmd, &runOutput))
		return NULL;

	if (revsFiles.contains(sha)) // has been created in the mean time?
		return revsFiles[sha];

	cacheNeedsUpdate = true;
	return insertNewFiles(sha, runOutput);
}

bool Git::startFileHistory(FileHistory* fh) {

	return startRevList(QStringList(fh->fileName()), fh);
}

void Git::getFileFilter(SCRef path, QMap<QString, bool>& shaMap) {

	shaMap.clear();
	QRegExp rx(path, Qt::CaseInsensitive, QRegExp::Wildcard);
	FOREACH (StrVect, it, revData->revOrder) {

		if (!revsFiles.contains(*it))
			continue;

		// case insensitive, wildcard search
		const RevFile* rf = revsFiles[*it];
		for (int i = 0; i < rf->count(); ++i)
			if (filePath(*rf, i).indexOf(rx) != -1) {
				shaMap.insert(*it, true);
				break;
			}
	}
}

bool Git::getPatchFilter(SCRef exp, bool isRegExp, QMap<QString, bool>& shaMap) {

	shaMap.clear();
	QString buf;
	FOREACH (StrVect, it, revData->revOrder)
		if (*it != ZERO_SHA)
			buf.append(*it).append('\n');

	if (buf.isEmpty())
		return true;

	QString runCmd("git diff-tree -r -s --stdin "), runOutput;
	if (isRegExp)
		runCmd.append("--pickaxe-regex ");

	runCmd.append(quote("-S" + exp));
	if (!run(runCmd, &runOutput, NULL, buf)) // could be slow
		return false;

	const QStringList sl(runOutput.split('\n', QString::SkipEmptyParts));
	FOREACH_SL (it, sl)
		shaMap.insert(*it, true);

	return true;
}

bool Git::resetCommits(int parentDepth) {

	QString runCmd("git reset --soft HEAD~");
	runCmd.append(QString::number(parentDepth));
	return run(runCmd);
}

bool Git::applyPatchFile(SCRef patchPath, bool commit, bool fold, bool isDragDrop) {

	if (commit && isStGIT) {
		if (fold)
			return run("stg fold " + quote(patchPath));

		return run("stg import --mail " + quote(patchPath));
	}
	QString runCmd("git am --utf8 --3way ");
	if (isDragDrop)
		runCmd.append("--keep ");

	else if (testFlag(SIGN_PATCH_F))
		runCmd.append("--signoff ");

	return run(runCmd + quote(patchPath));
}

bool Git::formatPatch(SCList shaList, SCRef dirPath, SCRef remoteDir) {

	bool remote = !remoteDir.isEmpty();
	QSettings settings;
	const QString FPArgs(settings.value(PATCH_ARGS_KEY).toString());

	QString runCmd("git format-patch");
	if (testFlag(NUMBERS_F) && !remote)
		runCmd.append(" -n");

	if (remote)
		runCmd.append(" --keep-subject");

	runCmd.append(" -o " + dirPath);
	if (!FPArgs.isEmpty())
		runCmd.append(" " + FPArgs);

	runCmd.append(" --start-number=");

	const QString tmp(workDir);
	if (remote)
		workDir = remoteDir; // run() uses workDir value

	int n = shaList.count();
	bool ret = false;
	FOREACH_SL (it, shaList) { // shaList is ordered by newest to oldest
		const QString cmd(runCmd + QString::number(n) + " " +
		                  *it + QString::fromLatin1("^..") + *it);
		n--;
		ret = run(cmd);
		if (!ret)
			break;
	}
	workDir = tmp;
	return ret;
}

bool Git::updateIndex(SCList selFiles) {

	if (selFiles.empty())
		return true;

	QString runCmd("git update-index --add --remove --replace -- ");
	runCmd.append(quote(selFiles));
	return run(runCmd);
}

bool Git::commitFiles(SCList selFiles, SCRef msg) {
/*
	Part of this code is taken from Fredrik Kuivinen "Gct" tool
*/
	const QString msgFile(gitDir + "/qgit_cmt_msg");
	if (!writeToFile(msgFile, msg)) // early skip
		return false;

	// add user selectable commit options
	QSettings settings;
	const QString CMArgs(settings.value(CMT_ARGS_KEY).toString());

	QString cmtOptions;
	if (!CMArgs.isEmpty())
		cmtOptions.append(" " + CMArgs);

	if (testFlag(SIGN_CMT_F))
		cmtOptions.append(" -s");

	if (testFlag(VERIFY_CMT_F))
		cmtOptions.append(" -v");

	// extract not selected files already updated
	// in index, save them to restore at the end
	const QStringList notSelInIndexFiles(getOtherFiles(selFiles, optOnlyInIndex));

	// extract selected NOT to be deleted files to
	// later feed git commit. Files to be deleted
	// should avoid going through 'git commit'
	QStringList selNotDelFiles;
	const RevFile* files = getFiles(ZERO_SHA); // files != NULL
	FOREACH_SL (it, selFiles) {
		int idx = findFileIndex(*files, *it);
		if (!files->statusCmp(idx, RevFile::DELETED))
			selNotDelFiles.append(*it);
	}
	// test if we need a git read-tree to temporary
	// remove not selected files from index
	if (!notSelInIndexFiles.empty())
		if (!run("git read-tree --reset HEAD"))
			return false;

	// before to commit we have to update index with all
	// the selected files because git commit doesn't
	// use --add flag
	updateIndex(selFiles);

	// now we can commit, 'git commit' will update index
	// with selected (not to be deleted) files for us
	QString runCmd("git commit" + cmtOptions + " -F " + msgFile);
	if (!selNotDelFiles.empty())
		runCmd.append(" -i " + quote(selNotDelFiles));

	if (!run(runCmd))
		return false;

	// finally restore not selected files in index
	if (!notSelInIndexFiles.empty())
		if (!updateIndex(notSelInIndexFiles))
			return false;

	QDir dir(workDir);
	dir.remove(msgFile);
	return true;
}

bool Git::mkPatchFromIndex(SCRef msg, SCRef patchFile) {

	QString runOutput;
	if (!run("git diff-index --cached -p HEAD", &runOutput))
		return false;

	const QString patch("Subject: " + msg + "\n---\n" + runOutput);
	return writeToFile(patchFile, patch);
}

const QStringList Git::getOtherFiles(SCList selFiles, bool onlyInIndex) {

	const RevFile* files = getFiles(ZERO_SHA); // files != NULL
	QStringList notSelFiles;
	for (int i = 0; i < files->count(); ++i) {
		SCRef fp = filePath(*files, i);
		if (selFiles.indexOf(fp) == -1) { // not selected...
			if (!onlyInIndex)
				notSelFiles.append(fp);
			else if (files->statusCmp(i, RevFile::IN_INDEX))
				notSelFiles.append(fp);
		}
	}
	return notSelFiles;
}

void Git::removeFiles(SCList selFiles, SCRef workDir, SCRef ext) {

	QDir d(workDir);
	FOREACH_SL (it, selFiles)
		d.rename(*it, *it + ext);
}

void Git::restoreFiles(SCList selFiles, SCRef workDir, SCRef ext) {

	QDir d(workDir);
	FOREACH_SL (it, selFiles)
		d.rename(*it + ext, *it); // overwrites any existent file
}

void Git::removeDeleted(SCList selFiles) {

	QDir dir(workDir);
	const RevFile* files = getFiles(ZERO_SHA); // files != NULL
	FOREACH_SL (it, selFiles) {
		int idx = findFileIndex(*files, *it);
		if (files->statusCmp(idx, RevFile::DELETED))
			dir.remove(*it);
	}
}

bool Git::stgCommit(SCList selFiles, SCRef msg, SCRef patchName, bool fold) {

	// here the deal is to create a patch with the diffs between the
	// updated index and HEAD, then resetting the index and working
	// dir to HEAD so to have a clean tree, then import/fold the patch
	bool retval = true;
	const QString patchFile(gitDir + "/qgit_tmp_patch");
	const QString extNS(".qgit_removed_not_selected");
	const QString extS(".qgit_removed_selected");

	// we have selected modified files in selFiles, we still need
	// to know the modified but not selected files and, among
	// these the cached ones to properly restore state at the end.
	const QStringList notSelFiles = getOtherFiles(selFiles, !optOnlyInIndex);
	const QStringList notSelInIndexFiles = getOtherFiles(selFiles, optOnlyInIndex);

	// update index with selected files
	if (!run("git read-tree --reset HEAD"))
		goto error;
	if (!updateIndex(selFiles))
		goto error;

	// create a patch with diffs between index and HEAD
	if (!mkPatchFromIndex(msg, patchFile))
		goto error;

	// temporary remove files according to their type
	removeFiles(selFiles, workDir, extS); // to use in case of rollback
	removeFiles(notSelFiles, workDir, extNS); // to restore at the end

	// checkout index to have a clean tree
	if (!run("git read-tree --reset HEAD"))
		goto error;
	if (!run("git checkout-index -q -f -u -a"))
		goto rollback;

	// finally import/fold the patch
	if (fold) {
		// update patch message before to fold so to use refresh only as a rename tool
		if (!msg.isEmpty()) {
			if (!run("stg refresh --message \"" + msg.trimmed() + "\""))
				goto rollback;
		}
		if (!run("stg fold " + quote(patchFile)))
			goto rollback;
		if (!run("stg refresh")) // refresh needed after fold
			goto rollback;
	} else {
		if (!run("stg import --mail --name " + quote(patchName) + " " + quote(patchFile)))
			goto rollback;
	}
	goto exit;

rollback:
	restoreFiles(selFiles, workDir, extS);
	removeDeleted(selFiles); // remove files to be deleted from working tree

error:
	retval = false;

exit:
	// it is safe to call restore() also if back-up files don't
	// exist, so we can 'goto exit' from anywhere.
	restoreFiles(notSelFiles, workDir, extNS);
	updateIndex(notSelInIndexFiles);
	QDir dir(workDir);
	dir.remove(patchFile);
	FOREACH_SL (it, selFiles)
		dir.remove(*it + extS); // remove temporary backup rollback files
	return retval;
}

bool Git::makeTag(SCRef sha, SCRef tagName, SCRef msg) {

	if (msg.isEmpty())
		return run("git tag " + tagName + " " + sha);

	return run("git tag -m \"" + msg + "\" " + tagName + " " + sha);
}

bool Git::deleteTag(SCRef sha) {

	const QStringList tags(getRefName(sha, TAG));
	if (!tags.empty())
		return run("git tag -d " + tags.first()); // only one

	return false;
}

bool Git::stgPush(SCRef sha) {

	const QStringList patch(getRefName(sha, UN_APPLIED));
	if (patch.count() != 1) {
		dbp("ASSERT in Git::stgPush, found %1 patches instead of 1", patch.count());
		return false;
	}
	return run("stg push " + quote(patch.first()));
}

bool Git::stgPop(SCRef sha) {

	QString top;
	if (!run("stg top", &top))
		return false;

	const QStringList patch(getRefName(sha, APPLIED));
	if (patch.count() != 1) {
		dbp("ASSERT in Git::stgPop, found %1 patches instead of 1", patch.count());
		return false;
	}
	if (patch.first() != top.trimmed())
		if (!run("stg pop " + quote(patch)))
			return false;

	return run("stg pop"); // finally remove selected one
}
