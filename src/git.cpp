/*
	Description: interface to git programs

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QPalette>
#include <QRegularExpression>
//#include <QSet> //CT TODO remove
#include <QSettings>
#include <QTextCodec>
#include <QTextDocument>
#include <QTextStream>
#include "FileHistory.h"
#include "annotate.h"
#include "cache.h"
#include "dataloader.h"
#include "git.h"
#include "lanes.h"
#include "myprocess.h"
#include "rangeselectimpl.h"

#define SHOW_MSG(x) QApplication::postEvent(parent(), new MessageEvent(x)); EM_PROCESS_EVENTS_NO_INPUT;

#define GIT_LOG_FORMAT "%m%HX%PX%n%cn<%ce>%n%an<%ae>%n%at%n%s%n"

// Used on init() for reading parameters once;
// It's OK to be unique among qgit windows.
static bool startup = true;

using namespace QGit;


// ****************************************************************************

bool Git::TreeEntry::operator<(const TreeEntry& te) const {

	if (this->type == te.type)
		return( this->name.localeAwareCompare( te.name ) < 0 );

	// directories are smaller then files
	// to appear as first when sorted
	if (this->type == "tree")
		return true;

	if (te.type == "tree")
		return false;

	return( this->name.localeAwareCompare( te.name ) < 0 );
}

Git::Git(QObject* p) : QObject(p) {

	EM_INIT(exGitStopped, "Stopping connection with git");

	fileCacheAccessed = cacheNeedsUpdate = isMergeHead = false;
	isStGIT = isGIT = loadingUnAppliedPatches = isTextHighlighterFound = false;
	errorReportingEnabled = true; // report errors if run() fails
	curDomain = NULL;
	shortHashLen = shortHashLenDefault;
	revData = NULL;
	revsFiles.reserve(MAX_DICT_SIZE);
}

void Git::checkEnvironment() {

	QString version;
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
	} else {
		dbs("Cannot find git files");
		return;
	}
	errorReportingEnabled = false;
	isTextHighlighterFound = run("source-highlight -V", &version);
	errorReportingEnabled = true;
	if (isTextHighlighterFound)
		textHighlighterVersionFound = version.section('\n', 0, 0);
	else
		textHighlighterVersionFound = "GNU source-highlight not installed";
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

	errorReportingEnabled = false; // 'git config' could fail, see docs

	run("git config user.name", &user);
	run("git config user.email", &email);
	info << "Local config" << user << email;

	run("git config --global user.name", &user);
	run("git config --global user.email", &email);
	info << "Global config" << user << email;

	errorReportingEnabled = true;
}

const QStringList Git::getGitConfigList(bool global) {

    QString runOutput;

    errorReportingEnabled = false; // 'git config' could fail, see docs

    if (global)
        run("git config --global --list", &runOutput);
    else
        run("git config --list", &runOutput);

    errorReportingEnabled = true;

    return runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts));
}

bool Git::isImageFile(SCRef file) {

	const QString ext(file.section('.', -1).toLower());
	return QImageReader::supportedImageFormats().contains(ext.toLatin1());
}

//CT TODO investigate if there is a better way of getting this (from git e.g.)
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

	QTextCodec::setCodecForLocale(tc);
	QString name(tc ? tc->name() : "Latin1");

	// workaround Qt issue of mime name different from
	// standard http://www.iana.org/assignments/character-sets
	if (name == "Big5-HKSCS")
		name = "Big5";

	bool dummy;
	if (tc != getTextCodec(&dummy))
		run("git config i18n.commitencoding " + name);
}

QTextCodec* Git::getTextCodec(bool* isGitArchive) {

	*isGitArchive = isGIT;
	if (!isGIT) // can be called also when not in an archive
		return NULL;

	QString runOutput;
	if (!run("git config i18n.commitencoding", &runOutput))
		return NULL;

	if (runOutput.isEmpty()) // git docs says default is utf-8
		return QTextCodec::codecForName(QByteArray("utf8"));

	return QTextCodec::codecForName(runOutput.trimmed().toLatin1());
}

//CT TODO utility function; can go elsewhere
const QString Git::quote(SCRef nm) {

	return (QUOTE_CHAR + nm + QUOTE_CHAR);
}

//CT TODO utility function; can go elsewhere
const QString Git::quote(SCList sl) {

	QString q(sl.join(QUOTE_CHAR + ' ' + QUOTE_CHAR));
	q.prepend(QUOTE_CHAR).append(QUOTE_CHAR);
	return q;
}

uint Git::checkRef(const ShaString& sha, uint mask) const {

	RefMap::const_iterator it(refsShaMap.constFind(sha));
	return (it != refsShaMap.constEnd() ? (*it).type & mask : 0);
}

uint Git::checkRef(SCRef sha, uint mask) const {

	RefMap::const_iterator it(refsShaMap.constFind(toTempSha(sha)));
	return (it != refsShaMap.constEnd() ? (*it).type & mask : 0);
}

const QStringList Git::getRefNames(SCRef sha, uint mask) const {

	QStringList result;
	if (!checkRef(sha, mask))
		return result;

	const Reference& rf = refsShaMap[toTempSha(sha)];

	if (mask & TAG)
		result << rf.tags;

	if (mask & BRANCH)
		result << rf.branches;

	if (mask & RMT_BRANCH)
		result << rf.remoteBranches;

	if (mask & REF)
		result << rf.refs;

	if (mask == APPLIED || mask == UN_APPLIED)
		result << QStringList(rf.stgitPatch);

	return result;
}

const QStringList Git::getAllRefSha(uint mask) {

	QStringList shas;
	FOREACH (RefMap, it, refsShaMap)
		if ((*it).type & mask)
			shas.append(it.key());
	return shas;
}

/*
const QString Git::refAsShortHash(SCRef sha)
{
	QString shortHash;
	const bool success = run("git rev-parse --short " + sha, &shortHash);
	// Fall back to input hash `sha` if rev-parse fails
	return success ? shortHash : sha;
}
*/

