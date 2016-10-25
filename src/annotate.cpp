/*
	Description: file history annotation

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QTimer>
#include "FileHistory.h"
#include "defmac.h"
#include "git.h"
#include "myprocess.h"
#include "annotate.h"

#define MAX_AUTHOR_LEN 16

using namespace QGit;

Annotate::Annotate(Git* parent, QObject* guiObj) : QObject(parent) {

	EM_INIT(exAnnCanceled, "Canceling annotation");

	git = parent;
	gui = guiObj;
	cancelingAnnotate = annotateRunning = annotateActivity = false;
	valid = canceled = isError = false;

	chk_connect_a(this, SIGNAL(annotateReady(Annotate*, bool, const QString&)),
                  git, SIGNAL(annotateReady(Annotate*, bool, const QString&)));
}

const FileAnnotation* Annotate::lookupAnnotation(const QString& sha) {

	if (!valid || sha.isEmpty())
		return NULL;

    AnnotateHistory::const_iterator it = ah.constFind(sha);
	if (it != ah.constEnd())
		return &(it.value());

	// ok, we are not lucky. Check for an ancestor before to give up
	int shaIdx;
	const QString ancestorSha = getAncestor(sha, &shaIdx);
	if (!ancestorSha.isEmpty()) {
        it = ah.constFind(ancestorSha);
		if (it != ah.constEnd())
			return &(it.value());
	}
	return NULL;
}

void Annotate::deleteWhenDone() {

	if (!EM_IS_PENDING(exAnnCanceled))
		EM_RAISE(exAnnCanceled);

	if (annotateRunning)
		cancelingAnnotate = true;

	on_deleteWhenDone();
}

void Annotate::on_deleteWhenDone() {

	if (!(annotateRunning || EM_IS_PENDING(exAnnCanceled)))
		deleteLater();
	else
		QTimer::singleShot(20, this, SLOT(on_deleteWhenDone()));
}

bool Annotate::start(const FileHistory* _fh) {

	// could change during annotation, so save them
	fh = _fh;
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
	ShaVect::const_iterator it(histRevOrder.constBegin());
	do
		ah.insert(*it, FileAnnotation(annId--));
	while (++it != histRevOrder.constEnd());

	// annotating the file history could be time consuming,
	// so return now and use a timer to start annotation
	QTimer::singleShot(100, this, SLOT(slotComputeDiffs()));
	return true;
}

void Annotate::slotComputeDiffs() {

	processingTime.start();

	if (!cancelingAnnotate)
		annotateFileHistory(); // now could call Qt event loop

	valid = !(isError || cancelingAnnotate);
	canceled = cancelingAnnotate;
	cancelingAnnotate = annotateRunning = false;
	if (canceled)
		deleteWhenDone();
	else {
		QString msg("%1 %2");
		msg = msg.arg(ah.count()).arg(processingTime.elapsed());
		emit annotateReady(this, valid, msg);
	}
}

void Annotate::annotateFileHistory() {

	// sweep from the oldest to newest so that parent
	// annotations are calculated before children
	ShaVect::const_iterator it(histRevOrder.constEnd());
	do {
		--it;
		doAnnotate(*it);
	} while (it != histRevOrder.constBegin() && !isError && !cancelingAnnotate);
}

void Annotate::doAnnotate(const ShaString& ss) {
// all the parents annotations must be valid here

	const QString sha(ss);
	FileAnnotation* fa = getFileAnnotation(sha);
	if (fa == NULL || fa->isValid || isError || cancelingAnnotate)
		return;

	const Rev* r = git->revLookup(ss, fh); // historyRevs
	if (r == NULL) {
		dbp("ASSERT doAnnotate: no revision %1", sha);
		isError = true;
		return;
	}
	const QString& diff(getPatch(sha)); // set FileAnnotation::fileSha
	if (r->parentsCount() == 0) { // initial revision
		setInitialAnnotation(ah[ss].fileSha, fa); // calls Qt event loop
		fa->isValid = true;
		return;
	}
	// now create a new annotation from first parent diffs
	const QStringList& parents(r->parents());
	const QString& parSha = parents.first();
	FileAnnotation* pa = getFileAnnotation(parSha);

	if (!pa || !pa->isValid) {
		dbp("ASSERT in doAnnotate: annotation for %1 not valid", parSha);
		isError = true;
		return;
	}
	const QString& author(setupAuthor(r->author(), fa->annId));
	setAnnotation(diff, author, pa->lines, fa->lines);

	// then add other parents diff if any
	QStringList::const_iterator it(parents.constBegin());
	++it;
	int parentNum = 1;
	while (it != parents.constEnd()) {

		FileAnnotation* pa = getFileAnnotation(*it);
		const QString& diff(getPatch(sha, parentNum++));
		QStringList tmpAnn;
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

FileAnnotation* Annotate::getFileAnnotation(const QString& sha) {

    AnnotateHistory::iterator it = ah.find(sha);
	if (it == ah.end()) {
		dbp("ASSERT getFileAnnotation: no revision %1", sha);
		isError = true;
		return NULL;
	}
	return &(*it);
}

void Annotate::setInitialAnnotation(const QString& fileSha, FileAnnotation* fa) {

	QByteArray fileData;

	// fh->fileNames() are in cronological order, so we need the last one
	git->getFile(fileSha, NULL, &fileData, fh->fileNames().last()); // calls Qt event loop
	if (cancelingAnnotate)
		return;

	int lineNum = fileData.count('\n');
	if (!fileData.endsWith('\n') && !fileData.isEmpty()) // No newline at end of file
		lineNum++;

	const QString empty;
	for (int i = 0; i < lineNum; i++)
		fa->lines.append(empty);
}

const QString Annotate::setupAuthor(const QString& origAuthor, int annId) {

	QString tmp(origAuthor.section('<', 0, 0).trimmed()); // strip e-mail address
	if (tmp.isEmpty()) { // probably only e-mail
		tmp = origAuthor;
		tmp.remove('<').remove('>');
		tmp = tmp.trimmed();
		tmp.truncate(MAX_AUTHOR_LEN);
	}
	// shrink author name if necessary
	if (tmp.length() > MAX_AUTHOR_LEN) {
        const QString& firstName(tmp.section(' ', 0, 0).trimmed());
        const QString& surname(tmp.section(' ', 1).trimmed());
		if (!firstName.isEmpty() && !surname.isEmpty())
			tmp = firstName.left(1) + ". " + surname;

		tmp.truncate(MAX_AUTHOR_LEN);
	}
	return QString("%1.%2").arg(annId, annNumLen).arg(tmp);
}

void Annotate::unify(QStringList& dst, const QStringList& src) {

	const QString m("Merge");
	for (int i = 0; i < dst.size(); ++i) {
		if (dst.at(i) == m)
			dst[i] = src.at(i);
	}
}

bool Annotate::setAnnotation(const QString& diff, const QString& author, const QStringList& prevAnn, QStringList& newAnn, int ofs) {

	newAnn.clear();
	QStringList::const_iterator cur(prevAnn.constBegin());
	QString line;
	int idx = 0, num, lineNumStart, lineNumEnd;
	int curLineNum = 1; // warning, starts from 1 instead of 0
	bool inHeader = true;

	while (getNextSection(diff, idx, line, "\n")) {

		char firstChar = line.at(0).toLatin1();

		if (inHeader) {
			if (firstChar == '@')
				inHeader = false;
			else
				continue;
		}
		switch (firstChar) {
		case '@':
			// an unified diff fragment header has form '@@ -a,b +c,d @@'
			// where 'a' is old file line number and 'b' is old file
			// number of lines of the hunk, 'c' and 'd' are the same
			// for new file. If the file does not have enough lines
			// then also the form '@@ -a +c @@' is used.
			if (ofs == 0)
				lineNumStart = line.indexOf('-') + 1;
			else
			// in this case we are given diff fragments with
			// faked small files that span the fragment plus
			// some padding. So we use 'c' instead of 'a' to
			// find the beginning of our patch in the faked file,
			// this value will be offsetted by ofs later
				lineNumStart = line.indexOf('+') + 1;

			lineNumEnd = line.indexOf(',', lineNumStart);
			if (lineNumEnd == -1) // small file case
				lineNumEnd = line.indexOf(' ', lineNumStart);

			num = line.mid(lineNumStart, lineNumEnd - lineNumStart).toInt();
			num -= ofs; // offset for range filter computation

			// diff lines start from 1, 0 is empty file,
			// instead QValueList::at() starts from 0
			if (num < 0 || num > prevAnn.size()) {
				dbp("ASSERT setAnnotation: start line number is %1", num);
				isError = true;
				return false;
			}
			for ( ; curLineNum < num; ++curLineNum) {
				newAnn.append(*cur);
				++cur;
			}
			break;
		case '+':
			newAnn.append(author);
			break;
		case '-':
			if (curLineNum > prevAnn.size()) {
				dbp("ASSERT setAnnotation: remove end of "
				    "file, diff is %1", diff);
				isError = true;
				return false;
			} else {
				++cur;
				++curLineNum;
			}
			break;
		case '\\':
			// diff(1) produces a "\ No newline at end of file", but the
			// message is locale dependent, so just test the space after '\'
			if (line[1] == ' ')
				break;

			// fall through
		default:
			if (curLineNum > prevAnn.size()) {
				dbp("ASSERT setAnnotation: end of "
				    "file reached, diff is %1", diff);
				isError = true;
				return false;
			} else {
				newAnn.append(*cur);
				++cur;
				++curLineNum;
			}
			break;
		}
	}
	// copy the tail
	for ( ; curLineNum <= prevAnn.size(); ++curLineNum) {
		newAnn.append(*cur);
		++cur;
	}
	return true;
}

const QString Annotate::getPatch(const QString& sha, int parentNum) {

	QString mergeSha(sha);
	if (parentNum)
		mergeSha = QString::number(parentNum) + " m " + sha;

	const Rev* r = git->revLookup(mergeSha, fh);
	if (!r)
		return QString();

    const QString& diff = r->diff();
    if (ah[sha].fileSha.isEmpty() && !parentNum) {

		int idx = diff.indexOf("..");
		if (idx != -1)
            ah[sha].fileSha = diff.mid(idx + 2, 40);
		else // file mode change only, same sha of parent
            ah[sha].fileSha = ah[r->parent(0)].fileSha;
	}
	return diff;
}

bool Annotate::getNextSection(const QString& d, int& idx, QString& sec, const QString& target) {

	if (idx >= d.length())
		return false;

	int newIdx = d.indexOf(target, idx);
	if (newIdx == -1) // last section, take all
		newIdx = d.length() - 1;

	sec = d.mid(idx, newIdx - idx + 1);
	idx = newIdx + 1;
	return true;
}


// ****************************** RANGE FILTER *****************************



bool Annotate::getRange(const QString& sha, RangeInfo* r) {

	if (!ranges.contains(sha) || !valid || canceled) {
		r->clear();
		return false;
	}
	*r = ranges[sha]; // by copy
	return true;
}

void Annotate::updateCrossRanges(const QString& chunk, bool rev, int fileLen, int ofs, RangeInfo* r) {

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
	QStringList beforeAnn;
	QStringList afterAnn;
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
		QStringList::const_iterator itStart(afterAnn.constBegin());
		for (int i = 0; i < r->start - ofs - 1; i++)
			++itStart;

		QStringList::const_iterator itEnd(afterAnn.constBegin());
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
		QStringList::const_iterator itStart(afterAnn.constEnd());
		QStringList::const_iterator itEnd(afterAnn.constEnd());

		QStringList::const_iterator it(afterAnn.constBegin());
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

void Annotate::updateRange(RangeInfo* r, const QString& diff, bool reverse) {

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
		int patchEnd = patchStart + (reverse ? newLineCnt : oldLineCnt);
		patchEnd--; // with 1 line patch patchStart == patchEnd

		// case 1: patch range after our range
		if (patchStart > r->end)
			continue;

		// case 2: patch range before our range
		if (patchEnd < r->start) {
			r->start += (reverse ? -lineNumDiff : lineNumDiff);
			r->end += (reverse ? -lineNumDiff : lineNumDiff);
			continue;
		}
		// case 3: the patch is whole inside our range
		if (patchStart >= r->start && patchEnd <= r->end) {
			r->end += (reverse ? -lineNumDiff : lineNumDiff);
			r->modified = true;
			continue;
		}
		// case 4: ranges are crossing
		// add padding so that resulting file is the UNION: selectRange U patchRange

		// reverse independent
		int beforePadding = (r->start > patchStart ? 0 : patchStart - r->start);

		// reverse dependent
		int afterPadding = (patchEnd > r->end ? 0 : r->end - patchEnd);

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

const QString Annotate::getAncestor(const QString& sha, int* shaIdx) {

	QString fileSha;

	try {
		annotateActivity = true;
		EM_REGISTER(exAnnCanceled);

		QStringList fn(fh->fileNames());
		FOREACH_SL (it, fn) {
			fileSha = git->getFileSha(*it, sha); // calls qApp->processEvents()
			if (!fileSha.isEmpty())
				break;
		}
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
	for (*shaIdx = 0; *shaIdx < histRevOrder.count(); (*shaIdx)++) {

		const FileAnnotation& fa(ah[histRevOrder[*shaIdx]]);
		if (fa.fileSha == fileSha)
			return histRevOrder[*shaIdx];
	}
	// ok still not found, this could happen if sha is an unapplied
	// stgit patch. In this case fall back on the first in the list
	// that is the newest.
	if (git->getAllRefSha(Git::UN_APPLIED).contains(sha))
		return histRevOrder.first();

	dbp("ASSERT in getAncestor: ancestor of %1 not found", sha);
	return "";
}

bool Annotate::isDescendant(const QString& sha, const QString& target) {
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
const QString Annotate::computeRanges(const QString& sha, int paraFrom, int paraTo, const QString& target) {

	ranges.clear();

	if (!valid || canceled || sha.isEmpty()) {
		dbp("ASSERT in computeRanges: annotation from %1 not valid", sha);
		return "";
	}
	// paragraphs start from 0 but ranges from 1
	int rangeStart = paraFrom + 1;
	int rangeEnd = paraTo + 1;

    QString ancestor = sha;
    int shaIdx;
    for (shaIdx = 0; shaIdx < histRevOrder.count(); ++shaIdx)
        if (histRevOrder[shaIdx] == sha)
			break;

	if (shaIdx == histRevOrder.count()) { // not in history, find an ancestor
		ancestor = getAncestor(sha, &shaIdx);
		if (ancestor.isEmpty())
			return "";
	}
	// insert starting one, always included by default, could be removed after
	ranges.insert(ancestor, RangeInfo(rangeStart, rangeEnd, true));

	// check if target is a descendant, so to skip back history walking
	bool isDirectDescendant = isDescendant(ancestor, target);

	// going back in history, to oldest following first parent lane
	const QString oldest(histRevOrder.last()); // causes a detach!
	const Rev* curRev = git->revLookup(ancestor, fh); // historyRevs
	QString curRevSha(curRev->sha());
	while (curRevSha != oldest && !isDirectDescendant) {

		const QString& diff(getPatch(curRevSha));
		if (diff.isEmpty()) {
			if (curRev->parentsCount() == 0)  // is initial
				break;

			dbp("ASSERT in rangeFilter 1: diff for %1 not found", curRevSha);
			return "";
		}
		RangeInfo r(ranges[curRevSha]);
		updateRange(&r, diff, true);

		// special case for modified flag. Mark always the 'after patch' revision
		// with modified flag, not the before patch. So we have to stick the flag
		// to the newer revision.
		ranges[curRevSha].modified = r.modified;

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
		ranges.insert(curRevSha, r);

		if (curRevSha == target) // stop now, no need to continue
			return ancestor;
	}
	// now that we have initial revision go back and sweep up all the
	// remaining stuff, we are guaranteed a good parent is always
	// found but in case of an independent branch, see below
	if (!isDirectDescendant)
		shaIdx = histRevOrder.count() - 1;

	for ( ; shaIdx >= 0; shaIdx--) {

        const QString& sha(histRevOrder[shaIdx]);

		if (!ranges.contains(sha)) {

			curRev = git->revLookup(sha, fh);

			if (curRev->parentsCount() == 0) {
				// the start of an independent branch is found in this case
				// insert an empty range, the whole branch will be ignored.
				// Merge of outside branches are very rare so this solution
				// seems enough if we don't want to dive in (useless) complications.
				ranges.insert(sha, RangeInfo());
				continue;
			}
			const QString& diff(getPatch(sha));
			if (diff.isEmpty()) {
				dbp("ASSERT in rangeFilter 2: diff for %1 not found", sha);
				return "";
			}
			QString parSha(curRev->parent(0));

			if (!ranges.contains(parSha)) {

				if (isDirectDescendant) // we must be in a parallel lane, no need
					continue;       // to compute range info, simply go on

				dbp("ASSERT in rangeFilter: range info for %1 not found", parSha);
				return "";
			}
			RangeInfo r(ranges[parSha]);
			updateRange(&r, diff, false);
			ranges.insert(sha, r);

			if (sha == target) // stop now, no need to continue
				return ancestor;
		}
	}
	return ancestor;
}

bool Annotate::seekPosition(int* paraFrom, int* paraTo, const QString& fromSha, const QString& toSha) {

	if ((*paraFrom == 0 && *paraTo == 0) || fromSha == toSha)
		return true;

	Ranges backup(ranges); // implicitly shared

	if (computeRanges(fromSha, *paraFrom, *paraTo, toSha).isEmpty())
		goto fail;

	if (!ranges.contains(toSha))
		goto fail;

	*paraFrom = ranges[toSha].start - 1;
	*paraTo = ranges[toSha].end - 1;
	ranges = backup;
	return true;

fail:
	ranges = backup;
	return false;
}
