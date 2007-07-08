/*
	Description: start-up repository opening and reading

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QSettings>
#include <QTextCodec>
#include "exceptionmanager.h"
#include "rangeselectimpl.h"
#include "lanes.h"
#include "myprocess.h"
#include "cache.h"
#include "annotate.h"
#include "mainimpl.h"
#include "dataloader.h"
#include "git.h"

#define POST_MSG(x) QApplication::postEvent(parent(), new MessageEvent(x))

using namespace QGit;

static QHash<QString, QString> localDates;

const QString Git::getLocalDate(SCRef gitDate) {
// fast path here, we use a cache to avoid the slow date calculation

	QString localDate(localDates.value(gitDate));
	if (!localDate.isEmpty())
		return localDate;

	QDateTime d;
	d.setTime_t(gitDate.toULong());
	localDate = d.toString(Qt::LocalDate);
	localDates[gitDate] = localDate;
	return localDate;
}

const QStringList Git::getArgs(bool askForRange, bool* quit) {

	static bool startup = true; // it's OK to be unique among qgit windows
	if (startup) {
		curRange = "";
		for (int i = 1; i < qApp->argc(); i++) {
			// in arguments with spaces double quotes
			// are stripped by Qt, so re-add them
			QString arg(qApp->argv()[i]);
			if (arg.contains(' '))
				arg.prepend('\"').append('\"');

			curRange.append(arg + ' ');
		}
	}
	if (    askForRange
	    &&  testFlag(RANGE_SELECT_F)
	    && (!startup || curRange.isEmpty())) {

		SCList names = getAllRefNames(TAG, !optOnlyLoaded);
		RangeSelectImpl rs((QWidget*)parent(), &curRange, names, this);
		*quit = (rs.exec() == QDialog::Rejected); // modal execution
		if (*quit)
			return QStringList();
	}
	startup = false;
	return MyProcess::splitArgList(curRange);
}

const QString Git::getBaseDir(bool* changed, SCRef wd, bool* ok, QString* gd) {
// we could run from a subdirectory, so we need to get correct directories

	QString runOutput, tmp(workDir);
	workDir = wd;
	errorReportingEnabled = false;
	bool ret = run("git rev-parse --git-dir", &runOutput); // run under newWorkDir
	errorReportingEnabled = true;
	workDir = tmp;
	runOutput = runOutput.trimmed();
	if (!ret || runOutput.isEmpty()) {
		*changed = true;
		if (ok)
			*ok = false;
		return wd;
	}
	// 'git rev-parse --git-dir' output could be a relative
	// to working dir (as ex .git) or an absolute path
	QDir d(runOutput.startsWith("/") ? runOutput : wd + "/" + runOutput);
	*changed = (d.absolutePath() != gitDir);
	if (gd)
		*gd = d.absolutePath();
	if (ok)
		*ok = true;
	d.cdUp();
	return d.absolutePath();
}

Git::Reference* Git::lookupReference(SCRef sha, bool create) {

	RefMap::iterator it(refsShaMap.find(sha));
	if (it == refsShaMap.end() && create)
		it = refsShaMap.insert(sha, Reference());

	return (it != refsShaMap.end() ? &(*it) : NULL);
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
	QString curBranchSHA, curBranchName;
	if (!run("git rev-parse HEAD", &curBranchSHA))
		return false;

	if (!run("git branch", &curBranchName))
		return false;

	curBranchSHA = curBranchSHA.trimmed();
	curBranchName = curBranchName.prepend('\n').section("\n*", 1);
	curBranchName = curBranchName.section('\n', 0, 0).trimmed();

	// read refs, normally unsorted
	QString runOutput;
	if (!run("git show-ref -d", &runOutput))
		return false;

	refsShaMap.clear();
	QString prevRefSha;
	QStringList patchNames, patchShas;
	const QStringList rLst(runOutput.split('\n', QString::SkipEmptyParts));
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
		Reference* cur = lookupReference(revSha, optCreate);

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
				refsShaMap.remove(prevRefSha);

			} else
				cur->tags.append(refName.mid(10));

			cur->type |= TAG;

		} else if (refName.startsWith("refs/heads/")) {

			cur->branches.append(refName.mid(11));
			cur->type |= BRANCH;
			if (curBranchSHA == revSha) {
				cur->type |= CUR_BRANCH;
				cur->currentBranch = curBranchName;
			}
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

	return !refsShaMap.empty();
}

void Git::parseStGitPatches(SCList patchNames, SCList patchShas) {

	patchesStillToFind = 0;

	// get patch names and status of current branch
	QString runOutput;
	if (!run("stg series", &runOutput))
		return;

	const QStringList pl(runOutput.split('\n', QString::SkipEmptyParts));
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
		Reference* cur = lookupReference(patchShas.at(pos), optCreate);
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
	return runOutput.split('\n', QString::SkipEmptyParts);
}

Rev* Git::fakeRevData(SCRef sha, SCList parents, SCRef author, SCRef date, SCRef log, SCRef longLog,
                      SCRef patch, int idx, FileHistory* fh) {

	QString data("commit " + sha + ' ' + parents.join(" ") + "\ntree ");
	data.append(sha);
	data.append("\nparent " + parents.join("\nparent "));
	data.append("\nauthor " + author + " " + date);
	data.append("\ncommitter " + author + " " + date);
	data.append("\n\n    " + log + '\n');
	data.append(longLog);
	data.append(patch);

	QByteArray* ba = new QByteArray(data.toAscii());
	ba->append('\0');

	fh->rowData.append(ba);
	int dummy;
	Rev* c = new Rev(*ba, 0, idx, &dummy, !isMainHistory(fh));
	return c;
}

const Rev* Git::fakeWorkDirRev(SCRef parent, SCRef log, SCRef longLog, int idx, FileHistory* fh) {

	QString patch;
	if (!isMainHistory(fh))
		patch = getWorkDirDiff(fh->fileName());

	QString date(QString::number(QDateTime::currentDateTime().toTime_t()) + " +0200");
	QString author("Working Dir");
	QStringList parents(parent);
	Rev* c = fakeRevData(ZERO_SHA, parents, author, date, log, longLog, patch, idx, fh);
	c->isDiffCache = true;
	c->lanes.append(EMPTY);
	return c;
}

const RevFile* Git::fakeWorkDirRevFile(const WorkingDirInfo& wd) {

	RevFile* rf = new RevFile();
	parseDiffFormat(*rf, wd.diffIndex);
	rf->onlyModified = false;

	FOREACH_SL (it, wd.otherFiles) {

		appendFileName(*rf, *it);
		rf->status.append(RevFile::UNKNOWN);
		rf->mergeParent.append(1);
	}
	RevFile cachedFiles;
	parseDiffFormat(cachedFiles, wd.diffIndexCached);
	for (int i = 0; i < rf->count(); i++)
		if (findFileIndex(cachedFiles, filePath(*rf, i)) != -1)
			rf->status[i] |= RevFile::IN_INDEX;
	return rf;
}

void Git::getDiffIndex() {

	QString status;
	if (!run("git status", &status)) // git status refreshes the index, run as first
		return;

	if (!run("git diff-index HEAD", &_wd.diffIndex))
		return;

	// check for files already updated in cache, we will
	// save this information in status third field
	if (!run("git diff-index --cached HEAD", &_wd.diffIndexCached))
		return;

	// get any file not in tree
	_wd.otherFiles = getOthersFiles();

	// now mockup a RevFile
	revsFiles.insert(ZERO_SHA, fakeWorkDirRevFile(_wd));

	// then mockup the corresponding Rev
	QString parent;
	if (!run("git rev-parse HEAD", &parent))
		return;

	parent = parent.section('\n', 0, 0);
	SCRef log = (isNothingToCommit() ? "Nothing to commit" : "Working dir changes");
	const Rev* r = fakeWorkDirRev(parent, log, status, revData->revOrder.count(), revData);
	revData->revs.insert(ZERO_SHA, r);
	revData->revOrder.append(ZERO_SHA);

	// finally send it to GUI
	emit newRevsAdded(revData, revData->revOrder);
}

void Git::parseDiffFormatLine(RevFile& rf, SCRef line, int parNum) {

	if (line[1] == ':') { // it's a combined merge

		// TODO rename/copy is not supported for combined merges
		appendFileName(rf, line.section('\t', -1));
		setStatus(rf, line.section('\t', 0, 0).section(' ', -1, -1).left(1));
		rf.mergeParent.append(parNum);
	} else { // faster parsing in normal case

		if (line.at(98) == '\t') {
			appendFileName(rf, line.mid(99));
			setStatus(rf, line.at(97));
			rf.mergeParent.append(parNum);
		} else
			// it's a rename or a copy, we are not in fast path now!
			setExtStatus(rf, line.mid(97), parNum);
	}
}

void Git::setStatus(RevFile& rf, SCRef rowSt) {

	char status = rowSt.at(0).toLatin1();
	switch (status) {
	case 'M':
	case 'T':
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

void Git::setExtStatus(RevFile& rf, SCRef rowSt, int parNum) {

	const QStringList sl(rowSt.split('\t', QString::SkipEmptyParts));
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
	appendFileName(rf, dest);
	rf.mergeParent.append(parNum);
	rf.status.append(RevFile::NEW);
	rf.extStatus.resize(rf.status.size());
	rf.extStatus[rf.status.size() - 1] = extStatusInfo;

	// simulate deleted orig file only in case of rename
	if (type.at(0) == 'R') { // renamed file
		appendFileName(rf, orig);
		rf.mergeParent.append(parNum);
		rf.status.append(RevFile::DELETED);
		rf.extStatus.resize(rf.status.size());
		rf.extStatus[rf.status.size() - 1] = extStatusInfo;
	}
	rf.onlyModified = false;
}

void Git::parseDiffFormat(RevFile& rf, SCRef buf) {

	int parNum = 1, startPos = 0, endPos = buf.indexOf('\n');
	while (endPos != -1) {

		SCRef line = buf.mid(startPos, endPos - startPos);
		if (line[0] == ':') // avoid sha's in merges output
			parseDiffFormatLine(rf, line, parNum);
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

	const QString baseCmd("git log --parents --boundary --pretty=raw -z --default HEAD");
	QStringList initCmd(baseCmd.split(' '));
	if (!isMainHistory(fh))
	/*
	   NOTE: we don't use '--remove-empty' option because
	   in case a file is deleted and then a new file with
	   the same name is created again in the same directory
	   then, with this option, file history is truncated to
	   the file deletion revision.
	*/
		initCmd << QString("-r -m -p --full-index").split(' ');
	else
		initCmd << "--topo-order";

	return startParseProc(initCmd + args, fh, QString());
}