int Git::getShortHashLength()
{
	int len = 0;
	QString shortHash;
	if (run("git rev-parse --short HEAD", &shortHash))
		len = shortHash.trimmed().size();   // Result contains a newline.
	return (len > shortHashLenDefault) ? len : shortHashLenDefault;
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
	bool ok = run("git rev-parse --revs-only " + refName, &runOutput);
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

	if (sha.isEmpty())
		return "";

	uint type = checkRef(sha);
	if (type == 0)
		return "";

	QString refsInfo;
	if (type & BRANCH) {
		const QString cap(type & CUR_BRANCH ? "HEAD: " : "Branch: ");
		refsInfo =  cap + getRefNames(sha, BRANCH).join(" ");
	}
	if (type & RMT_BRANCH)
		refsInfo.append("   Remote branch: " + getRefNames(sha, RMT_BRANCH).join(" "));

	if (type & TAG)
		refsInfo.append("   Tag: " + getRefNames(sha, TAG).join(" "));

	if (type & REF)
		refsInfo.append("   Ref: " + getRefNames(sha, REF).join(" "));

	if (type & APPLIED)
		refsInfo.append("   Patch: " + getRefNames(sha, APPLIED).join(" "));

	if (type & UN_APPLIED)
		refsInfo.append("   Patch: " + getRefNames(sha, UN_APPLIED).join(" "));

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
	Reference& rf = refsShaMap[toTempSha(sha)];

	if (!rf.tagMsg.isEmpty())
		return rf.tagMsg;

	QRegularExpression pgp("-----BEGIN PGP SIGNATURE*END PGP SIGNATURE-----",
	            Qt::CaseSensitive, QRegularExpression::Wildcard);

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

const FileAnnotation* Git::lookupAnnotation(Annotate* ann, SCRef sha) {

	return (ann ? ann->lookupAnnotation(sha) : NULL);
}

void Git::cancelDataLoading(const FileHistory* fh) {
// normally called when closing file viewer

	emit cancelLoading(fh); // non blocking
}

const Rev* Git::revLookup(SCRef sha, const FileHistory* fh) const {

	return revLookup(toTempSha(sha), fh);
}

const Rev* Git::revLookup(const ShaString& sha, const FileHistory* fh) const {

	const RevMap& r = (fh ? fh->revs : revData->revs);
	return (sha.latin1() ? r.value(sha) : NULL);
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
#ifndef Q_OS_WIN32
	// without this process doesn't start under Linux
	QString cmd(runCmd.startsWith("#!") ? runCmd : "#!/bin/sh\n" + runCmd);
#else
	QString cmd(runCmd);
#endif
	if (!writeToFile(scriptFile, cmd, true))
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
		if (fileNamesVec[rf.nameAt(i)] == nm && dirNamesVec[rf.dirAt(i)] == dr)
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

const QStringList Git::getChildren(SCRef parent) {

        QStringList children;
	const Rev* r = revLookup(parent);
	if (!r)
                return children;

        for (int i = 0; i < r->children.count(); i++)
                children.append(revData->revOrder[r->children[i]]);

        // reorder children by loading order
        QStringList::iterator itC(children.begin());
        for ( ; itC != children.end(); ++itC) {
		const Rev* r = revLookup(*itC);
		(*itC).prepend(QString("%1 ").arg(r->orderIdx, 6));
	}
        children.sort();
        for (itC = children.begin(); itC != children.end(); ++itC)
		(*itC) = (*itC).section(' ', -1, -1);

        return children;
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
		runCmd = "git diff-tree --no-color -r --patch-with-stat ";
		runCmd.append(combined ? "-c " : "-C -m "); // TODO rename for combined

        const Rev* r = revLookup(sha);
        if (r->parentsCount() == 0)
            runCmd.append("--root ");

		runCmd.append(diffToSha + " " + sha); // diffToSha could be empty
	} else
		runCmd = "git diff-index --no-color -r -m --patch-with-stat HEAD";

	return runAsync(runCmd, receiver);
}

const QString Git::getWorkDirDiff(SCRef fileName) {

	QString runCmd("git diff-index --no-color -r -z -m -p --full-index --no-commit-id HEAD"), runOutput;
	if (!fileName.isEmpty())
		runCmd.append(" -- " + quote(fileName));

	if (!run(runCmd, &runOutput))
		return "";

	/* For unknown reasons file sha of index is not ZERO_SHA but
	   a value of unknown origin.
	   Replace that with ZERO_SHA so to not fool annotate
	*/
	int idx = runOutput.indexOf("..");
	if (idx != -1)
		runOutput.replace(idx + 2, 40, ZERO_SHA);

	return runOutput;
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

MyProcess* Git::getFile(SCRef fileSha, QObject* receiver, QByteArray* result, SCRef fileName) {

	QString runCmd;
	/*
	  symlinks in git are one line files with just the name of the target,
	  not the target content. Instead 'cat' command resolves symlinks and
	  returns target content. So we use 'cat' only if the file is modified
          in working directory, to let annotation work for changed files, otherwise
	  we go with a safe 'git cat-file blob HEAD' instead.
	  NOTE: This fails if the modified file is a new symlink, converted
	  from an old plain file. In this case annotation will fail until
	  change is committed.
	*/
	if (fileSha == ZERO_SHA)

#ifdef Q_OS_WIN32
    {
		QString winPath = quote(fileName);
		winPath.replace("/", "\\");
		runCmd = "type " + winPath;
    }
#else
		runCmd = "cat " + quote(fileName);
#endif

	else {
		if (fileSha.isEmpty()) // deleted
			runCmd = "git diff-tree HEAD HEAD"; // fake an empty file reading
		else
			runCmd = "git cat-file blob " + fileSha;
	}
	if (!receiver) {
		run(result, runCmd);
		return NULL; // in case of sync call we ignore run() return value
	}
	return runAsync(runCmd, receiver);
}

MyProcess* Git::getHighlightedFile(SCRef fileSha, QObject* receiver, QString* result, SCRef fileName) {

	if (!isTextHighlighter()) {
		dbs("ASSERT in getHighlightedFile: highlighter not found");
		return NULL;
	}
	QString ext(fileName.section('.', -1, -1, QString::SectionIncludeLeadingSep));
	QString inputFile(workDir + "/qgit_hlght_input" + ext);
	if (!saveFile(fileSha, fileName, inputFile))
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

bool Git::saveFile(SCRef fileSha, SCRef fileName, SCRef path) {

	QByteArray fileData;
	getFile(fileSha, NULL, &fileData, fileName); // sync call
	if (isBinaryFile(fileName))
		return writeToFile(path, fileData);

	return writeToFile(path, QString(fileData));
}

bool Git::getTree(SCRef treeSha, TreeInfo& ti, bool isWorkingDir, SCRef path) {

	QStringList deleted;
	if (isWorkingDir) {

		// retrieve unknown and deleted files under path
		QStringList unknowns, dummy;
		getWorkDirFiles(unknowns, dummy, RevFile::UNKNOWN);

		FOREACH_SL (it, unknowns) {

			// don't add files under other directories
			QFileInfo f(*it);
			SCRef d(f.dir().path());

			if (d == path || (path.isEmpty() && d == ".")) {
				TreeEntry te(f.fileName(), "", "?");
				ti.append(te);
			}
		}
		getWorkDirFiles(deleted, dummy, RevFile::DELETED);
	}
	// if needed fake a working directory tree starting from HEAD tree
	QString runOutput, tree(treeSha);
	if (treeSha == ZERO_SHA) {
		// HEAD could be empty for just init'ed repositories
		if (!run("git rev-parse --revs-only HEAD", &tree))
			return false;

		tree = tree.trimmed();
	}
	if (!tree.isEmpty() && !run("git ls-tree " + tree, &runOutput))
		return false;

	const QStringList sl(runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts)));
	FOREACH_SL (it, sl) {

		// append any not deleted file
		SCRef fn((*it).section('\t', 1, 1));
		SCRef fp(path.isEmpty() ? fn : path + '/' + fn);

		if (deleted.empty() || (deleted.indexOf(fp) == -1)) {
			TreeEntry te(fn, (*it).mid(12, 40), (*it).mid(7, 4));
			ti.append(te);
		}
	}
	std::sort(ti.begin(), ti.end()); // list directories before files
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

	if (!revsFiles.contains(ZERO_SHA_RAW))
		return true;

	const RevFile* rf = revsFiles[ZERO_SHA_RAW];
	return (rf->count() == workingDirInfo.otherFiles.count());
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
	return (c && c->parentsCount() == 1 && QString(c->parent(0)) == par); // no merges
}

bool Git::isContiguous(const QStringList &revs)
{
	if (revs.count() == 1) return true;
	for (QStringList::const_iterator it=revs.begin(), end=revs.end()-1; it!=end; ++it) {
		const Rev* c = revLookup(*it);
		if (!c->parents().contains(*(it+1))) return false;
	}
	return true;
}

bool Git::isSameFiles(SCRef tree1Sha, SCRef tree2Sha) {

	// early skip common case of browsing with up and down arrows, i.e.
	// going from parent(child) to child(parent). In this case we can
	// check RevFileMap and skip a costly 'git diff-tree' call.
	if (isParentOf(tree1Sha, tree2Sha))
		return !isTreeModified(tree2Sha);

	if (isParentOf(tree2Sha, tree1Sha))
		return !isTreeModified(tree1Sha);

	const QString runCmd("git diff-tree --no-color -r " + tree1Sha + " " + tree2Sha);
	QString runOutput;
	if (!run(runCmd, &runOutput))
		return false;

	bool isChanged = (runOutput.indexOf(" A\t") != -1 || runOutput.indexOf(" D\t") != -1);
	return !isChanged;
}

const QStringList Git::getDescendantBranches(SCRef sha, bool shaOnly) {

	QStringList tl;
	const Rev* r = revLookup(sha);
	if (!r || (r->descBrnMaster == -1))
		return tl;

	const QVector<int>& nr = revLookup(revData->revOrder[r->descBrnMaster])->descBranches;

	for (int i = 0; i < nr.count(); i++) {

		const ShaString& sha = revData->revOrder[nr[i]];
		if (shaOnly) {
			tl.append(sha);
			continue;
		}
		SCRef cap = " (" + sha + ") ";
		RefMap::const_iterator it(refsShaMap.find(sha));
		if (it == refsShaMap.constEnd())
			continue;

		if (!(*it).branches.empty())
			tl.append((*it).branches.join(" ").append(cap));

		if (!(*it).remoteBranches.empty())
			tl.append((*it).remoteBranches.join(" ").append(cap));
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

		const ShaString& sha = revData->revOrder[nr[i]];
		SCRef cap = " (" + sha + ")";
		RefMap::const_iterator it(refsShaMap.find(sha));
		if (it != refsShaMap.constEnd())
			tl.append((*it).tags.join(cap).append(cap));
	}
	return tl;
}

const QString Git::getLastCommitMsg() {

	// FIXME: Make sure the amend action is not called when there is
	// nothing to amend. That is in empty repository or over StGit stack
	// with nothing applied.
	QString sha;
	QString top;
	if (run("git rev-parse --verify HEAD", &top))
	    sha = top.trimmed();
	else {
		dbs("ASSERT: getLastCommitMsg head is not valid");
		return "";
	}

	const Rev* c = revLookup(sha);
	if (!c) {
		dbp("ASSERT: getLastCommitMsg sha <%1> not found", sha);
		return "";
	}

	return c->shortLog() + "\n\n" + c->longLog().trimmed();
}

const QString Git::getNewCommitMsg() {

	const Rev* c = revLookup(ZERO_SHA);
	if (!c) {
		dbs("ASSERT: getNewCommitMsg zero_sha not found");
		return "";
	}
	QString status = c->longLog();
	status.prepend('\n').replace(QRegularExpression("\\n([^#\\n]?)"), "\n#\\1"); // comment all the lines

	if (isMergeHead) {
		QFile file(QDir(gitDir).absoluteFilePath("MERGE_MSG"));
		if (file.open(QIODevice::ReadOnly)) {
			QTextStream in(&file);

			while(!in.atEnd())
				status.prepend(in.readLine());

			file.close();
		}
	}
	return status;
}

//CT TODO utility function; can go elsewhere
const QString Git::colorMatch(SCRef txt, QRegularExpression& regExp) {

	QString text = qt4and5escaping(txt);

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

//CT TODO utility function; can go elsewhere
const QString Git::formatList(SCList sl, SCRef name, bool inOneLine) {

	if (sl.isEmpty())
		return QString();

	QString ls = "<tr><td class='h'>" + name + "</td><td>";
	const QString joinStr = inOneLine ? ", " : "</td></tr>\n" + ls;
	ls += sl.join(joinStr);
	ls += "</td></tr>\n";
	return ls;
}

const QString Git::getDesc(SCRef sha, QRegularExpression& shortLogRE, QRegularExpression& longLogRE,
                           bool showHeader, FileHistory* fh) {

	if (sha.isEmpty())
		return "";

	const Rev* c = revLookup(sha, fh);
	if (!c)            // sha of a not loaded revision, as
		return ""; // example asked from file history

	QString text;
	if (c->isDiffCache)
		text = Qt::convertFromPlainText(c->longLog());
	else {
		QTextStream ts(&text);
		ts << "<html><head><style type=\"text/css\">"
		        "tr.head { background-color: " << QPalette().color(QPalette::Mid).name() << " }\n"
		        "td.h { font-weight: bold; }\n"
		        "table { background-color: " << QPalette().color(QPalette::Button).name() << "; }\n"
		        "span.h { font-weight: bold; font-size: medium; }\n"
		        "div.l { white-space: pre; "
		        "font-family: " << TYPE_WRITER_FONT.family() << ";"
		        "font-size: " << TYPE_WRITER_FONT.pointSize() << "pt;}\n"
		        "</style></head><body><div class='t'>\n"
		        "<table border=0 cellspacing=0 cellpadding=2>";

		ts << "<tr class='head'> <th></th> <th><span class='h'>"
			<< colorMatch(c->shortLog(), shortLogRE)
			<< "</span></th></tr>";

		if (showHeader) {
			if (c->committer() != c->author())
				ts << formatList(QStringList(qt4and5escaping(c->committer())), "Committer");

			ts << formatList(QStringList(qt4and5escaping(c->author())), "Author");
			ts << formatList(QStringList(getLocalDate(c->authorDate())), " Author date");

			if (c->isUnApplied || c->isApplied) {

				QStringList patches(getRefNames(sha, APPLIED));
				patches += getRefNames(sha, UN_APPLIED);
				ts << formatList(patches, "Patch");
			} else {
				ts << formatList(c->parents(), "Parent", false);
                                ts << formatList(getChildren(sha), "Child", false);
				ts << formatList(getDescendantBranches(sha), "Branch", false);
				ts << formatList(getNearTags(!optGoDown, sha), "Follows");
				ts << formatList(getNearTags(optGoDown, sha), "Precedes");
			}
		}
		QString longLog(c->longLog());
		if (showHeader) {
			longLog.prepend(QString("\n") + c->shortLog() + "\n");
		}

		QString log(colorMatch(longLog, longLogRE));
		log.replace("\n", "\n    ").prepend('\n');
		ts << "</table></div><div class='l'>" << log << "</div></body></html>";
	}
	// highlight SHA's
	//
	// added to commit logs, we avoid to call git rev-parse for a possible abbreviated
	// sha if there isn't a leading trailing space or an open parenthesis and,
	// in that case, before the space must not be a ':' character.
	// It's an ugly heuristic, but seems to work in most cases.
	QRegularExpression reSHA("..[0-9a-f]{21,40}|[^:][\\s(][0-9a-f]{6,20}", Qt::CaseInsensitive);
	reSHA.setMinimal(false);
	int pos = 0;
	while ((pos = text.indexOf(reSHA, pos)) != -1) {

		SCRef ref = reSHA.cap(0).mid(2);
		const Rev* r = (ref.length() == 40 ? revLookup(ref) : revLookup(getRefSha(ref)));
		if (r && r->sha() != ZERO_SHA_RAW) {
			QString slog(r->shortLog());
			if (slog.isEmpty()) // very rare but possible
				slog = r->sha();
			if (slog.length() > 60)
				slog = slog.left(57).trimmed().append("...");

			const QString link("<a href=\"" + r->sha() + "\">" + qt4and5escaping(slog) + "</a>");
			text.replace(pos + 2, ref.length(), link);
			pos += link.length();
		} else
			pos += reSHA.cap(0).length();
	}
	return text;
}

const RevFile* Git::insertNewFiles(SCRef sha, SCRef data) {

	/* we use an independent FileNamesLoader to avoid data
	 * corruption if we are loading file names in background
	 */
	FileNamesLoader fl;

	RevFile* rf = new RevFile();
	parseDiffFormat(*rf, data, fl);
	flushFileNames(fl);

	revsFiles.insert(toPersistentSha(sha, revsFilesShaBackupBuf), rf);
	return rf;
}

bool Git::runDiffTreeWithRenameDetection(SCRef runCmd, QString* runOutput) {
/* Under some cases git could warn out:

      "too many files, skipping inexact rename detection"

   So if this occurs fallback on NO rename detection.
*/
	QString cmd(runCmd); // runCmd must be without -C option
	cmd.replace("git diff-tree", "git diff-tree -C");

	errorReportingEnabled = false;
	bool renameDetectionOk = run(cmd, runOutput);
	errorReportingEnabled = true;

	if (!renameDetectionOk) // retry without rename detection
		return run(runCmd, runOutput);

	return true;
}

const RevFile* Git::getAllMergeFiles(const Rev* r) {

	SCRef mySha(ALL_MERGE_FILES + r->sha());
	if (revsFiles.contains(toTempSha(mySha)))
		return revsFiles[toTempSha(mySha)];

	EM_PROCESS_EVENTS; // 'git diff-tree' could be slow

	QString runCmd("git diff-tree --no-color -r -m " + r->sha()), runOutput;
	if (!runDiffTreeWithRenameDetection(runCmd, &runOutput))
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

		QString runCmd("git diff-tree --no-color -r -m ");
		runCmd.append(diffToSha + " " + sha);
		if (!path.isEmpty())
			runCmd.append(" " + path);

		EM_PROCESS_EVENTS; // 'git diff-tree' could be slow

		QString runOutput;
		if (!runDiffTreeWithRenameDetection(runCmd, &runOutput))
			return NULL;

		// we insert a dummy revision file object. It will be
		// overwritten at each request but we don't care.
		return insertNewFiles(CUSTOM_SHA, runOutput);
	}
	if (revsFiles.contains(r->sha()))
		return revsFiles[r->sha()]; // ZERO_SHA search arrives here

	if (sha == ZERO_SHA) {
		dbs("ASSERT in Git::getFiles, ZERO_SHA not found");
		return NULL;
	}

	EM_PROCESS_EVENTS; // 'git diff-tree' could be slow

	QString runCmd("git diff-tree --no-color -r -c " + sha), runOutput;
	if (!runDiffTreeWithRenameDetection(runCmd, &runOutput))
		return NULL;

	if (revsFiles.contains(r->sha())) // has been created in the mean time?
		return revsFiles[r->sha()];

	cacheNeedsUpdate = true;
	return insertNewFiles(sha, runOutput);
}

bool Git::startFileHistory(SCRef sha, SCRef startingFileName, FileHistory* fh) {

	QStringList args(getDescendantBranches(sha, true));
	if (args.isEmpty())
		args << "HEAD";

	QString newestFileName = getNewestFileName(args, startingFileName);
	fh->resetFileNames(newestFileName);

	args.clear(); // load history from all the branches
	args << getAllRefSha(BRANCH | RMT_BRANCH);

	args << "--" << newestFileName;
	return startRevList(args, fh);
}

const QString Git::getNewestFileName(SCList branches, SCRef fileName) {

	QString curFileName(fileName), runOutput, args;
	while (true) {
		args = branches.join(" ") + " -- " + curFileName;
		if (!run("git ls-tree " + args, &runOutput))
			break;

		if (!runOutput.isEmpty())
			break;

		QString msg("Retrieving file renames, now at '" + curFileName + "'...");
		QApplication::postEvent(parent(), new MessageEvent(msg));
		EM_PROCESS_EVENTS_NO_INPUT;

		if (!run("git rev-list -n1 " + args, &runOutput))
			break;

		if (runOutput.isEmpty()) // try harder
			if (!run("git rev-list --full-history -n1 " + args, &runOutput))
				break;

		if (runOutput.isEmpty())
			break;

		SCRef sha = runOutput.trimmed();
		QStringList newCur;
		if (!populateRenamedPatches(sha, QStringList(curFileName), NULL, &newCur, true))
			break;

		curFileName = newCur.first();
	}
	return curFileName;
}

void Git::getFileFilter(SCRef path, ShaSet& shaSet) const {

	shaSet.clear();
	QRegularExpression rx(path, Qt::CaseInsensitive, QRegularExpression::Wildcard);
	FOREACH (ShaVect, it, revData->revOrder) {

		if (!revsFiles.contains(*it))
			continue;

		// case insensitive, wildcard search
		const RevFile* rf = revsFiles[*it];
		for (int i = 0; i < rf->count(); ++i)
			if (filePath(*rf, i).contains(rx)) {
				shaSet.insert(*it);
				break;
			}
	}
}

bool Git::getPatchFilter(SCRef exp, bool isRegExp, ShaSet& shaSet) {

	shaSet.clear();
	QString buf;
	FOREACH (ShaVect, it, revData->revOrder)
		if (*it != ZERO_SHA_RAW)
			buf.append(*it).append('\n');

	if (buf.isEmpty())
		return true;

	EM_PROCESS_EVENTS; // 'git diff-tree' could be slow

	QString runCmd("git diff-tree --no-color -r -s --stdin "), runOutput;
	if (isRegExp)
		runCmd.append("--pickaxe-regex ");

	runCmd.append(quote("-S" + exp));
	if (!run(runCmd, &runOutput, NULL, buf))
		return false;

	const QStringList sl(runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts)));
	FOREACH_SL (it, sl)
		shaSet.insert(*it);

	return true;
}

