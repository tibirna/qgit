/*
	Description: file history annotation

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include "git.h"
#include "annotate.h"

#define MAX_AUTHOR_LEN 16

Annotate::Annotate(Git* parent, QObject* guiObj) : QObject(parent) {

	EM_INIT(exAnnCanceled, "Canceling annotation");

	git = parent;
	gui = guiObj;
	cancelingAnnotate = annotateRunning = annotateActivity = false;
	valid = canceled = false;

	patchProcBuf.reserve(1000000); // avoid repeated reallocation, will be big!

	processingTime.start();

	connect(&patchProc, SIGNAL(readyReadStandardOutput()),
	        this, SLOT(on_patchProc_readyReadStandardOutput()));

	connect(&patchProc, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(on_patchProc_finished(int, QProcess::ExitStatus)));
}

const FileAnnotation* Annotate::lookupAnnotation(SCRef sha, SCRef fn) {

	if (!valid || fileName != fn)
		return NULL;

	AnnotateHistory::const_iterator it = ah.find(sha);
	if (it != ah.constEnd())
		return &(it.value());

	// ok, we are not lucky. Check for an ancestor before to give up
	int shaIdx;
	const QString ancestorSha = getAncestor(sha, fileName, &shaIdx);
	if (!ancestorSha.isEmpty()) {
		it = ah.find(ancestorSha);
		if (it != ah.constEnd())
			return &(it.value());
	}
	return NULL;
}

bool Annotate::startPatchProc(SCRef buf, SCRef fileName) {

	QString cmd("git diff-tree -r -m --patch-with-raw --no-commit-id --stdin --");
	QStringList args(cmd.split(' ', QString::SkipEmptyParts));
	args.append(fileName); // handle file name with spaces case
	patchProc.setWorkingDirectory(git->workDir);
	patchProcBuf = "";
	return QGit::startProcess(&patchProc, args, buf);
}

void Annotate::on_patchProc_readyReadStandardOutput() {

	const QString tmp(patchProc.readAllStandardOutput());
	annFilesNum += tmp.count("diff --git ");
	if (annFilesNum > (int)histRevOrder.count())
		annFilesNum = histRevOrder.count();
	patchProcBuf.append(tmp);
}

void Annotate::deleteWhenDone() {

	if (!EM_IS_PENDING(exAnnCanceled))
		EM_RAISE(exAnnCanceled);

	if (annotateRunning)
		cancelingAnnotate = true;

	if (patchProc.state() == QProcess::Running)
		patchProc.terminate();

	on_deleteWhenDone();
}

void Annotate::on_deleteWhenDone() {

	if (!(annotateRunning || EM_IS_PENDING(exAnnCanceled)))
		deleteLater();
	else
		QTimer::singleShot(20, this, SLOT(on_deleteWhenDone()));
}

void Annotate::on_progressTimer_timeout() {

	if (!cancelingAnnotate && !isError) {
		const QString n(QString::number(annFilesNum));
		QApplication::postEvent(gui, new AnnotateProgressEvent(n));
	}
}

bool Annotate::start(const FileHistory* _fh) {

	// could change during annotation, so save them
	fh = _fh;
	fileName = fh->fileName();
	histRevOrder = fh->revOrder;

	if (histRevOrder.isEmpty()) {
		valid = false;
		return false;
	}
	annotateRunning = true;

	// init AnnotateHistory
	annFilesNum = 0;
	annId = histRevOrder.count();
	annNumLen = QString::number(histRevOrder.count()).length();
	StrVect::const_iterator it(histRevOrder.constBegin());
	do
		ah.insert(*it, FileAnnotation(annId--));
	while (++it != histRevOrder.constEnd());

	// annotation is split in two parts.
	// first we get the list of sha's to feed git diff-tree.
	// This is very fast.
	isError = false;
	annotateFileHistory(fileName, true);
	cancelingAnnotate = cancelingAnnotate || patchScript.isEmpty(); // only one (initial) rev

	// start an async call to get the patches. This is the slowest part.
	if (isError || cancelingAnnotate || !startPatchProc(patchScript, fileName)) {
		slotComputeDiffs(); // clean-up if something went wrong
		return false;
	}
	connect(&progressTimer, SIGNAL(timeout()),
	        this, SLOT(on_progressTimer_timeout()));

	progressTimer.start(500);
	return true;
}

void Annotate::on_patchProc_finished(int, QProcess::ExitStatus) {

	// start computing diffs only on return from event handler
	QTimer::singleShot(1, this, SLOT(slotComputeDiffs()));
	progressTimer.stop();
}

void Annotate::slotComputeDiffs() {

	// when all the patches are loaded we compute the annotation.
	// this part is normally faster then getting the patches.
	if (!cancelingAnnotate) {

		diffMap.clear();
		AnnotateHistory::iterator it(ah.begin());
		do
			(*it).isValid = false; // reset flags
		while (++it != ah.end());

		// remove first 'next patch' marker
		int first = patchProcBuf.indexOf(':');
		if (first != -1) {
			nextFileSha = patchProcBuf.mid(first + 56, 40);
			patchProcBuf.remove(0, first + 100);
		}
		annotateFileHistory(fileName, false); // now could call Qt event loop
	}
	valid = !(isError || cancelingAnnotate);
	canceled = cancelingAnnotate;
	cancelingAnnotate = annotateRunning = false;
	if (canceled)
		deleteWhenDone();
	else
		git->annotateExited(this);

//	StrVect::const_iterator it(histRevOrder.constBegin());
//	do {
//		dbg(*it); dbg(ah[*it].fileSha);
//	} while (++it != histRevOrder.constEnd());
}

void Annotate::annotateFileHistory(SCRef fileName, bool buildPatchScript) {

	// sweep from the oldest to newest so that parent
	// annotations are calculated before children
	StrVect::const_iterator it(histRevOrder.constEnd());
	do {
		--it;
		doAnnotate(fileName, *it, buildPatchScript);
	} while (it != histRevOrder.constBegin() && !isError && !cancelingAnnotate);
}

void Annotate::doAnnotate(SCRef fileName, SCRef sha, bool buildPatchScript) {
// all the parents annotations must be valid here

	FileAnnotation* fa = getFileAnnotation(sha);
	if (fa == NULL || fa->isValid || isError || cancelingAnnotate)
		return;

	const Rev* r = git->revLookup(sha, fh); // historyRevs
	if (r == NULL) {
		dbp("ASSERT doAnnotate: no revision %1", sha);
		isError = true;
		return;
	}
	if (r->parentsCount() == 0) { // initial revision
		if (!buildPatchScript)
			setInitialAnnotation(fileName, sha, fa); // calls Qt event loop
		fa->isValid = true;
		return;
	}
	// now create a new annotation from first parent diffs
	const QStringList parents(r->parents());
	const QString& parSha = parents.first();
	FileAnnotation* pa = getFileAnnotation(parSha);

	if (!(pa && pa->isValid)) {
		dbp("ASSERT in doAnnotate: annotation for %1 not valid", parSha);
		isError = true;
		return;
	}
	if (buildPatchScript) // just prepare patch script
		updatePatchScript(sha, parSha);
	else {
		const QString diff(getNextPatch(patchProcBuf, fileName, sha));
		const QString author(setupAuthor(r->author(), fa->annId));
		setAnnotation(diff, author, pa->lines, fa->lines);
	}
	// then add other parents diff if any
	QStringList::const_iterator it(parents.constBegin());
	++it;
	while (it != parents.constEnd()) {

		FileAnnotation* pa = getFileAnnotation(*it);

		if (buildPatchScript) { // just prepare patch script
			updatePatchScript(sha, *it);
			++it;
			continue;
		}
		const QString diff(getNextPatch(patchProcBuf, fileName, sha));
		QLinkedList<QString> tmpAnn;
		setAnnotation(diff, "Merge", pa->lines, tmpAnn);

		// the two annotations must be of the same length
		if (fa->lines.count() != tmpAnn.count()) {
			qDebug("ASSERT: merging annotations of different length\n merging "
			       "%s in %s", (*it).toLatin1().constData(), sha.toLatin1().constData());
			isError = true;
			return;
		}
		// finally we unify the annotations
		unify(tmpAnn, fa->lines);
		fa->lines = tmpAnn;
		++it;
	}
	fa->isValid = true;
}

FileAnnotation* Annotate::getFileAnnotation(SCRef sha) {

	AnnotateHistory::iterator it(ah.find(sha));
	if (it == ah.end()) {
		dbp("ASSERT getFileAnnotation: no revision %1", sha);
		isError = true;
		return NULL;
	}
	return &(*it);
}

void Annotate::setInitialAnnotation(SCRef fileName, SCRef sha, FileAnnotation* fa) {

	QString fileSha;
	QByteArray fileData;
	git->getFile(fileName, sha, NULL, &fileData, &fileSha); // calls Qt event loop
	if (cancelingAnnotate)
		return;

	if (fileSha.isEmpty()) {
		dbp("ASSERT in setInitialAnnotation: empty file of initial rev %1", sha);
		isError = true;
		return;
	}
	ah[sha].fileSha = fileSha;
	QString fileTxt(fileData);
	int lineNum = fileTxt.count('\n');
	if (!fileTxt.endsWith("\n")) // No newline at end of file
		lineNum++;

	for (int i = 0; i < lineNum; i++)
		fa->lines.append(QString(""));
}

const QString Annotate::setupAuthor(SCRef origAuthor, int annId) {

	QString author(QString("%1.").arg(annId, annNumLen)); // first field is annotation id
	QString tmp(origAuthor.section('<', 0, 0).trimmed()); // strip e-mail address
	if (tmp.isEmpty()) { // probably only e-mail
		tmp = origAuthor;
		tmp.remove('<').remove('>');
		tmp = tmp.trimmed();
		tmp.truncate(MAX_AUTHOR_LEN);
	}
	// shrink author name if necessary
	if (tmp.length() > MAX_AUTHOR_LEN) {
		SCRef firstName(tmp.section(' ', 0, 0));
		SCRef surname(tmp.section(' ', 1));
		if (!firstName.isEmpty() && !surname.isEmpty())
			tmp = firstName.left(1) + ". " + surname;
		tmp.truncate(MAX_AUTHOR_LEN);
	}
	author.append(tmp);
	return author;
}

void Annotate::unify(SLList dst, SCLList src) {

	QLinkedList<QString>::Iterator itd(dst.begin());
	QLinkedList<QString>::const_iterator its(src.constBegin());
	for ( ; itd != dst.end(); ++itd, ++its)
		if (*itd == "Merge")
			*itd = *its;
}

void Annotate::setAnnotation(SCRef diff, SCRef author, SCLList prevAnn, SLList newAnn, int ofs) {

	newAnn = prevAnn;
	QLinkedList<QString>::iterator cur(newAnn.begin());
	QString line;
	int idx = 0, num, lineNumStart, lineNumEnd;
	while (getNextSection(diff, idx, line, "\n")) {
		char firstChar = line.at(0).toLatin1();
		switch (firstChar) {
		case '@':
			// an unified diff fragment header has form '@@ -a,b +c,d @@'
			// where 'a' is old file line number and 'b' is old file
			// number of lines of the hunk, 'c' and 'd' are the same
			// for new file. If the file does not have enough lines
			// then also the form '@@ -a +c @@' is used.
			lineNumStart = line.indexOf('+') + 1;
			lineNumEnd = line.indexOf(',', lineNumStart);
			if (lineNumEnd == -1) // small file case
				lineNumEnd = line.indexOf(' ', lineNumStart);

			num = line.mid(lineNumStart, lineNumEnd - lineNumStart).toInt();
			num -= ofs; // offset for range filter computation

			// diff lines start from 1, 0 is empty file,
			// instead QValueList::at() starts from 0
			if (num <= 0) { // file is deleted
				if (num < 0)
					dbp("ASSERT processDiff: start line number is %1", num);
				newAnn.clear();
				return;
			}
			cur = newAnn.begin();
			for (int i = 0; i < num - 1; i++) // FIXME save pointer
				++cur;
			break;
		case '+':
			if (cur != newAnn.end()) {
				cur = newAnn.insert(cur, author);
				++cur;
			} else {
				newAnn.append(author);
				cur = newAnn.end();
			}
			break;
		case '-':
			if (!newAnn.isEmpty()) {
				if (cur != newAnn.end())
					cur = newAnn.erase(cur);
				else {
					dbp("ASSERT processDiff: remove end of "
					    "file, diff is %1", diff);
					isError = true;
					return;
				}
			} else {
				dbp("ASSERT processDiff: remove line from "
				    "empty annotation, diff is %1", diff);
				isError = true;
				return;
			}
			break;
		case '\\':
			// diff(1) produces a "\ No newline at end of file", but the
			// message is locale dependent, so just test the space after '\'
			if (line[1] == ' ')
				break;
			else
				; // fall through
		default:
			++cur;
			break;
		}
	}
}

void Annotate::updatePatchScript(SCRef sha, SCRef par) {

	if (sha == QGit::ZERO_SHA) // diff-tree --stdin doesn't work with working
		return;            // dir patch will be directly fetched when needed

	const QString runCmd(sha + " " + par);
	patchScript.append(runCmd).append('\n');
}

const QString Annotate::getNextPatch(QString& patchFile, SCRef fileName, SCRef sha) {

	if (sha == QGit::ZERO_SHA) {
		// diff-tree --stdin doesn't work with working dir so get it
		// directly. We don't need file sha info because is ZERO_SHA
		QString runOutput;
		QString runCmd("git diff-index -r -m -p HEAD -- " + QGit::QUOTE_CHAR +
				fileName + QGit::QUOTE_CHAR);

		git->run(runCmd, &runOutput);
		if (cancelingAnnotate)
			return "";

		// restore removed head line of next patch
		const QString nextHeader("\n:100644 100644 " + QGit::ZERO_SHA + ' '
		                         + nextFileSha + " M\t" + fileName + '\n');
		runOutput.append(nextHeader);
		patchFile.prepend(runOutput);
		nextFileSha = QGit::ZERO_SHA;
	}
	// use patchProcBuf to get proper diff. Patches in patchProcBuf are
	// correctly ordered, so take the first patch
	ah[sha].fileSha = nextFileSha;

	bool noNewLine = (patchFile[0] == ':');
	if (noNewLine)
		dbp("WARNING: No newline at the end of %1 patch", sha);

	int end = (noNewLine) ? 0 : patchFile.indexOf("\n:");
	QString diff;
	if (end != -1) {
		diff = patchFile.left(end + 1);
		nextFileSha = patchFile.mid(end + 57 - (int)noNewLine, 40);
		patchFile.remove(0, end + 100); // the whole line until file name
	} else
		diff = patchFile;

	int start = diff.indexOf('@');
	// handle a possible file mode only change and remove header
	diff = (start != -1) ? diff.mid(start) : "";

	int i = 0;
	while (diffMap.contains(Key(sha, i)))
		i++;
	diffMap.insert(Key(sha, i), diff);
	return diff;
}

bool Annotate::getNextSection(SCRef d, int& idx, QString& sec, SCRef target) {

	if (idx >= (int)d.length())
		return false;

	int newIdx = d.indexOf(target, idx);
	if (newIdx == -1) // last section, take all
		newIdx = d.length() - 1;

	sec = d.mid(idx, newIdx - idx + 1);
	idx = newIdx + 1;
	return true;
}


// ****************************** RANGE FILTER *****************************



bool Annotate::getRange(SCRef sha, RangeInfo* r) {

	if (!rangeMap.contains(sha) || !valid || canceled) {
		r->clear();
		return false;
	}
	*r = rangeMap[sha]; // by copy
	return true;
}

void Annotate::updateCrossRanges(SCRef chunk, bool rev, int fileLen, int ofs, RangeInfo* r) {

/* here the deal is to fake a file that will be modified by chunk, the
   file must contain also the whole output range.

   Then we apply an annotation step and see what happens...

   First we mark each line of the file with the corresponding line number,
   then apply the patch and check where the lines have been moved around.

   Now we have to cases:

   - In reverse case we infer the before-patch range knowing the after-patch range.
     So we check what lines we have in the region corresponding to after-patch range.

   - In forward case we infer the after-patch range knowing the before-patch range.
     So we scan the resulting annotation to find line numbers corresponding to the
     before-patch range.

*/
	// because of the padding the file first line number will be
	//
	// fileFirstLineNr = newLineId - beforePadding = fileOffset + 1
	QLinkedList<QString> beforeAnn;
	QLinkedList<QString> afterAnn;
	for (int lineNum = ofs + 1; lineNum <= ofs + fileLen; lineNum++)
		beforeAnn.append(QString::number(lineNum));

	const QString fakedAuthor("*");
	setAnnotation(chunk, fakedAuthor, beforeAnn, afterAnn, ofs);
	int newStart = ofs + 1;
	int newEnd = ofs + fileLen;

	if (rev) {
		// let's see what line number we have at given range interval limits.
		// at() counts from 0.
		//QLinkedList<QString>::const_iterator itStart(afterAnn.at(r->start - ofs - 1));
		//QLinkedList<QString>::const_iterator itEnd(afterAnn.at(r->end - ofs - 1));
		QLinkedList<QString>::const_iterator itStart(afterAnn.begin());
		for (int i = 0; i < r->start - ofs - 1; i++)
			++itStart;

		QLinkedList<QString>::const_iterator itEnd(afterAnn.begin());
		for (int i = 0; i < r->end - ofs - 1; i++)
			++itEnd;

		bool leftExtended = (*itStart == fakedAuthor);
		bool rightExtended = (*itEnd == fakedAuthor);

		// if range boundary is a line added by the patch
		// we consider inclusive and extend the range
		++itStart;
		do {
			--itStart;
			if (*itStart != fakedAuthor) {
				newStart = (*itStart).toInt();
				break;
			}
		} while (itStart != afterAnn.constBegin());

		while (itEnd != afterAnn.constEnd()) {

			if (*itEnd != fakedAuthor) {
				newEnd = (*itEnd).toInt();
				break;
			}
			++itEnd;
		}
		if (leftExtended && *itStart != fakedAuthor)
			newStart++;

		if (rightExtended && itEnd != afterAnn.constEnd())
			newEnd--;

		r->modified = (leftExtended || rightExtended);

		if (!r->modified) { // check for consecutive sequence
			for (int i = r->start; i <= r->end; ++i, ++itStart)
				if (i - r->start != (*itStart).toInt() - newStart) {
					r->modified = true;
					break;
				}
		}
		if (newStart > newEnd) // selected range is whole inside new added lines
			newStart = newEnd = 0;

	} else { // forward case

		// scan afterAnn to check for before-patch range boundaries
		QLinkedList<QString>::const_iterator itStart(afterAnn.constEnd());
		QLinkedList<QString>::const_iterator itEnd(afterAnn.constEnd());

		QLinkedList<QString>::const_iterator it(afterAnn.begin());
		for (int lineNum = ofs + 1; it != afterAnn.constEnd(); ++lineNum, ++it) {

			if (*it != fakedAuthor) {

				if ((*it).toInt() <= r->start) {
					newStart = lineNum;
					itStart = it;
				}
				if (  (*it).toInt() >= r->end
				    && itEnd == afterAnn.constEnd()) { // one-shot
					newEnd = lineNum;
					itEnd = it;
				}
			}
		}
		if (itStart != afterAnn.constEnd() && (*itStart).toInt() < r->start)
			newStart++;

		if (itEnd != afterAnn.constEnd() && (*itEnd).toInt() > r->end)
			newEnd--;

		r->modified = (itStart == afterAnn.constEnd() || itEnd == afterAnn.constEnd());

		if (!r->modified) { // check for consecutive sequence
			for (int i = r->start; i <= r->end; ++itStart, i++)
				if ((*itStart).toInt() != i) {
				r->modified = true;
				break;
				}
		}
		if (newStart > newEnd) // selected range has been deleted
			newStart = newEnd = 0;
	}
	r->start = newStart;
	r->end = newEnd;
}