bool Git::startUnappliedList() {

	QStringList unAppliedShaList(getAllRefSha(UN_APPLIED));
	if (unAppliedShaList.isEmpty())
		return false;

	// WARNING: with this command 'git log' could send spurious
	// revs so we need some filter out logic during loading
	QStringList cmd(QString("git log --parents --pretty=raw -z ^HEAD").split(' '));
	cmd << unAppliedShaList;
	return startParseProc(cmd, revData, QString());
}

void Git::stop(bool saveCache) {
// normally called when changing directory or closing

	EM_RAISE(exGitStopped);

	// stop all data sending from process and asks them
	// to terminate. Note that process could still keep
	// running for a while although silently
	emit cancelAllProcesses(); // non blocking

	if (cacheNeedsUpdate && saveCache) {

		cacheNeedsUpdate = false;
		if (!filesLoadingCurSha.isEmpty()) // we are in the middle of a loading
			revsFiles.remove(filesLoadingCurSha); // remove partial data

		if (!revsFiles.isEmpty()) {
			POST_MSG("Saving cache. Please wait...");
			EM_PROCESS_EVENTS_NO_INPUT; // to paint the message
			if (!Cache::save(gitDir, revsFiles, dirNamesVec, fileNamesVec))
				dbs("ERROR unable to save file names cache");
		}
	}
}