bool Git::resetCommits(int parentDepth) {

	QString runCmd("git reset --soft HEAD~");
	runCmd.append(QString::number(parentDepth));
	return run(runCmd);
}

bool Git::merge(SCRef into, SCList sources, QString *error)
{
	if (error) *error = "";
	if (!run(QString("git checkout -q %1").arg(into)))
		return false; // failed to checkout

	QString cmd = QString("git merge -q --no-commit ") + sources.join(" ");
	MyProcess p(parent(), this, workDir, false);
	p.runSync(cmd, NULL, NULL, "");

	const QString& e = p.getErrorOutput();
	if (e.contains("stopped before committing as requested"))
		return true;
	if (error) *error = e;
	return false;
}

bool Git::applyPatchFile(SCRef patchPath, bool fold, bool isDragDrop) {

	if (isStGIT) {
		if (fold) {
                        bool ok = run("stg fold " + quote(patchPath)); // merge in working directory
			if (ok)
				ok = run("stg refresh"); // update top patch
			return ok;
		} else
			return run("stg import --mail " + quote(patchPath));
	}
	QString runCmd("git am ");

	QSettings settings;
	const QString APOpt(settings.value(AM_P_OPT_KEY).toString());
	if (!APOpt.isEmpty())
		runCmd.append(APOpt.trimmed() + " ");

	if (isDragDrop)
		runCmd.append("--keep ");

	if (testFlag(SIGN_PATCH_F))
		runCmd.append("--signoff ");

	return run(runCmd + quote(patchPath));
}

const QStringList Git::sortShaListByIndex(SCList shaList) {

	QStringList orderedShaList;
	FOREACH_SL (it, shaList)
		appendNamesWithId(orderedShaList, *it, QStringList(*it), true);

	orderedShaList.sort();
	QStringList::iterator itN(orderedShaList.begin());
	for ( ; itN != orderedShaList.end(); ++itN) // strip 'idx'
		(*itN) = (*itN).section(' ', -1, -1);

        return orderedShaList;
}

bool Git::formatPatch(SCList shaList, SCRef dirPath, SCRef remoteDir) {

	bool remote = !remoteDir.isEmpty();
	QSettings settings;
	const QString FPOpt(settings.value(FMT_P_OPT_KEY).toString());

	QString runCmd("git format-patch --no-color");
	if (testFlag(NUMBERS_F) && !remote)
		runCmd.append(" -n");

	if (remote)
		runCmd.append(" --keep-subject");

	runCmd.append(" -o " + quote(dirPath));
	if (!FPOpt.isEmpty())
		runCmd.append(" " + FPOpt.trimmed());

	const QString tmp(workDir);
	if (remote)
		workDir = remoteDir; // run() uses workDir value

	// shaList is ordered by newest to oldest
	runCmd.append(" " + shaList.last());
	runCmd.append(QString::fromLatin1("^..") + shaList.first());
	bool ret = run(runCmd);
	workDir = tmp;
	return ret;
}

const QStringList Git::getOtherFiles(SCList selFiles, bool onlyInIndex) {

	const RevFile* files = getFiles(ZERO_SHA); // files != NULL
	QStringList notSelFiles;
	for (int i = 0; i < files->count(); ++i) {
		SCRef fp = filePath(*files, i);
		if (selFiles.indexOf(fp) == -1) { // not selected...
			if (!onlyInIndex || files->statusCmp(i, RevFile::IN_INDEX))
				notSelFiles.append(fp);
		}
	}
	return notSelFiles;
}

bool Git::updateIndex(SCList selFiles) {

	const RevFile* files = getFiles(ZERO_SHA); // files != NULL

	QStringList toAdd, toRemove;
	FOREACH_SL (it, selFiles) {
		int idx = findFileIndex(*files, *it);
		if (files->statusCmp(idx, RevFile::DELETED))
			toRemove << *it;
		else
			toAdd << *it;
	}
	if (!toRemove.isEmpty() && !run("git rm --cached --ignore-unmatch -- " + quote(toRemove)))
		return false;

	if (!toAdd.isEmpty() && !run("git add -- " + quote(toAdd)))
		return false;

	return true;
}