void Annotate::updateRange(RangeInfo* r, SCRef diff, bool reverse) {

	r->modified = false;
	if (r->start == 0)
		return;

	// r(start, end) is updated after each chunk incrementally and
	// not at the end of the whole diff to be always in sync with
	// chunk headers that are updated in the same way by GNU diff.
	//
	// so in case of reverse we have to apply the chunks from last
	// one to first.
	int idx = 0;
	QString chunk;
	QStringList chunkList;
	while (getNextSection(diff, idx, chunk, "\n@"))
		if (reverse)
			chunkList.prepend(chunk);
		else
			chunkList.append(chunk);

	QStringList::const_iterator chunkIt(chunkList.constBegin());
	while (chunkIt != chunkList.constEnd()) {

		// an unified diff fragment header has form '@@ -a,b +c,d @@'
		// where 'a' is old file line number and 'b' is old file
		// number of lines of the hunk, 'c' and 'd' are the same
		// for new file. If the file does not have enough lines
		// then also the form '@@ -a +c @@' is used.
		chunk  = *chunkIt++;
		int m  = chunk.indexOf('-');
		int c1 = chunk.indexOf(',', m);
		int p  = chunk.indexOf('+', c1);
		int c2 = chunk.indexOf(',', p);
		int e  = chunk.indexOf(' ', c2);

		int oldLineCnt = chunk.mid(c1 + 1, p - c1 - 2).toInt();
		int newLineId = chunk.mid(p + 1, c2 - p - 1).toInt();
		int newLineCnt = chunk.mid(c2 + 1, e - c2 - 1).toInt();
		int lineNumDiff = newLineCnt - oldLineCnt;

		// because r(start, end) is updated after each chunk we have to
		// consider the updated patch delimiters to compare with r(start, end)
		int patchStart = newLineId;

		// patch end depends only to lines count so is...
		int patchEnd = patchStart + ((reverse) ? newLineCnt : oldLineCnt);
		patchEnd--; // with 1 line patch patchStart == patchEnd

		// case 1: patch range after our range
		if (patchStart > r->end)
			continue;

		// case 2: patch range before our range
		if (patchEnd < r->start) {
			r->start += ((reverse) ? -lineNumDiff : lineNumDiff);
			r->end += ((reverse) ? -lineNumDiff : lineNumDiff);
			continue;
		}
		// case 3: the patch is whole inside our range
		if (patchStart >= r->start && patchEnd <= r->end) {
			r->end += ((reverse) ? -lineNumDiff : lineNumDiff);
			r->modified = true;
			continue;
		}
		// case 4: ranges are crossing
		// add padding so that resulting file is the UNION: selectRange U patchRange

		// reverse independent
		int beforePadding = (r->start > patchStart) ? 0 : patchStart - r->start;

		// reverse dependent
		int afterPadding = (patchEnd > r->end) ? 0 : r->end - patchEnd;

		// file is the faked file on which we will apply the diff,
		// so it is always the _old_ before the patch one.
		int fileLenght = beforePadding + oldLineCnt + afterPadding;

		// given the chunk header @@ -a,b +c,d @@, line nr. 'c' must correspond
		// to the file line nr. 1 because we have only a partial file.
		// More, there is also the file padding to consider.
		// So we need that 'c' corresponds to file line nr. 'beforePadding + 1'
		//
		// the transformation in setAnnotation() is
		//     newLineNum = 'c' - offset = beforePadding + 1
		// so...
		int fileOffset = newLineId - beforePadding - 1;

		// because of the padding the file first line number will be
		//     fileFirstLineNr = newLineId - beforePadding = fileOffset + 1
		updateCrossRanges(chunk, reverse, fileLenght, fileOffset, r);
	}
}