void Git::clearRevs() {

	revData->clear();
	patchesStillToFind = 0; // TODO TEST WITH FILTERING
	firstNonStGitPatch = "";
	_wd.clear();
	revsFiles.remove(ZERO_SHA);
}

void Git::clearFileNames() {

	qDeleteAll(revsFiles);
	revsFiles.clear();
	fileNamesMap.clear();
	dirNamesMap.clear();
	dirNamesVec.clear();
	fileNamesVec.clear();
	cacheNeedsUpdate = false;
}

bool Git::init(SCRef wd, bool askForRange, QStringList* filterList, bool* quit) {
// normally called when changing git directory. Must be called after stop()

	*quit = false;
	clearRevs();

	/*
	  In _sp we store calling parameters for init2(), because we
	  could call init2() also outside init() in later time.

	  If loading is canceled before init2() starts, then DataLoader
	  does not emit loaded() signal, on_loaded() is not called and
	  init2() does never start, so we don't bother to overwrite any
	  exsisting previous value here.
	*/
	_sp.args.clear();
	_sp.filteredLoading = (filterList != NULL);

	try {
		setThrowOnStop(true);

		// check if repository is valid
		bool repoChanged;
		workDir = getBaseDir(&repoChanged, wd, &isGIT, &gitDir);

		if (repoChanged) {
			localDates.clear();
			clearFileNames();
			fileCacheAccessed = false;
		}
		if (!isGIT) {
			setThrowOnStop(false);
			return false;
		}
		const QString msg1("Path is '" + workDir + "'    Loading ");
		if (!_sp.filteredLoading) {

			// update text codec according to repo settings
			bool dummy;
			QTextCodec::setCodecForCStrings(getTextCodec(&dummy));

			// load references
			POST_MSG(msg1 + "refs...");
			if (!getRefs())
				dbs("WARNING: no tags or heads found");

			// startup input range dialog
			POST_MSG("");
			_sp.args = getArgs(askForRange, quit); // must be called with refs loaded
			if (*quit) {
				setThrowOnStop(false);
				return false;
			}
			// load StGit unapplied patches, must be after getRefs()
			if (isStGIT) {
				loadingUnAppliedPatches = startUnappliedList();
				if (loadingUnAppliedPatches) {

					POST_MSG(msg1 + "StGIT unapplied patches...");
					setThrowOnStop(false);

					// we will continue with init2() at
					// the end of loading...
					return true;
				}
			}
		} else // filteredLoading
			_sp.args << getAllRefSha(BRANCH | RMT_BRANCH) << "--" << *filterList;

		init2();
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

	try {
		setThrowOnStop(true);

		// load working dir files
		if (!_sp.filteredLoading && testFlag(DIFF_INDEX_F)) {
			POST_MSG(msg1 + "working directory changed files...");
			getDiffIndex(); // blocking, we could be in setRepository() now
		}
		POST_MSG(msg1 + "revisions...");
		if (!startRevList(_sp.args, revData))
			dbs("ERROR: unable to start 'git log'");

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

			uint kb = byteSize / 1024;
			float mbs = (float)byteSize / loadTime / 1000;
			QString tmp;
			tmp.sprintf("Loaded %i revisions  (%i KB),   "
			            "time elapsed: %i ms  (%.2f MB/s)",
			            fh->revs.count(), kb, loadTime, mbs);

			if (!tryFollowRenames(fh))
				emit loadCompleted(fh, tmp);

			if (isMainHistory(fh))
				// check for revisions modified files out of fast path
				// let the dust to settle down, so that the first
				// revision is shown to user without noticeable delay
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
		if (!populateRenamedPatches(it.next(), fh->curFileNames, fh, &oldNames, false))
			it.remove();

	if (fh->renamedRevs.isEmpty())
		return false;

	QStringList args;
	args << fh->renamedRevs << "--" << oldNames;
	fh->fileNames << oldNames;
	fh->curFileNames = oldNames;
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
	else if (!run("git diff -r --full-index " + prevFileSha + " " + lastFileSha, &runOutput))
		return false;

	SCRef prevFile = line.section('\t', -1, -1);
	if (!oldNames->contains(prevFile))
		oldNames->append(prevFile);

	// save the patch, will be used later to create a
	// proper graft sha with correct parent info
	if (!runOutput.isEmpty() && fh)
		fh->renamedPatches.insert(renamedSha, runOutput);
	else
		fh->renamedPatches.insert(renamedSha, "similarity index 100%");

	return true;
}

void Git::populateFileNamesMap() {

	for (int i = 0; i < dirNamesVec.count(); ++i)
		dirNamesMap.insert(dirNamesVec[i], i);

	for (int i = 0; i < fileNamesVec.count(); ++i)
		fileNamesMap.insert(fileNamesVec[i], i);
}

void Git::loadFileNames() {
// warning this function is not re-entrant, background
// activity after repository has been loaded

	if (!fileCacheAccessed) { // deferred file names cache load

		fileCacheAccessed = true;
		bool isWorkingDirRevFile = (getFiles(ZERO_SHA) != NULL);
		clearFileNames(); // any already created RevFile will be lost

		if (Cache::load(gitDir, revsFiles, dirNamesVec, fileNamesVec))
			populateFileNamesMap();
		else
			dbs("ERROR: unable to load file names cache");

		if (isWorkingDirRevFile) // re-add ZERO_SHA with new file names indices
			revsFiles.insert(ZERO_SHA, fakeWorkDirRevFile(_wd));
	}

	QString diffTreeBuf;
	FOREACH (StrVect, it, revData->revOrder) {
		if (!revsFiles.contains(*it)) {
			const Rev* c = revLookup(*it);
			if (c->parentsCount() == 1) // skip initials and merges
				diffTreeBuf.append(*it).append('\n');
		}
	}
	if (!diffTreeBuf.isEmpty()) {
		filesLoadingPending = filesLoadingCurSha = "";
		const QString runCmd("git diff-tree -r -C --stdin");
		runAsync(runCmd, this, diffTreeBuf);
	}
	indexTree();
}

int Git::addChunk(FileHistory* fh, const QByteArray& ba, int start) {

	RevMap& r = fh->revs;
	int nextStart;

	// only here we create a new rev
	Rev* rev = new Rev(ba, start, fh->revOrder.count(), &nextStart, !isMainHistory(fh));

	if (nextStart == -1) { // half chunk detected
		delete rev;
		return -1;
	}

	SCRef sha = rev->sha();

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
		    !loadingUnAppliedPatches) {

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
		QString date(rev->authorDate() + " +0200");
		Rev* c = fakeRevData(rev->sha(), rev->parents(), rev->author(),
		                     date, rev->shortLog(), rev->longLog(),
		                     fh->renamedPatches[sha], prevSha->orderIdx, fh);

		r.insert(rev->sha(), c); // overwrite old content
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
		while (r.contains(mergeSha));
		r.insert(mergeSha, rev);
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

		} else if (patchesStillToFind > 0) { // try to avoid costly lookup

			Reference* rf = lookupReference(sha);
			if (rf && (rf->type & APPLIED)) {

				Rev* c = const_cast<Rev*>(revLookup(sha, fh));
				c->isApplied = true;
				patchesStillToFind--;
				if (patchesStillToFind == 0)
					// any rev will be discarded until
					// firstNonStGitPatch arrives
					firstNonStGitPatch = c->parent(0);
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
	if (!files || findFileIndex(*files, fh->fileName()) == -1)
		return false;

	// insert a custom ZERO_SHA rev with proper parent
	const Rev* rf = fakeWorkDirRev(parent, "Working dir changes", "long log\n", 0, fh);
	fh->revs.insert(ZERO_SHA, rf);
	fh->revOrder.append(ZERO_SHA);
	return true;
}

void Git::setLane(SCRef sha, FileHistory* fh) {

	Lanes* l = fh->lns;
	uint i = fh->firstFreeLane;
	const QVector<QString>& shaVec(fh->revOrder);
	for (uint cnt = shaVec.count(); i < cnt; ++i) {
		SCRef curSha = shaVec[i];
		Rev* r = const_cast<Rev*>(revLookup(curSha, fh));
		if (r->lanes.count() == 0)
			updateLanes(*r, *l, curSha);

		if (curSha == sha)
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

	SCRef nextSha = (isInitial) ? "" : c.parent(0);
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
//	qDebug("%s %s",tmp.latin1(), c.sha.latin1());
}

void Git::procReadyRead(const QByteArray& fileChunk) {

	if (filesLoadingPending.isEmpty())
		filesLoadingPending = fileChunk;
	else
		filesLoadingPending.append(fileChunk); // add to previous half lines

	RevFile* rf = NULL;
	if (revsFiles.contains(filesLoadingCurSha))
		rf = const_cast<RevFile*>(revsFiles[filesLoadingCurSha]);

	int nextEOL = filesLoadingPending.indexOf('\n');
	int lastEOL = -1;
	while (nextEOL != -1) {

		SCRef line(filesLoadingPending.mid(lastEOL + 1, nextEOL - lastEOL - 1));
		if (line.at(0) != ':') {
			SCRef sha = line.left(40);
			if (!rf || sha != filesLoadingCurSha) { // new commit
				rf = new RevFile();
				revsFiles.insert(sha, rf);
				filesLoadingCurSha = sha;
				cacheNeedsUpdate = true;
			} else
				dbp("ASSERT: repeated sha %1 in file names loading", sha);
		} else // line.constref(0) == ':'
			parseDiffFormatLine(*rf, line, 1);

		lastEOL = nextEOL;
		nextEOL = filesLoadingPending.indexOf('\n', lastEOL + 1);
	}
	if (lastEOL != -1)
		filesLoadingPending.remove(0, lastEOL + 1);
}

void Git::appendFileName(RevFile& rf, SCRef name) {

	int idx = name.lastIndexOf('/') + 1;
	SCRef dr = name.left(idx);
	SCRef nm = name.mid(idx);

	QMap<QString, int>::const_iterator it(dirNamesMap.find(dr));
	if (it == dirNamesMap.constEnd()) {
		int idx = dirNamesVec.count();
		dirNamesMap.insert(dr, idx);
		dirNamesVec.append(dr);
		rf.dirs.append(idx);
	} else
		rf.dirs.append(*it);

	it = fileNamesMap.find(nm);
	if (it == fileNamesMap.constEnd()) {
		int idx = fileNamesVec.count();
		fileNamesMap.insert(nm, idx);
		fileNamesVec.append(nm);
		rf.names.append(idx);
	} else
		rf.names.append(*it);
}

void Git::updateDescMap(const Rev* r,uint idx, QMap<QPair<uint, uint>, bool>& dm,
                        QMap<uint, QVector<int> >& dv) {

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
		if (qFind(src1.constBegin(), src1.constEnd(), src2[i]) == src1.constEnd())
			dst.append(src2[i]);

	p->descBranches = dst;
	p->descBrnMaster = p->orderIdx;
}

void Git::mergeNearTags(bool down, Rev* p, const Rev* r, const QMap<QPair<uint, uint>, bool>& dm) {

	bool isTag = checkRef(r->sha(), TAG);
	int r_descRefsMaster = isTag ? r->orderIdx : r->descRefsMaster;
	int r_ancRefsMaster = isTag ? r->orderIdx : r->ancRefsMaster;

	if (down && (p->descRefsMaster == r_descRefsMaster || r_descRefsMaster == -1))
		return;

	if (!down && (p->ancRefsMaster == r_ancRefsMaster || r_ancRefsMaster == -1))
		return;

	// we want the nearest tag only, so remove any tag
	// that is ancestor of any other tag in p U r
	const StrVect& ro = revData->revOrder;
	SCRef sha1 = down ? ro[p->descRefsMaster] : ro[p->ancRefsMaster];
	SCRef sha2 = down ? ro[r_descRefsMaster] : ro[r_ancRefsMaster];
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

	const StrVect& ro = revData->revOrder;
	if (ro.count() == 0)
		return;

	// we keep the pairs(x, y). Value is true if x is
	// ancestor of y or false if y is ancestor of x
	QMap<QPair<uint, uint>, bool> descMap;
	QMap<uint, QVector<int> > descVect;

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
				SCRef sha = ro[r->descBrnMaster];
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
				p->childs.append(i);

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
		for (int y = 0; y < r->childs.count(); y++) {

			Rev* c = const_cast<Rev*>(revLookup(ro[r->childs[y]]));
			if (c) {
				if (c->ancRefsMaster == -1)
					c->ancRefsMaster = isTag ? r->orderIdx:r->ancRefsMaster;
				else
					mergeNearTags(!optGoDown, c, r, descMap);
			}
		}
	}
}

// ********************************* Rev **************************

const QString Rev::mid(int start, int len) const {

	// warning no sanity check is done on arguments
	const char* data = ba.constData();
	return QString::fromAscii(data + start, len);
}

const QString Rev::parent(int idx) const {

	return mid(start + boundaryOfs + 41 + 41 * idx, 40);
}

const QStringList Rev::parents() const {

	if (parentsCnt == 0)
		return QStringList();

	int ofs = start + boundaryOfs + 41;
	return mid(ofs, 41 * parentsCnt - 1).split(' ', QString::SkipEmptyParts);
}

int Rev::indexData(bool withDiff) { // fast path here, less then 4% of load time
/*
  This is what 'git log' produces:

	- one line with "commit" + sha + an arbitrary amount of parent's sha, in case
	  of a merge in file history the line terminates with "(from <sha of parent>)"
	- one line with "tree"
	- an arbitrary amount of "parent" lines
	- one line with "author"
	- one line with "committer"
	- zero or more non blank lines with other info, as the encoding
	- one blank line
	- zero or one line with log title
	- zero or more lines with log message
	- zero or more lines with diff content (only for file history)
	- a terminating '\0'
*/
	int last = ba.size() - 1;
	if (start > last)
		return -1;

	boundaryOfs = uint(ba.at(start) == '-');

	parentsCnt = 0;
	int idx = start + boundaryOfs + 40;
	while (idx < last && ba.at(idx) == ' ') {
		idx += 41;
		parentsCnt++;
	}
	if (withDiff && parentsCnt > 1) {
	/* In this case the at end of the line is appended
	   the following info "(from <sha of parent>)" that we
	   have to skip.
	*/
		parentsCnt--;
		idx += 7;
	}
	idx += 47; // idx points to first line '\n', so skip tree line
	while (idx < last && ba.at(idx) == 'p') //skip parents
		idx += 48;

	idx += 23;
	if (idx > last)
		return -1;

	int lineEnd = ba.indexOf('\n', idx); // author line end
	if (lineEnd == -1)
		return -1;

	lineEnd += 23;
	if (lineEnd > last)
		return -1;

	autStart = idx - 16;
	autLen = lineEnd - idx - 24;
	autDateStart = lineEnd - 39;
	autDateLen = 10;

	idx = ba.indexOf('\n', lineEnd); // committer line end
	if (idx == -1)
		return -1;

	// shortlog could be not '\n' terminated so use committer
	// end of line as a safe start point to find chunk end
	int end = ba.indexOf('\0', idx); // this is the slowest find
	if (end == -1)
		return -1;

	// ok, from here we are sure we have a complete chunk
	while (++idx < end && ba.at(idx) != '\n') // check for the first blank line
		idx = ba.indexOf('\n', idx);

	sLogStart = idx + 5;
	if (end < sLogStart) { // no shortlog no longLog and no diff

		sLogStart = sLogLen = 0;
		lLogStart = lLogLen = 0;
		diffStart = diffLen = 0;
		return ++end;
	}
	lLogStart = ba.indexOf('\n', sLogStart);
	if (lLogStart != -1 && lLogStart < end) {

		sLogLen = lLogStart++ - sLogStart;
		lLogLen = end - lLogStart;

	} else { // no longLog and no new line at the end of shortlog
		sLogLen = end - sLogStart;
		lLogStart = lLogLen = 0;
	}
	diffStart = diffLen = 0;
	if (withDiff) {

		diffStart = ba.indexOf("\ndiff ", lineEnd);
		if (diffStart != -1 && diffStart < end) {

			lLogLen = diffStart++ - lLogStart;
			diffLen = end - diffStart;

			// chatch patological cases
			if (sLogStart >= diffStart)
				sLogStart = sLogLen = 0;
			if (lLogStart >= diffStart)
				lLogStart = lLogLen = 0;
		}
	}
	return ++end;
}