bool Git::commitFiles(SCList selFiles, SCRef msg, bool amend) {

	const QString msgFile(gitDir + "/qgit_cmt_msg.txt");
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

	if (amend)
		cmtOptions.append(" --amend");

	bool ret = false;

	// get not selected files but updated in index to restore at the end
	const QStringList notSel(getOtherFiles(selFiles, optOnlyInIndex));

	// call git reset to remove not selected files from index
	if (!notSel.empty() && !run("git reset -- " + quote(notSel)))
		goto fail;

	// update index with selected files
	if (!updateIndex(selFiles))
		goto fail;

	// now we can finally commit..
	if (!run("git commit" + cmtOptions + " -F " + quote(msgFile)))
		goto fail;

	// restore not selected files that were already in index
	if (!notSel.empty() && !updateIndex(notSel))
		goto fail;

	ret = true;
fail:
	QDir dir(workDir);
	dir.remove(msgFile);
	return ret;
}

bool Git::mkPatchFromWorkDir(SCRef msg, SCRef patchFile, SCList files) {

 	/* unfortunately 'git diff' sees only files already
	 * known to git or already in index, so update index first
	 * to be sure also unknown files are correctly found
	 */
 	if (!updateIndex(files))
 		return false;

	QString runOutput;
	if (!run("git diff --no-ext-diff -C HEAD -- " + quote(files), &runOutput))
		return false;

	const QString patch("Subject: " + msg + "\n---\n" + runOutput);
	return writeToFile(patchFile, patch);
}

bool Git::stgCommit(SCList selFiles, SCRef msg, SCRef patchName, bool fold) {

	/* Here the deal is to use 'stg import' and 'stg fold' to add a new
	 * patch or refresh the current one respectively. Unfortunately refresh
	 * does not work with partial selection of files and also does not take
	 * patch message from a file that is needed to avoid artifacts with '\n'
	 * and friends.
	 *
	 * So steps are:
	 *
	 * - Create a patch file with the changes you want to import/fold in StGit
         * - Stash working directory files because import/fold wants a clean directory
	 * - Import/fold the patch
         * - Unstash and merge working directory modified files
	 * - Restore index with not selected files
	 */

	/* Step 1: Create a patch file with the changes you want to import/fold */
	bool ret = false;
	const QString patchFile(gitDir + "/qgit_tmp_patch.txt");

	// in case we don't have files to restore we can shortcut various commands
	bool partialSelection = !getOtherFiles(selFiles, !optOnlyInIndex).isEmpty();

	// get not selected files but updated in index to restore at the end
	QStringList notSel;
	if (partialSelection) // otherwise notSel is for sure empty
		notSel = getOtherFiles(selFiles, optOnlyInIndex);

        // create a patch with diffs between working directory and HEAD
	if (!mkPatchFromWorkDir(msg, patchFile, selFiles))
		goto fail;

        /* Step 2: Stash working directory modified files */
	if (partialSelection) {
		errorReportingEnabled = false;
		run("git stash"); // unfortunately 'git stash' is noisy on stderr
		errorReportingEnabled = true;
	}

	/* Step 3: Call stg import/fold */

	// setup a clean state
	if (!run("stg status --reset"))
		goto fail_and_unstash;

	if (fold) {
		// update patch message before to fold, note that
		// command 'stg edit' requires stg version 0.14 or later
		if (!msg.isEmpty() && !run("stg edit --message " + quote(msg.trimmed())))
			goto fail_and_unstash;

		if (!run("stg fold " + quote(patchFile)))
			goto fail_and_unstash;

		if (!run("stg refresh")) // refresh needed after fold
			goto fail_and_unstash;

	} else if (!run("stg import --mail --name " + quote(patchName) + " " + quote(patchFile)))
		goto fail_and_unstash;

	if (partialSelection) {

                /* Step 4: Unstash and merge working directory modified files */
		errorReportingEnabled = false;
		run("git stash pop"); // unfortunately 'git stash' is noisy on stderr
		errorReportingEnabled = true;

		/* Step 5: restore not selected files that were already in index */
		if (!notSel.empty() && !updateIndex(notSel))
			goto fail;
	}

	ret = true;
	goto exit;

fail_and_unstash:

	if (partialSelection) {
		run("git reset");
		errorReportingEnabled = false;
		run("git stash pop");
		errorReportingEnabled = true;
	}
fail:
exit:
	QDir dir(workDir);
	dir.remove(patchFile);
	return ret;
}

bool Git::stgPush(SCRef sha) {

	const QStringList patch(getRefNames(sha, UN_APPLIED));
	if (patch.count() != 1) {
		dbp("ASSERT in Git::stgPush, found %1 patches instead of 1", patch.count());
		return false;
	}
	return run("stg push " + quote(patch.first()));
}

bool Git::stgPop(SCRef sha) {

	const QStringList patch(getRefNames(sha, APPLIED));
	if (patch.count() != 1) {
		dbp("ASSERT in Git::stgPop, found %1 patches instead of 1", patch.count());
		return false;
	}
	return run("stg pop " + quote(patch));
}


//! cache for dates conversion. Common among qgit windows
static QHash<QString, QString> localDates;
/**
 * Accesses a cache that avoids slow date calculation
 *
 * @param gitDate
 *   the reference from which we want to get the date
 *
 * @return
 *   human-readable date
 **/
const QString Git::getLocalDate(SCRef gitDate) {
        QString localDate(localDates.value(gitDate));

        // cache miss
        if (localDate.isEmpty()) {
                static QDateTime d;
                d.setTime_t(gitDate.toUInt());
                localDate = QLocale::system().toString(d, QLocale::ShortFormat);

                // save to cache
                localDates[gitDate] = localDate;
        }

        return localDate;
}

const QStringList Git::getArgs(bool* quit, bool repoChanged) {

        QString args;
        QStringList arglist = qApp->arguments();

        // Remove first argument which is the path of the current executable
        arglist.removeFirst();

        if (startup) {
                bool ignoreNext = false;
                foreach (QString arg, arglist) {
                        // ignore --view-file=* and --view-file * QGit arguments
                        if (ignoreNext) {
                                ignoreNext = false;
                                continue;
                        } else if (arg.startsWith("--view-file="))
                                continue;
                        else if (arg == "--view-file") {
                                ignoreNext = true;
                                continue;
                        }

                        // in arguments with spaces double quotes
                        // are stripped by Qt, so re-add them
                        if (arg.contains(' '))
                                arg.prepend('\"').append('\"');

                        args.append(arg + ' ');
                }
        }
        if (!startup || args.isEmpty()) { // need to retrieve args
                if (testFlag(RANGE_SELECT_F)) { // open range dialog
                        RangeSelectImpl rs((QWidget*)parent(), &args, repoChanged, this);
                        *quit = (rs.exec() == QDialog::Rejected); // modal execution
                        if (*quit)
                            return QStringList();
                } else {
                    args = RangeSelectImpl::getDefaultArgs();
                }
        }
        startup = false;
        return MyProcess::splitArgList(args);
}

bool Git::getGitDBDir(SCRef wd, QString& gd, bool& changed) {
// we could run from a subdirectory, so we need to get correct directories

        QString runOutput, tmp(workDir);
        workDir = wd;
        errorReportingEnabled = false;
        bool success = run("git rev-parse --git-dir", &runOutput); // run under newWorkDir
        errorReportingEnabled = true;
        workDir = tmp;
        runOutput = runOutput.trimmed();
        if (success) {
                // 'git rev-parse --git-dir' output could be a relative
                // to working directory (as ex .git) or an absolute path
                QDir d(runOutput.startsWith("/") ? runOutput : wd + "/" + runOutput);
                changed = (d.absolutePath() != gitDir);
                gd = d.absolutePath();
        }
        return success;
}

bool Git::getBaseDir(SCRef wd, QString& bd, bool& changed) {
// we could run from a subdirectory, so we need to get correct directories

        // We use --show-cdup and not --git-dir for this, in order to take into account configurations
        //  in which .git is indeed a "symlink", a text file containing the path of the actual .git database dir.
        // In that particular case, the parent directory of the one given by --git-dir is *not* necessarily
        //  the base directory of the repository.

        QString runOutput, tmp(workDir);
        workDir = wd;
        errorReportingEnabled = false;
        bool success = run("git rev-parse --show-cdup", &runOutput); // run under newWorkDir
        errorReportingEnabled = true;
        workDir = tmp;
        runOutput = runOutput.trimmed();
        if (success) {
                // 'git rev-parse --show-cdup' is relative to working directory.
                QDir d(wd + "/" + runOutput);
                bd = d.absolutePath();
                changed = (bd != workDir);
        }
        else {
                changed = true;
                bd = wd;
        }
        return success;
}

Git::Reference* Git::lookupOrAddReference(const ShaString& sha) {
        RefMap::iterator it(refsShaMap.find(sha));
        if (it == refsShaMap.end()) it = refsShaMap.insert(sha, Reference());
        return &(*it);
}

Git::Reference* Git::lookupReference(const ShaString& sha) {
  RefMap::iterator it(refsShaMap.find(sha));
  if (it == refsShaMap.end()) return 0;
  return &(*it);
}

bool Git::getRefs() {

        // check for a StGIT stack
        QDir d(gitDir);
        QString stgCurBranch;
        if (d.exists("patches")) { // early skip
                errorReportingEnabled = false;
                isStGIT = run("stg branch", &stgCurBranch); // slow command
                errorReportingEnabled = true;
                stgCurBranch = stgCurBranch.trimmed();
        } else
                isStGIT = false;

        // check for a merge and read current branch sha
        isMergeHead = d.exists("MERGE_HEAD");
        QString curBranchSHA;
        if (!run("git rev-parse --revs-only HEAD", &curBranchSHA))
                return false;

        if (!run("git branch", &curBranchName))
                return false;

        curBranchSHA = curBranchSHA.trimmed();
        curBranchName = curBranchName.prepend('\n').section("\n*", 1);
        curBranchName = curBranchName.section('\n', 0, 0).trimmed();
        if (curBranchName.contains(" detached "))
            curBranchName = "";

        // read refs, normally unsorted
        QString runOutput;
        if (!run("git show-ref -d", &runOutput))
                return false;

        refsShaMap.clear();
        shaBackupBuf.clear(); // revs are already empty now

        QString prevRefSha;
        QStringList patchNames, patchShas;
        const QStringList rLst(runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts)));
        FOREACH_SL (it, rLst) {

                SCRef revSha = (*it).left(40);
                SCRef refName = (*it).mid(41);

                if (refName.startsWith("refs/patches/")) {

                        // save StGIT patch sha, to be used later
                        SCRef patchesDir("refs/patches/" + stgCurBranch + "/");
                        if (refName.startsWith(patchesDir)) {
                                patchNames.append(refName.mid(patchesDir.length()));
                                patchShas.append(revSha);
                        }
                        // StGIT patches should not be added to refs,
                        // but an applied StGIT patch could be also an head or
                        // a tag in this case will be added in another loop cycle
                        continue;
                }
                // one rev could have many tags
                Reference* cur = lookupOrAddReference(toPersistentSha(revSha, shaBackupBuf));

                if (refName.startsWith("refs/tags/")) {

                        if (refName.endsWith("^{}")) { // tag dereference

                                // we assume that a tag dereference follows strictly
                                // the corresponding tag object in rLst. So the
                                // last added tag is a tag object, not a commit object
                                cur->tags.append(refName.mid(10, refName.length() - 13));

                                // store tag object. Will be used to fetching
                                // tag message (if any) when necessary.
                                cur->tagObj = prevRefSha;

                                // tagObj must be removed from ref map
                                if (!prevRefSha.isEmpty())
                                        refsShaMap.remove(toTempSha(prevRefSha));

                        } else
                                cur->tags.append(refName.mid(10));

                        cur->type |= TAG;

                } else if (refName.startsWith("refs/heads/")) {

                        cur->branches.append(refName.mid(11));
                        cur->type |= BRANCH;
                        if (curBranchSHA == revSha)
                                cur->type |= CUR_BRANCH;
                } else if (refName.startsWith("refs/remotes/") && !refName.endsWith("HEAD")) {

                        cur->remoteBranches.append(refName.mid(13));
                        cur->type |= RMT_BRANCH;

                } else if (!refName.startsWith("refs/bases/") && !refName.endsWith("HEAD")) {

                        cur->refs.append(refName);
                        cur->type |= REF;
                }
                prevRefSha = revSha;
        }
        if (isStGIT && !patchNames.isEmpty())
                parseStGitPatches(patchNames, patchShas);

        // mark current head (even when detached)
        Reference* cur = lookupOrAddReference(toPersistentSha(curBranchSHA, shaBackupBuf));
        cur->type |= CUR_BRANCH;

        return !refsShaMap.empty();
}