const QString Annotate::getAncestor(SCRef sha, SCRef fileName, int* shaIdx) {

	QString fileSha;

	try {
		annotateActivity = true;
		EM_REGISTER(exAnnCanceled);

		fileSha = git->getFileSha(fileName, sha); // calls qApp->processEvents()
		if (fileSha.isEmpty()) {
			dbp("ASSERT in getAncestor: empty file from %1", sha);
			return "";
		}
		EM_REMOVE(exAnnCanceled);
		annotateActivity = false;

	} catch(int i) {

		EM_REMOVE(exAnnCanceled);
		annotateActivity = false;

		if (EM_MATCH(i, exAnnCanceled, "getting ancestor")) {
			EM_THROW_PENDING;
			return "";
		}
		const QString info("Exception \'" + EM_DESC(i) + "\' "
		                   "not handled in Annotation lookup...re-throw");
		dbs(info);
		throw;
	}
	// NOTE: more then one revision could have the same file sha as our
	// input revision. This happens if the patch is reverted or if the patch
	// modify only file mode but no content. From the point of view of code
	// range filtering this is equivalent, so we don't care to find the correct
	// ancestor, but just the first revision with the same file sha
	for (*shaIdx = 0; *shaIdx < (int)histRevOrder.count(); (*shaIdx)++) {

		const FileAnnotation& fa(ah[histRevOrder[*shaIdx]]);
		if (fa.fileSha == fileSha)
			return histRevOrder[*shaIdx];
	}
	dbp("ASSERT in getAncestor: ancestor of %1 not found", sha);
	return "";
}