void Git::parseStGitPatches(SCList patchNames, SCList patchShas) {

        patchesStillToFind = 0;

        // get patch names and status of current branch
        QString runOutput;
        if (!run("stg series", &runOutput))
                return;

        const QStringList pl(runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts)));
        FOREACH_SL (it, pl) {

                SCRef status = (*it).left(1);
                SCRef patchName = (*it).mid(2);

                bool applied = (status == "+" || status == ">");
                int pos = patchNames.indexOf(patchName);
                if (pos == -1) {
                        dbp("ASSERT in Git::parseStGitPatches(), patch %1 "
                            "not found in references list.", patchName);
                        continue;
                }
                const ShaString& ss = toPersistentSha(patchShas.at(pos), shaBackupBuf);
                Reference* cur = lookupOrAddReference(ss);
                cur->stgitPatch = patchName;
                cur->type |= (applied ? APPLIED : UN_APPLIED);

                if (applied)
                        patchesStillToFind++;
        }
}

const QStringList Git::getOthersFiles() {
// add files present in working directory but not in git archive

        QString runCmd("git ls-files --others");
        QSettings settings;
        QString exFile(settings.value(EX_KEY, EX_DEF).toString());
        if (!exFile.isEmpty()) {
                QString path = (exFile.startsWith("/")) ? exFile : workDir + "/" + exFile;
                if (QFile::exists(path))
                        runCmd.append(" --exclude-from=" + quote(exFile));
        }
        QString exPerDir(settings.value(EX_PER_DIR_KEY, EX_PER_DIR_DEF).toString());
        if (!exPerDir.isEmpty())
                runCmd.append(" --exclude-per-directory=" + quote(exPerDir));

        QString runOutput;
        run(runCmd, &runOutput);
        return runOutput.split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts));
}

Rev* Git::fakeRevData(SCRef sha, SCList parents, SCRef author, SCRef date, SCRef log, SCRef longLog,
                      SCRef patch, int idx, FileHistory* fh) {

        QString data('>' + sha + 'X' + parents.join(" ") + " \n");
        data.append(author + '\n' + author + '\n' + date + '\n');
        data.append(log + '\n' + longLog);

        QString header("log size " + QString::number(QByteArray(data.toLatin1()).length() - 1) + '\n');
        data.prepend(header);
        if (!patch.isEmpty())
                data.append('\n' + patch);

        QTextCodec* tc = QTextCodec::codecForLocale();
        QByteArray* ba = new QByteArray(tc->fromUnicode(data));
        ba->append('\0');

        fh->rowData.append(ba);
        int dummy;
        Rev* c = new Rev(*ba, 0, idx, &dummy, !isMainHistory(fh));
        return c;
}

const Rev* Git::fakeWorkDirRev(SCRef parent, SCRef log, SCRef longLog, int idx, FileHistory* fh) {

        QString patch;
        if (!isMainHistory(fh))
                patch = getWorkDirDiff(fh->fileNames().first());

        QString date(QString::number(QDateTime::currentDateTime().toSecsSinceEpoch()));
        QString author("-");
        QStringList parents(parent);
        Rev* c = fakeRevData(ZERO_SHA, parents, author, date, log, longLog, patch, idx, fh);
        c->isDiffCache = true;
        c->lanes.append(EMPTY);
        return c;
}

const RevFile* Git::fakeWorkDirRevFile(const WorkingDirInfo& wd) {

        FileNamesLoader fl;
        RevFile* rf = new RevFile();
        parseDiffFormat(*rf, wd.diffIndex, fl);
        rf->onlyModified = false;

        FOREACH_SL (it, wd.otherFiles) {

                appendFileName(*rf, *it, fl);
                rf->status.append(RevFile::UNKNOWN);
                rf->mergeParent.append(1);
        }
        RevFile cachedFiles;
        parseDiffFormat(cachedFiles, wd.diffIndexCached, fl);
        flushFileNames(fl);

        for (int i = 0; i < rf->count(); i++)
                if (findFileIndex(cachedFiles, filePath(*rf, i)) != -1)
                        rf->status[i] |= RevFile::IN_INDEX;
        return rf;
}

void Git::getDiffIndex() {

        QString status;
        if (!run("git status", &status)) // git status refreshes the index, run as first
                return;

        QString head;
        if (!run("git rev-parse --revs-only HEAD", &head))
                return;

        head = head.trimmed();
        if (!head.isEmpty()) { // repository initialized but still no history

                if (!run("git diff-index " + head, &workingDirInfo.diffIndex))
                        return;

                // check for files already updated in cache, we will
                // save this information in status third field
                if (!run("git diff-index --cached " + head, &workingDirInfo.diffIndexCached))
                        return;
        }
        // get any file not in tree
        workingDirInfo.otherFiles = getOthersFiles();

        // now mockup a RevFile
        revsFiles.insert(ZERO_SHA_RAW, fakeWorkDirRevFile(workingDirInfo));

        // then mockup the corresponding Rev
        SCRef log = (isNothingToCommit() ? "Nothing to commit" : "Working directory changes");
        const Rev* r = fakeWorkDirRev(head, log, status, revData->revOrder.count(), revData);
        revData->revs.insert(ZERO_SHA_RAW, r);
        revData->revOrder.append(ZERO_SHA_RAW);
        revData->earlyOutputCntBase = revData->revOrder.count();

        // finally send it to GUI
        emit newRevsAdded(revData, revData->revOrder);
}

void Git::parseDiffFormatLine(RevFile& rf, SCRef line, int parNum, FileNamesLoader& fl) {

        if (line[1] == ':') { // it's a combined merge

                /* For combined merges rename/copy information is useless
                 * because nor the original file name, nor similarity info
                 * is given, just the status tracks that in the left/right
                 * branch a renamed/copy occurred (as example status could
                 * be RM or MR). For visualization purposes we could consider
                 * the file as modified
                 */
                appendFileName(rf, line.section('\t', -1), fl);
                setStatus(rf, "M");
                rf.mergeParent.append(parNum);
        } else { // faster parsing in normal case

                if (line.at(98) == '\t') {
                        appendFileName(rf, line.mid(99), fl);
                        setStatus(rf, line.at(97));
                        rf.mergeParent.append(parNum);
                } else
                        // it's a rename or a copy, we are not in fast path now!
                        setExtStatus(rf, line.mid(97), parNum, fl);
        }
}

//CT TODO can go in RevFile
void Git::setStatus(RevFile& rf, SCRef rowSt) {

        char status = rowSt.at(0).toLatin1();
        switch (status) {
        case 'M':
        case 'T':
        case 'U':
                rf.status.append(RevFile::MODIFIED);
                break;
        case 'D':
                rf.status.append(RevFile::DELETED);
                rf.onlyModified = false;
                break;
        case 'A':
                rf.status.append(RevFile::NEW);
                rf.onlyModified = false;
                break;
        case '?':
                rf.status.append(RevFile::UNKNOWN);
                rf.onlyModified = false;
                break;
        default:
                dbp("ASSERT in Git::setStatus, unknown status <%1>. "
                    "'MODIFIED' will be used instead.", rowSt);
                rf.status.append(RevFile::MODIFIED);
                break;
        }
}

void Git::setExtStatus(RevFile& rf, SCRef rowSt, int parNum, FileNamesLoader& fl) {

        const QStringList sl(rowSt.split('\t', QGIT_SPLITBEHAVIOR(SkipEmptyParts)));
        if (sl.count() != 3) {
                dbp("ASSERT in setExtStatus, unexpected status string %1", rowSt);
                return;
        }
        // we want store extra info with format "orig --> dest (Rxx%)"
        // but git give us something like "Rxx\t<orig>\t<dest>"
        SCRef type = sl[0];
        SCRef orig = sl[1];
        SCRef dest = sl[2];
        const QString extStatusInfo(orig + " --> " + dest + " (" + type + "%)");

        /*
           NOTE: we set rf.extStatus size equal to position of latest
                 copied/renamed file. So it can have size lower then
                 rf.count() if after copied/renamed file there are
                 others. Here we have no possibility to know final
                 dimension of this RefFile. We are still in parsing.
        */

        // simulate new file
        appendFileName(rf, dest, fl);
        rf.mergeParent.append(parNum);
        rf.status.append(RevFile::NEW);
        rf.extStatus.resize(rf.status.size());
        rf.extStatus[rf.status.size() - 1] = extStatusInfo;

        // simulate deleted orig file only in case of rename
        if (type.at(0) == 'R') { // renamed file
                appendFileName(rf, orig, fl);
                rf.mergeParent.append(parNum);
                rf.status.append(RevFile::DELETED);
                rf.extStatus.resize(rf.status.size());
                rf.extStatus[rf.status.size() - 1] = extStatusInfo;
        }
        rf.onlyModified = false;
}

//CT TODO utility function; can go elsewhere
void Git::parseDiffFormat(RevFile& rf, SCRef buf, FileNamesLoader& fl) {

        int parNum = 1, startPos = 0, endPos = buf.indexOf('\n');
        while (endPos != -1) {

                SCRef line = buf.mid(startPos, endPos - startPos);
                if (line[0] == ':') // avoid sha's in merges output
                        parseDiffFormatLine(rf, line, parNum, fl);
                else
                        parNum++;

                startPos = endPos + 1;
                endPos = buf.indexOf('\n', endPos + 99);
        }
}

bool Git::startParseProc(SCList initCmd, FileHistory* fh, SCRef buf) {

        DataLoader* dl = new DataLoader(this, fh); // auto-deleted when done

        connect(this, SIGNAL(cancelLoading(const FileHistory*)),
                dl, SLOT(on_cancel(const FileHistory*)));

        connect(dl, SIGNAL(newDataReady(const FileHistory*)),
                this, SLOT(on_newDataReady(const FileHistory*)));

        connect(dl, SIGNAL(loaded(FileHistory*, ulong, int,
                bool, const QString&, const QString&)), this,
                SLOT(on_loaded(FileHistory*, ulong, int,
                bool, const QString&, const QString&)));

        return dl->start(initCmd, workDir, buf);
}

bool Git::startRevList(SCList args, FileHistory* fh) {

        QString baseCmd("git -c log.diffMerges=separate log --topo-order --no-color "

#ifndef Q_OS_WIN32
                        "--log-size " // FIXME broken on Windows
#endif
                        "--parents --boundary -z "
                        "--pretty=format:" GIT_LOG_FORMAT);

        // we don't need log message body for file history
        if (isMainHistory(fh))
                baseCmd.append("%b");

        QStringList initCmd(baseCmd.split(' '));
        if (!isMainHistory(fh)) {
        /*
           NOTE: we don't use '--remove-empty' option because
           in case a file is deleted and then a new file with
           the same name is created again in the same directory
           then, with this option, file history is truncated to
           the file deletion revision.
        */
                initCmd << QString("-r -m -p --full-index --simplify-merges").split(' ');
        } else
                {} // initCmd << QString("--early-output"); currently disabled

        return startParseProc(initCmd + args, fh, QString());
}

bool Git::startUnappliedList() {

        QStringList unAppliedShaList(getAllRefSha(UN_APPLIED));
        if (unAppliedShaList.isEmpty())
                return false;

        // WARNING: with this command 'git log' could send spurious
        // revs so we need some filter out logic during loading
        QString cmd("git log --no-color --parents -z "

#ifndef Q_OS_WIN32
                    "--log-size " // FIXME broken on Windows
#endif
                    "--pretty=format:" GIT_LOG_FORMAT "%b ^HEAD");

        QStringList sl(cmd.split(' '));
        sl << unAppliedShaList;
        return startParseProc(sl, revData, QString());
}

void Git::stop(bool saveCache) {
// normally called when changing directory or closing

        EM_RAISE(exGitStopped);

        // stop all data sending from process and asks them
        // to terminate. Note that process could still keep
        // running for a while although silently
        emit cancelAllProcesses(); // non blocking

        // after cancelAllProcesses() procFinished() is not called anymore
        // TODO perhaps is better to call procFinished() also if process terminated
        // incorrectly as QProcess does. BUt first we need to fix FileView::on_loadCompleted()
        emit fileNamesLoad(1, revsFiles.count() - filesLoadingStartOfs);

        if (cacheNeedsUpdate && saveCache) {

                cacheNeedsUpdate = false;
                if (!filesLoadingCurSha.isEmpty()) // we are in the middle of a loading
                        revsFiles.remove(toTempSha(filesLoadingCurSha)); // remove partial data

                if (!revsFiles.isEmpty()) {
                        SHOW_MSG("Saving cache. Please wait...");
                        if (!Cache::save(gitDir, revsFiles, dirNamesVec, fileNamesVec))
                                dbs("ERROR unable to save file names cache");
                }
        }
}

void Git::clearRevs() {

        revData->clear();
        patchesStillToFind = 0; // TODO TEST WITH FILTERING
        firstNonStGitPatch = "";
        workingDirInfo.clear();
        revsFiles.remove(ZERO_SHA_RAW);
}

void Git::clearFileNames() {

        qDeleteAll(revsFiles);
        revsFiles.clear();
        fileNamesMap.clear();
        dirNamesMap.clear();
        dirNamesVec.clear();
        fileNamesVec.clear();
        revsFilesShaBackupBuf.clear();
        cacheNeedsUpdate = false;
}

bool Git::init(SCRef wd, bool askForRange, const QStringList* passedArgs, bool overwriteArgs, bool* quit) {
// normally called when changing git directory. Must be called after stop()

        *quit = false;
        clearRevs();

        /* we only update filtering info here, original arguments
         * are not overwritten. Only getArgs() can update arguments,
         * an exception is if flag overwriteArgs is set
         */
        loadArguments.filteredLoading = (!overwriteArgs && passedArgs != NULL);
        if (loadArguments.filteredLoading)
                loadArguments.filterList = *passedArgs;

        if (overwriteArgs) // in this case must be passedArgs != NULL
                loadArguments.args = *passedArgs;

        try {
                setThrowOnStop(true);

                const QString msg1("Path is '" + workDir + "'    Loading ");

                // check if repository is valid
                bool repoChanged;
                isGIT = getGitDBDir(wd, gitDir, repoChanged);

                if (repoChanged) {
                        bool dummy;
                        getBaseDir(wd, workDir, dummy);
                        localDates.clear();
                        fileCacheAccessed = false;

                        SHOW_MSG(msg1 + "file names cache...");
                        loadFileCache();
                        SHOW_MSG("");
                }
                if (!isGIT) {
                        setThrowOnStop(false);
                        return false;
                }
                if (!passedArgs) {

                        // update text codec according to repo settings
                        bool dummy;
                        QTextCodec::setCodecForLocale(getTextCodec(&dummy));

                        // load references
                        SHOW_MSG(msg1 + "refs...");
                        if (!getRefs())
                                dbs("WARNING: no tags or heads found");

                        // startup input range dialog
                        SHOW_MSG("");
                        if (startup || askForRange) {
                                loadArguments.args = getArgs(quit, repoChanged); // must be called with refs loaded
                                if (*quit) {
                                        setThrowOnStop(false);
                                        return false;
                                }
                        }
                        // load StGit unapplied patches, must be after getRefs()
                        if (isStGIT) {
                                loadingUnAppliedPatches = startUnappliedList();
                                if (loadingUnAppliedPatches) {

                                        SHOW_MSG(msg1 + "StGIT unapplied patches...");
                                        setThrowOnStop(false);

                                        // we will continue with init2() at
                                        // the end of loading...
                                        return true;
                                }
                        }
                }
                init2();
                shortHashLen = getShortHashLength();
                setThrowOnStop(false);
                return true;

        } catch (int i) {

                setThrowOnStop(false);

                if (isThrowOnStopRaised(i, "initializing 1")) {
                        EM_THROW_PENDING;
                        return false;
                }
                const QString info("Exception \'" + EM_DESC(i) + "\' "
                                   "not handled in init...re-throw");
                dbs(info);
                throw;
        }
}

void Git::init2() {

        const QString msg1("Path is '" + workDir + "'    Loading ");

        // after loading unapplied patch update base early output offset to
        // avoid losing unapplied patches at first early output event
        if (isStGIT)
                revData->earlyOutputCntBase = revData->revOrder.count();

        try {
                setThrowOnStop(true);

                // load working directory files
                if (!loadArguments.filteredLoading && testFlag(DIFF_INDEX_F)) {
                        SHOW_MSG(msg1 + "working directory changed files...");
                        getDiffIndex(); // blocking, we could be in setRepository() now
                }
                SHOW_MSG(msg1 + "revisions...");

                // build up command line arguments
                QStringList args(loadArguments.args);
                if (loadArguments.filteredLoading) {
                        if (!args.contains("--"))
                                args << "--";

                        args << loadArguments.filterList;
                }
                if (!startRevList(args, revData))
                        SHOW_MSG("ERROR: unable to start 'git log'");

                setThrowOnStop(false);

        } catch (int i) {

                setThrowOnStop(false);

                if (isThrowOnStopRaised(i, "initializing 2")) {
                        EM_THROW_PENDING;
                        return;
                }
                const QString info("Exception \'" + EM_DESC(i) + "\' "
                                   "not handled in init2...re-throw");
                dbs(info);
                throw;
        }
}

void Git::on_newDataReady(const FileHistory* fh) {

        emit newRevsAdded(fh , fh->revOrder);
}

void Git::on_loaded(FileHistory* fh, ulong byteSize, int loadTime,
                    bool normalExit, SCRef cmd, SCRef errorDesc) {

        if (!errorDesc.isEmpty()) {
                MainExecErrorEvent* e = new MainExecErrorEvent(cmd, errorDesc);
                QApplication::postEvent(parent(), e);
        }
        if (normalExit) { // do not send anything if killed

                on_newDataReady(fh);

                if (!loadingUnAppliedPatches) {

                        fh->loadTime += loadTime;

                        ulong kb = byteSize / 1024;
                        double mbs = (double)byteSize / fh->loadTime / 1000;
                        QString tmp;
                        tmp.asprintf("Loaded %i revisions  (%li KB),   "
                                     "time elapsed: %i ms  (%.2f MB/s)",
                                     fh->revs.count(), kb, fh->loadTime, mbs);

                        if (!tryFollowRenames(fh))
                                emit loadCompleted(fh, tmp);

                        if (isMainHistory(fh))
                                // wait the dust to settle down before to start
                                // background file names loading for new revisions
                                QTimer::singleShot(500, this, SLOT(loadFileNames()));
                }
        }
        if (loadingUnAppliedPatches) {
                loadingUnAppliedPatches = false;
                revData->lns->clear(); // again to reset lanes
                init2(); // continue with loading of remaining revisions
        }
}

bool Git::tryFollowRenames(FileHistory* fh) {

        if (isMainHistory(fh))
                return false;

        QStringList oldNames;
        QMutableStringListIterator it(fh->renamedRevs);
        while (it.hasNext())
                if (!populateRenamedPatches(it.next(), fh->curFNames, fh, &oldNames, false))
                        it.remove();

        if (fh->renamedRevs.isEmpty())
                return false;

        QStringList args;
        args << fh->renamedRevs << "--" << oldNames;
        fh->fNames << oldNames;
        fh->curFNames = oldNames;
        fh->renamedRevs.clear();
        return startRevList(args, fh);
}

bool Git::populateRenamedPatches(SCRef renamedSha, SCList newNames, FileHistory* fh,
                                 QStringList* oldNames, bool backTrack) {

        QString runOutput;
        if (!run("git diff-tree -r -M " + renamedSha, &runOutput))
                return false;

        // find the first renamed file with the new file name in renamedFiles list
        QString line;
        FOREACH_SL (it, newNames) {
                if (backTrack) {
                        line = runOutput.section('\t' + *it + '\t', 0, 0,
                                                 QString::SectionIncludeTrailingSep);
                        line.chop(1);
                } else
                        line = runOutput.section('\t' + *it + '\n', 0, 0);

                if (!line.isEmpty())
                        break;
        }
        if (line.contains('\n'))
                line = line.section('\n', -1, -1);

        SCRef status = line.section('\t', -2, -2).section(' ', -1, -1);
        if (!status.startsWith('R'))
                return false;

        if (backTrack) {
                SCRef nextFile = runOutput.section(line, 1, 1).section('\t', 1, 1);
                oldNames->append(nextFile.section('\n', 0, 0));
                return true;
        }
        // get the diff betwen two files
        SCRef prevFileSha = line.section(' ', 2, 2);
        SCRef lastFileSha = line.section(' ', 3, 3);
        if (prevFileSha == lastFileSha) // just renamed
                runOutput.clear();
        else if (!run("git diff --no-ext-diff -r --full-index " + prevFileSha + " " + lastFileSha, &runOutput))
                return false;

        SCRef prevFile = line.section('\t', -1, -1);
        if (!oldNames->contains(prevFile))
                oldNames->append(prevFile);

        // save the patch, will be used later to create a
        // proper graft sha with correct parent info
        if (fh) {
                QString tmp(!runOutput.isEmpty() ? runOutput : "diff --no-ext-diff --\nsimilarity index 100%\n");
                fh->renamedPatches.insert(renamedSha, tmp);
        }
        return true;
}