bool Annotate::isDescendant(SCRef sha, SCRef target) {
// quickly check if target is a direct descendant of sha, i.e. if starting
// from target, sha could be reached walking along his parents. In case
// a merge is found the search returns false because you'll need,
// in general, all the previous ranges to compute the target one.

	const Rev* r = git->revLookup(sha, fh);
	if (!r)
		return false;

	int shaIdx = r->orderIdx;
	r = git->revLookup(target, fh);

	while (r && r->orderIdx < shaIdx && r->parentsCount() == 1)
		r = git->revLookup(r->parent(0), fh);

	return (r && r->orderIdx == shaIdx);
}

/*
  Range filtering uses only annotated added/removed lines to compute ranges,
  patch content is never checked, this algorithm is fast but fails in case of
  code shuffle; if a patch moves some code between two independents part of the
  same file this will be interpreted as a delete of origin code. New added code
  is not recognized as the same of old one because content is not checked.

  Only checking for copies would fix the corner case, but implementation
  is too difficult, so better accept this design limitation for now.
*/
const QString Annotate::computeRanges(SCRef sha, int rangeStart, int rangeEnd, SCRef target) {

	rangeMap.clear();

	if (!valid || canceled) {
		dbp("ASSERT in computeRanges: annotation from %1 not valid", sha);
		return "";
	}
	QString ancestor(sha);
	int shaIdx;
	for (shaIdx = 0; shaIdx < (int)histRevOrder.count(); shaIdx++)
		if (histRevOrder[shaIdx] == sha)
			break;

	if (shaIdx == (int)histRevOrder.count()) { // not in history, find an ancestor
		ancestor = getAncestor(sha, fileName, &shaIdx);
		if (ancestor.isEmpty())
			return "";
	}
	// insert starting one, always included by default, could be removed after
	rangeMap.insert(ancestor, RangeInfo(rangeStart, rangeEnd, true));

	// check if target is a descendant, so to skip back history walking
	bool isDirectDescendant = isDescendant(ancestor, target);

	// going back in history, to oldest following first parent lane
	const QString oldest(histRevOrder.last()); // causes a detach!
	const Rev* curRev = git->revLookup(ancestor, fh); // historyRevs
	QString curRevSha(curRev->sha());
	while (curRevSha != oldest && !isDirectDescendant) {

		if (!diffMap.contains(Key(curRevSha, 0))) {
			if (curRev->parentsCount() == 0)  // is initial
				break;

			dbp("ASSERT in rangeFilter 1: diff for %1 not found", curRevSha);
			return "";
		}
		RangeInfo r(rangeMap[curRevSha]);
		updateRange(&r, diffMap[Key(curRevSha, 0)], true);

		// special case for modified flag. Mark always the 'after patch' revision
		// with modified flag, not the before patch. So we have to stick the flag
		// to the newer revision.
		rangeMap[curRevSha].modified = r.modified;

		// if the second revision does not modify the range then r.modified == false
		// and the first revision range is created with modified == false, if the
		// first revision is initial then the loop is exited without update the flag
		// and the first revision is missed.
		//
		// we want to always include first revision as a compare base also if
		// does not modify anything. Of course range must be valid.
		r.modified = (r.start != 0);

		if (curRev->parentsCount() == 0)
			break;

		curRev = git->revLookup(curRev->parent(0), fh);
		curRevSha = curRev->sha();
		rangeMap.insert(curRevSha, r);

		if (curRevSha == target) // stop now, no need to continue
			return ancestor;
	}
	// now that we have initial revision go back and sweep up all the
	// remaining stuff, we are guaranteed a good parent is always
	// found but in case of an independent branch, see below
	if (!isDirectDescendant)
		shaIdx = histRevOrder.count() - 1;

	for ( ; shaIdx >= 0; shaIdx--) {

		SCRef sha(histRevOrder[shaIdx]);

		if (!rangeMap.contains(sha)) {

			curRev = git->revLookup(sha, fh);

			if (curRev->parentsCount() == 0) {
				// the start of an independent branch is found in this case
				// insert an empty range, the whole branch will be ignored.
				// Merge of outside branches are very rare so this solution
				// seems enough if we don't want to dive in (useless) complications.
				rangeMap.insert(sha, RangeInfo());
				continue;
			}
			if (!diffMap.contains(Key(sha, 0))) {
				dbp("ASSERT in rangeFilter 2: diff for %1 not found", sha);
				return "";
			}
			SCRef parSha(curRev->parent(0));

			if (!rangeMap.contains(parSha)) {

				if (isDirectDescendant) // we must be in a parallel lane, no need
					continue;       // to compute range info, simply go on

				dbp("ASSERT in rangeFilter: range info for %1 not found", parSha);
				return "";
			}
			RangeInfo r(rangeMap[parSha]);
			updateRange(&r, diffMap[Key(sha, 0)], false);
			rangeMap.insert(sha, r);

			if (sha == target) // stop now, no need to continue
				return ancestor;
		}
	}
	return ancestor;
}

bool Annotate::seekPosition(int* paraFrom, int* paraTo, SCRef fromSha, SCRef toSha) {

	if ((*paraFrom == 0 && *paraTo == 0) || fromSha == toSha)
		return true;

	QMap<QString, RangeInfo> backup;
	backup = rangeMap;  // QMap is implicitly shared

	// paragraphs start from 0 but ranges from 1
	if (computeRanges(fromSha, *paraFrom + 1, *paraTo + 1, toSha).isEmpty())
		goto fail;

	if (!rangeMap.contains(toSha))
		goto fail;

	*paraFrom = rangeMap[toSha].start - 1;
	*paraTo = rangeMap[toSha].end - 1;
	rangeMap = backup;
	return true;

fail:
	rangeMap = backup;
	return false;
}