void Git::populateFileNamesMap() {

        for (int i = 0; i < dirNamesVec.count(); ++i)
                dirNamesMap.insert(dirNamesVec[i], i);

        for (int i = 0; i < fileNamesVec.count(); ++i)
                fileNamesMap.insert(fileNamesVec[i], i);
}

void Git::loadFileCache() {

        if (!fileCacheAccessed) {

                fileCacheAccessed = true;
                clearFileNames();
                QByteArray shaBuf;
                if (Cache::load(gitDir, revsFiles, dirNamesVec, fileNamesVec, shaBuf)) {
                        revsFilesShaBackupBuf.append(shaBuf);
                        populateFileNamesMap();
                } else {
                        // The cache isn't valid. Clear it before we corrupt it
                        // by freeing `shaBuf`.
                        clearFileNames();
                        dbs("ERROR: unable to load file names cache");
                }
        }
}

void Git::loadFileNames() {

        indexTree(); // we are sure data loading is finished at this point

        int revCnt = 0;
        QString diffTreeBuf;
        FOREACH (ShaVect, it, revData->revOrder) {

                if (!revsFiles.contains(*it)) {
                        const Rev* c = revLookup(*it);
                        if (c->parentsCount() == 1) { // skip initials and merges
                                diffTreeBuf.append(*it).append('\n');
                                revCnt++;
                        }
                }
        }
        if (!diffTreeBuf.isEmpty()) {
                filesLoadingPending = filesLoadingCurSha = "";
                filesLoadingStartOfs = revsFiles.count();
                emit fileNamesLoad(3, revCnt);

                const QString runCmd("git diff-tree --no-color -r -C --stdin");
                runAsync(runCmd, this, diffTreeBuf);
        }
}

bool Git::filterEarlyOutputRev(FileHistory* fh, Rev* rev) {

        if (fh->earlyOutputCnt < fh->revOrder.count()) {

                const ShaString& sha = fh->revOrder[fh->earlyOutputCnt++];
                const Rev* c = revLookup(sha, fh);
                if (c) {
                        if (rev->sha() != sha || rev->parents() != c->parents()) {
                                // mismatch found! set correct value, 'rev' will
                                // overwrite 'c' upon returning
                                rev->orderIdx = c->orderIdx;
                                revData->clear(false); // flush the tail
                        } else
                                return true; // filter out 'rev'
                }
        }
        // we have new revisions, exit from early output state
        fh->setEarlyOutputState(false);
        return false;
}

int Git::addChunk(FileHistory* fh, const QByteArray& ba, int start) {

        RevMap& r = fh->revs;
        int nextStart;
        Rev* rev;

        do {
                // only here we create a new rev
                rev = new Rev(ba, start, fh->revOrder.count(), &nextStart, !isMainHistory(fh));

                if (nextStart == -2) {
                        delete rev;
                        fh->setEarlyOutputState(true);
                        start = ba.indexOf('\n', start) + 1;
                }

        } while (nextStart == -2);

        if (nextStart == -1) { // half chunk detected
                delete rev;
                return -1;
        }

        const ShaString& sha = rev->sha();

        if (fh->earlyOutputCnt != -1 && filterEarlyOutputRev(fh, rev)) {
                delete rev;
                return nextStart;
        }

        if (isStGIT) {
                if (loadingUnAppliedPatches) { // filter out possible spurious revs

                        Reference* rf = lookupReference(sha);
                        if (!(rf && (rf->type & UN_APPLIED))) {
                                delete rev;
                                return nextStart;
                        }
                }
                // remove StGIT spurious revs filter
                if (!firstNonStGitPatch.isEmpty() && firstNonStGitPatch == sha)
                        firstNonStGitPatch = "";

                // StGIT called with --all option creates spurious revs so filter
                // out unknown revs until no more StGIT patches are waited and
                // firstNonStGitPatch is reached
                if (!(firstNonStGitPatch.isEmpty() && patchesStillToFind == 0) &&
                    !loadingUnAppliedPatches && isMainHistory(fh)) {

                        Reference* rf = lookupReference(sha);
                        if (!(rf && (rf->type & APPLIED))) {
                                delete rev;
                                return nextStart;
                        }
                }
                if (r.contains(sha)) {
                        // StGIT unapplied patches could be sent again by
                        // 'git log' as example if called with --all option.
                        if (r[sha]->isUnApplied) {
                                delete rev;
                                return nextStart;
                        }
                        // could be a side effect of 'git log -m', see below
                        if (isMainHistory(fh) || rev->parentsCount() < 2)
                                dbp("ASSERT: addChunk sha <%1> already received", sha);
                }
        }
        if (r.isEmpty() && !isMainHistory(fh)) {
                bool added = copyDiffIndex(fh, sha);
                rev->orderIdx = added ? 1 : 0;
        }
        if (   !isMainHistory(fh)
            && !fh->renamedPatches.isEmpty()
            &&  fh->renamedPatches.contains(sha)) {

                // this is the new rev with renamed file, the rev is correct but
                // the patch, create a new rev with proper patch and use that instead
                const Rev* prevSha = revLookup(sha, fh);
                Rev* c = fakeRevData(sha, rev->parents(), rev->author(),
                                     rev->authorDate(), rev->shortLog(), rev->longLog(),
                                     fh->renamedPatches[sha], prevSha->orderIdx, fh);

                r.insert(sha, c); // overwrite old content
                fh->renamedPatches.remove(sha);
                return nextStart;
        }
        if (!isMainHistory(fh) && rev->parentsCount() > 1 && r.contains(sha)) {
        /* In this case git log is called with -m option and merges are splitted
           in one commit per parent but all them have the same sha.
           So we add only the first to fh->revOrder to display history correctly,
           but we nevertheless add all the commits to 'r' so that annotation code
           can get the patches.
        */
                QString mergeSha;
                int i = 0;
                do
                        mergeSha = QString::number(++i) + " m " + sha;
                while (r.contains(toTempSha(mergeSha)));

                const ShaString& ss = toPersistentSha(mergeSha, shaBackupBuf);
                r.insert(ss, rev);
        } else {
                r.insert(sha, rev);
                fh->revOrder.append(sha);

                if (rev->parentsCount() == 0 && !isMainHistory(fh))
                        fh->renamedRevs.append(sha);
        }
        if (isStGIT) {
                // updateLanes() is called too late, after loadingUnAppliedPatches
                // has been reset so update the lanes now.
                if (loadingUnAppliedPatches) {

                        Rev* c = const_cast<Rev*>(revLookup(sha, fh));
                        c->isUnApplied = true;
                        c->lanes.append(UNAPPLIED);

                } else if (patchesStillToFind > 0 || !isMainHistory(fh)) { // try to avoid costly lookup

                        Reference* rf = lookupReference(sha);
                        if (rf && (rf->type & APPLIED)) {

                                Rev* c = const_cast<Rev*>(revLookup(sha, fh));
                                c->isApplied = true;
                                if (isMainHistory(fh)) {
                                        patchesStillToFind--;
                                        if (patchesStillToFind == 0)
                                                // any rev will be discarded until
                                                // firstNonStGitPatch arrives
                                                firstNonStGitPatch = c->parent(0);
                                }
                        }
                }
        }
        return nextStart;
}

bool Git::copyDiffIndex(FileHistory* fh, SCRef parent) {
// must be called with empty revs and empty revOrder

        if (!fh->revOrder.isEmpty() || !fh->revs.isEmpty()) {
                dbs("ASSERT in copyDiffIndex: called with wrong context");
                return false;
        }
        const Rev* r = revLookup(ZERO_SHA);
        if (!r)
                return false;

        const RevFile* files = getFiles(ZERO_SHA);
        if (!files || findFileIndex(*files, fh->fileNames().first()) == -1)
                return false;

        // insert a custom ZERO_SHA rev with proper parent
        const Rev* rf = fakeWorkDirRev(parent, "Working directory changes", "long log\n", 0, fh);
        fh->revs.insert(ZERO_SHA_RAW, rf);
        fh->revOrder.append(ZERO_SHA_RAW);
        return true;
}

void Git::setLane(SCRef sha, FileHistory* fh) {

        Lanes* l = fh->lns;
        uint i = fh->firstFreeLane;
        QVector<QByteArray> ba;
        const ShaString& ss = toPersistentSha(sha, ba);
        const ShaVect& shaVec(fh->revOrder);

        for (uint cnt = shaVec.count(); i < cnt; ++i) {

                const ShaString& curSha = shaVec[i];
                Rev* r = const_cast<Rev*>(revLookup(curSha, fh));
                if (r->lanes.count() == 0)
                        updateLanes(*r, *l, curSha);

                if (curSha == ss)
                        break;
        }
        fh->firstFreeLane = ++i;
}

void Git::updateLanes(Rev& c, Lanes& lns, SCRef sha) {
// we could get third argument from c.sha(), but we are in fast path here
// and c.sha() involves a deep copy, so we accept a little redundancy

        if (lns.isEmpty())
                lns.init(sha);

        bool isDiscontinuity;
        bool isFork = lns.isFork(sha, isDiscontinuity);
        bool isMerge = (c.parentsCount() > 1);
        bool isInitial = (c.parentsCount() == 0);

        if (isDiscontinuity)
                lns.changeActiveLane(sha); // uses previous isBoundary state

        lns.setBoundary(c.isBoundary()); // update must be here

        if (isFork)
                lns.setFork(sha);
        if (isMerge)
                lns.setMerge(c.parents());
        if (c.isApplied)
                lns.setApplied();
        if (isInitial)
                lns.setInitial();

        lns.getLanes(c.lanes); // here lanes are snapshotted

        SCRef nextSha = (isInitial) ? "" : QString(c.parent(0));

        lns.nextParent(nextSha);

        if (c.isApplied)
                lns.afterApplied();
        if (isMerge)
                lns.afterMerge();
        if (isFork)
                lns.afterFork();
        if (lns.isBranch())
                lns.afterBranch();

//	QString tmp = "", tmp2;
//	for (uint i = 0; i < c.lanes.count(); i++) {
//		tmp2.setNum(c.lanes[i]);
//		tmp.append(tmp2 + "-");
//	}
//	qDebug("%s %s", tmp.toUtf8().data(), sha.toUtf8().data());
}

void Git::procFinished() {

        flushFileNames(fileLoader);
        filesLoadingPending = filesLoadingCurSha = "";
        emit fileNamesLoad(1, revsFiles.count() - filesLoadingStartOfs);
}

void Git::procReadyRead(const QByteArray& fileChunk) {

        QTextCodec* tc = QTextCodec::codecForLocale();

        if (filesLoadingPending.isEmpty())
                filesLoadingPending = tc->toUnicode(fileChunk);
        else
                filesLoadingPending.append(tc->toUnicode(fileChunk)); // add to previous half lines

        RevFile* rf = NULL;
        if (!filesLoadingCurSha.isEmpty() && revsFiles.contains(toTempSha(filesLoadingCurSha)))
                rf = const_cast<RevFile*>(revsFiles[toTempSha(filesLoadingCurSha)]);

        int nextEOL = filesLoadingPending.indexOf('\n');
        int lastEOL = -1;
        while (nextEOL != -1) {

                SCRef line(filesLoadingPending.mid(lastEOL + 1, nextEOL - lastEOL - 1));
                if (line.at(0) != ':') {
                        SCRef sha = line.left(40);
                        if (!rf || sha != filesLoadingCurSha) { // new commit
                                rf = new RevFile();
                                revsFiles.insert(toPersistentSha(sha, revsFilesShaBackupBuf), rf);
                                filesLoadingCurSha = sha;
                                cacheNeedsUpdate = true;
                        } else
                                dbp("ASSERT: repeated sha %1 in file names loading", sha);
                } else // line.constref(0) == ':'
                        parseDiffFormatLine(*rf, line, 1, fileLoader);

                lastEOL = nextEOL;
                nextEOL = filesLoadingPending.indexOf('\n', lastEOL + 1);
        }
        if (lastEOL != -1)
                filesLoadingPending.remove(0, lastEOL + 1);

        emit fileNamesLoad(2, revsFiles.count() - filesLoadingStartOfs);
}

void Git::flushFileNames(FileNamesLoader& fl) {

        if (!fl.rf)
                return;

        QByteArray& b = fl.rf->pathsIdx;
        QVector<int>& dirs = fl.rfDirs;

        b.clear();
        b.resize(2 * dirs.size() * static_cast<int>(sizeof(int)));

        int* d = (int*)(b.data());

        for (int i = 0; i < dirs.size(); i++) {

                d[i] = dirs.at(i);
                d[dirs.size() + i] = fl.rfNames.at(i);
        }
        dirs.clear();
        fl.rfNames.clear();
        fl.rf = NULL;
}

void Git::appendFileName(RevFile& rf, SCRef name, FileNamesLoader& fl) {

        if (fl.rf != &rf) {
                flushFileNames(fl);
                fl.rf = &rf;
        }
        int idx = name.lastIndexOf('/') + 1;
        SCRef dr = name.left(idx);
        SCRef nm = name.mid(idx);

        QHash<QString, int>::const_iterator it(dirNamesMap.constFind(dr));
        if (it == dirNamesMap.constEnd()) {
                int idx = dirNamesVec.count();
                dirNamesMap.insert(dr, idx);
                dirNamesVec.append(dr);
                fl.rfDirs.append(idx);
        } else
                fl.rfDirs.append(*it);

        it = fileNamesMap.constFind(nm);
        if (it == fileNamesMap.constEnd()) {
                int idx = fileNamesVec.count();
                fileNamesMap.insert(nm, idx);
                fileNamesVec.append(nm);
                fl.rfNames.append(idx);
        } else
                fl.rfNames.append(*it);
}

void Git::updateDescMap(const Rev* r,uint idx, QHash<QPair<uint, uint>, bool>& dm,
                        QHash<uint, QVector<int> >& dv) {

        QVector<int> descVec;
        if (r->descRefsMaster != -1) {

                const Rev* tmp = revLookup(revData->revOrder[r->descRefsMaster]);
                const QVector<int>& nr = tmp->descRefs;

                for (int i = 0; i < nr.count(); i++) {

                        if (!dv.contains(nr[i])) {
                                dbp("ASSERT descendant for %1 not found", r->sha());
                                return;
                        }
                        const QVector<int>& dvv = dv[nr[i]];

                        // copy the whole vector instead of each element
                        // in the first iteration of the loop below
                        descVec = dvv; // quick (shared) copy

                        for (int y = 0; y < dvv.count(); y++) {

                                uint v = (uint)dvv[y];
                                QPair<uint, uint> key = qMakePair(idx, v);
                                QPair<uint, uint> keyN = qMakePair(v, idx);
                                dm.insert(key, true);
                                dm.insert(keyN, false);

                                // we don't want duplicated entry, otherwise 'dvv' grows
                                // greatly in repos with many tagged development branches
                                if (i > 0 && !descVec.contains(v)) // i > 0 is rare, no
                                        descVec.append(v);         // need to optimize
                        }
                }
        }
        descVec.append(idx);
        dv.insert(idx, descVec);
}

void Git::mergeBranches(Rev* p, const Rev* r) {

        int r_descBrnMaster = (checkRef(r->sha(), BRANCH | RMT_BRANCH) ? r->orderIdx : r->descBrnMaster);

        if (p->descBrnMaster == r_descBrnMaster || r_descBrnMaster == -1)
                return;

        // we want all the descendant branches, so just avoid duplicates
        const QVector<int>& src1 = revLookup(revData->revOrder[p->descBrnMaster])->descBranches;
        const QVector<int>& src2 = revLookup(revData->revOrder[r_descBrnMaster])->descBranches;
        QVector<int> dst(src1);
        for (int i = 0; i < src2.count(); i++)
                if (std::find(src1.constBegin(), src1.constEnd(), src2[i]) == src1.constEnd())
                        dst.append(src2[i]);

        p->descBranches = dst;
        p->descBrnMaster = p->orderIdx;
}

void Git::mergeNearTags(bool down, Rev* p, const Rev* r, const QHash<QPair<uint, uint>, bool>& dm) {

        bool isTag = checkRef(r->sha(), TAG);
        int r_descRefsMaster = isTag ? r->orderIdx : r->descRefsMaster;
        int r_ancRefsMaster = isTag ? r->orderIdx : r->ancRefsMaster;

        if (down && (p->descRefsMaster == r_descRefsMaster || r_descRefsMaster == -1))
                return;

        if (!down && (p->ancRefsMaster == r_ancRefsMaster || r_ancRefsMaster == -1))
                return;

        // we want the nearest tag only, so remove any tag
        // that is ancestor of any other tag in p U r
        const ShaVect& ro = revData->revOrder;
        const ShaString& sha1 = down ? ro[p->descRefsMaster] : ro[p->ancRefsMaster];
        const ShaString& sha2 = down ? ro[r_descRefsMaster] : ro[r_ancRefsMaster];
        const QVector<int>& src1 = down ? revLookup(sha1)->descRefs : revLookup(sha1)->ancRefs;
        const QVector<int>& src2 = down ? revLookup(sha2)->descRefs : revLookup(sha2)->ancRefs;
        QVector<int> dst(src1);

        for (int s2 = 0; s2 < src2.count(); s2++) {

                bool add = false;
                for (int s1 = 0; s1 < src1.count(); s1++) {

                        if (src2[s2] == src1[s1]) {
                                add = false;
                                break;
                        }
                        QPair<uint, uint> key = qMakePair((uint)src2[s2], (uint)src1[s1]);

                        if (!dm.contains(key)) { // could be empty if all tags are independent
                                add = true; // could be an independent path
                                continue;
                        }
                        add = (down && dm[key]) || (!down && !dm[key]);
                        if (add)
                                dst[s1] = -1; // mark for removing
                        else
                                break;
                }
                if (add)
                        dst.append(src2[s2]);
        }
        QVector<int>& nearRefs = (down ? p->descRefs : p->ancRefs);
        int& nearRefsMaster = (down ? p->descRefsMaster : p->ancRefsMaster);

        nearRefs.clear();
        for (int s2 = 0; s2 < dst.count(); s2++)
                if (dst[s2] != -1)
                        nearRefs.append(dst[s2]);

        nearRefsMaster = p->orderIdx;
}

void Git::indexTree() {

        const ShaVect& ro = revData->revOrder;
        if (ro.count() == 0)
                return;

        // we keep the pairs(x, y). Value is true if x is
        // ancestor of y or false if y is ancestor of x
        QHash<QPair<uint, uint>, bool> descMap;
        QHash<uint, QVector<int> > descVect;

        // walk down the tree from latest to oldest,
        // compute children and nearest descendants
        for (uint i = 0, cnt = ro.count(); i < cnt; i++) {

                uint type = checkRef(ro[i]);
                bool isB = (type & (BRANCH | RMT_BRANCH));
                bool isT = (type & TAG);

                const Rev* r = revLookup(ro[i]);

                if (isB) {
                        Rev* rr = const_cast<Rev*>(r);
                        if (r->descBrnMaster != -1) {
                                const ShaString& sha = ro[r->descBrnMaster];
                                rr->descBranches = revLookup(sha)->descBranches;
                        }
                        rr->descBranches.append(i);
                }
                if (isT) {
                        updateDescMap(r, i, descMap, descVect);
                        Rev* rr = const_cast<Rev*>(r);
                        rr->descRefs.clear();
                        rr->descRefs.append(i);
                }
                for (uint y = 0; y < r->parentsCount(); y++) {

                        Rev* p = const_cast<Rev*>(revLookup(r->parent(y)));
                        if (p) {
                                p->children.append(i);

                                if (p->descBrnMaster == -1)
                                        p->descBrnMaster = isB ? r->orderIdx : r->descBrnMaster;
                                else
                                        mergeBranches(p, r);

                                if (p->descRefsMaster == -1)
                                        p->descRefsMaster = isT ? r->orderIdx : r->descRefsMaster;
                                else
                                        mergeNearTags(optGoDown, p, r, descMap);
                        }
                }
        }
        // walk backward through the tree and compute nearest tagged ancestors
        for (int i = ro.count() - 1; i >= 0; i--) {

                const Rev* r = revLookup(ro[i]);
                bool isTag = checkRef(ro[i], TAG);

                if (isTag) {
                        Rev* rr = const_cast<Rev*>(r);
                        rr->ancRefs.clear();
                        rr->ancRefs.append(i);
                }
                for (int y = 0; y < r->children.count(); y++) {

                        Rev* c = const_cast<Rev*>(revLookup(ro[r->children[y]]));
                        if (c) {
                                if (c->ancRefsMaster == -1)
                                        c->ancRefsMaster = isTag ? r->orderIdx:r->ancRefsMaster;
                                else
                                        mergeNearTags(!optGoDown, c, r, descMap);
                        }
                }
        }
}

