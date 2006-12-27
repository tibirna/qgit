/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <q3textedit.h>
#include <q3syntaxhighlighter.h>
#include <q3listview.h>
#include <qmessagebox.h>
#include <qstatusbar.h>
#include <qapplication.h>
#include <qeventloop.h>
#include <qcursor.h>
#include <qregexp.h>
#include <qclipboard.h>
#include <qtoolbutton.h>
#include "domain.h"
#include "myprocess.h"
#include "mainimpl.h"
#include "git.h"
#include "annotate.h"
#include "filecontent.h"

#define MAX_LINE_NUM 5

const QString FileContent::HTML_HEAD       = "<font color=\"#C0C0C0\">"; // light gray
const QString FileContent::HTML_TAIL       = "</font>";
const QString FileContent::HTML_FILE_START = "<pre><tt>";
const QString FileContent::HTML_FILE_END   = "</tt></pre>";

class FileHighlighter : public Q3SyntaxHighlighter {
public:
	FileHighlighter(Q3TextEdit* te, FileContent* fc) : Q3SyntaxHighlighter(te), f(fc) {};
	virtual int highlightParagraph(const QString& p, int) {

		if (f->isHtmlSource)
			return 0;

		if (!f->isRangeFilterActive)
			setFormat(0, p.length(), textEdit()->font());

		int headLen = f->isAnnotationAppended ? f->annoLen + MAX_LINE_NUM : MAX_LINE_NUM;
		setFormat(0, headLen, Qt::lightGray);

		if (f->isRangeFilterActive && f->rangeInfo->start != 0)
			if (   f->rangeInfo->start - 1 <= currentParagraph()
			    && f->rangeInfo->end - 1 >= currentParagraph()) {

				QFont fn(textEdit()->font());
				fn.setBold(true);
				setFormat(0, p.length(), fn, Qt::blue);
			}
		return 0;
	}
private:
	FileContent* f;
};

FileContent::FileContent(Domain* dm, Git* g, Q3TextEdit* f) : d(dm), git(g), ft(f) {

	st = &(d->st);

	// init native types
	isRangeFilterActive = isHtmlSource = false;
	isShowAnnotate = true;

	rangeInfo = new RangeInfo();
	fileHighlighter = new FileHighlighter(ft, this);

	ft->setFont(QGit::TYPE_WRITER_FONT);

	clearAnnotate();
	clearText(false);

	connect(ft, SIGNAL(doubleClicked(int,int)), this, SLOT(on_doubleClicked(int,int)));

	connect(git, SIGNAL(annotateReady(Annotate*, const QString&, bool, const QString&)),
	        this, SLOT(on_annotateReady(Annotate*,const QString&,bool, const QString&)));
}

FileContent::~FileContent() {

	clear();
	delete fileHighlighter;
	delete rangeInfo;
}

void FileContent::clearAnnotate() {

	git->cancelAnnotate(annotateObj);
	annotateObj = NULL;
	curAnn = NULL;
	annoLen = 0;
	isAnnotationAvailable = false;
	emit annotationAvailable(false);
}

void FileContent::clearText(bool emitSignal) {

	git->cancelProcess(proc);
	ft->clear();
	fileRowData = fileProcessedData = halfLine = "";
	isFileAvailable = isAnnotationAppended = false;
	curLine = 1;
	if (curAnn)
		curAnnIt = curAnn->lines.constBegin();

	if (emitSignal)
		emit fileAvailable(false);
}

void FileContent::clear() {

	clearAnnotate();
	clearText();
}

void FileContent::setShowAnnotate(bool b) {
// add an annotation if is available and still not appended, this
// can happen if annotation became available while loading the file.
// If isShowAnnotate is unset try to remove any annotation.

	isShowAnnotate = b;

	if (   !isFileAvailable
	    || (curAnn == NULL && isShowAnnotate)
	    || (isAnnotationAppended == isShowAnnotate))
		return;

	// re-feed with file content processData()
	saveScreenState();
	const QString tmp(fileRowData);
	clearText(false); // emitSignal = false
	processData(tmp);
	procFinished(false); // print and call restoreScreenState()
}

void FileContent::setHighlightSource(bool b) {

	if (b && !git->isTextHighlighter()) {
		dbs("ASSERT in setHighlightSource: no highlighter found");
		return;
	}
	ft->setTextFormat(b ? Qt::RichText : Qt::PlainText);
	isHtmlSource = b;
	update(true);
}

void FileContent::update(bool force) {

	bool shaChanged = (st->sha(true) != st->sha(false));
	bool fileNameChanged = (st->fileName(true) != st->fileName(false));

	if (!fileNameChanged && !shaChanged && !force)
		return;

	saveScreenState();

	if (fileNameChanged)
		clear();
	else
		clearText();

	if (!force)
		lookupAnnotation(); // before file loading

	if (isHtmlSource) // both calls bound procFinished() and procReadyRead() slots
		proc = git->getHighlightedFile(st->fileName(), st->sha(), this, NULL);
	else
		proc = git->getFile(st->fileName(), st->sha(), this, NULL); // non blocking

	ss.isValid = false;
	if (isRangeFilterActive)
		getRange(st->sha(), rangeInfo);
	else if (curAnn) {
		// call seekPosition() while loading the file so to shadow the compute time
		int& from = ss.hasSelectedText ? ss.paraFrom : ss.topPara;
		int& to = ss.hasSelectedText ? ss.paraTo : ss.topPara;
		ss.isValid = annotateObj->seekPosition(&from, &to, st->sha(false), st->sha(true));
	}
}

void FileContent::startAnnotate(FileHistory* fh) {

	annotateObj = git->startAnnotate(fh, d); // non blocking
}

uint FileContent::annotateLength(const FileAnnotation* annFile) {

	int maxLen = 0;
	FOREACH (QLinkedList<QString>, it, annFile->lines)
		if ((*it).length() > maxLen)
			maxLen = (*it).length();

	return maxLen;
}

bool FileContent::getRange(SCRef sha, RangeInfo* r) {

	if (annotateObj)
		return  annotateObj->getRange(sha, r);

	return false;
}

void FileContent::goToAnnotation(int revId) {

	if (   !isAnnotationAppended
	    || !curAnn
	    || (revId == 0)
	    || (ft->textFormat() == Qt::RichText)) // setSelection() fails in this case
		return;

	const QString firstLine(QString::number(revId) + ".");
	int idx = 0;
	FOREACH (QLinkedList<QString>, it, curAnn->lines) {
		if ((*it).stripWhiteSpace().startsWith(firstLine)) {
			ft->setSelection(idx, 0, idx, (*it).length());
			return;
		}
		++idx;
	}
}

bool FileContent::goToRangeStart() {

	if (   !isRangeFilterActive
	    || !curAnn
	    || (rangeInfo->start == 0))
		return false;

	// scroll the viewport so that range is at top
	ft->setCursorPosition(rangeInfo->start - 2, 0);
	int t = ft->paragraphRect(rangeInfo->start - 2).top();
	ft->setContentsPos(0, t);
	return true;
}

void FileContent::copySelection() {

	if (!ft->hasSelectedText())
		return;

	int headLen = isAnnotationAppended ? annoLen + MAX_LINE_NUM : MAX_LINE_NUM;
	headLen++; // to count the space after line number

	Qt::TextFormat tf = ft->textFormat();
	ft->setTextFormat(Qt::PlainText); // we want text without formatting tags, this
	QString sel(ft->selectedText());  // trick does not work with getSelection()
	ft->setTextFormat(tf);

	int indexFrom, dummy;
	ft->getSelection(&dummy, &indexFrom, &dummy, &dummy);
	QClipboard* cb = QApplication::clipboard();

	if (indexFrom < headLen && (tf != Qt::RichText)) {
		sel.remove(0, headLen - indexFrom);
		if (sel.isEmpty()) { // an header part, like the author name, was selected
			cb->setText(ft->selectedText(), QClipboard::Clipboard);
			return;
		}
	}
	QRegExp re("\n.{0," + QString::number(headLen) + "}");
	sel.replace(re, "\n");
	cb->setText(sel, QClipboard::Clipboard);
}

bool FileContent::rangeFilter(bool b) {

	isRangeFilterActive = false;

	if (b) {
		if (!annotateObj) {
			dbs("ASSERT in rangeFilter: annotateObj not available");
			return false;
		}
		int indexFrom, paraFrom, paraTo, dummy;
		ft->getSelection(&paraFrom, &indexFrom, &paraTo, &dummy); // does not work with RichText

		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		EM_PROCESS_EVENTS_NO_INPUT;
		QString anc;

		try {
			d->setThrowOnDelete(true);
			// could call qApp->processEvents()
			anc = annotateObj->computeRanges(st->sha(), ++paraFrom, ++paraTo);
			d->setThrowOnDelete(false);

		} catch (int i) {
			d->setThrowOnDelete(false);
			QApplication::restoreOverrideCursor();
			if (d->isThrowOnDeleteRaised(i, "range filtering")) {
				EM_THROW_PENDING;
				return false;
			}
			const QString info("Exception \'" + EM_DESC(i) + "\' "
			                   "not handled in lookupAnnotation...re-throw");
			dbs(info);
			throw;
		}
		QApplication::restoreOverrideCursor();

		if (!anc.isEmpty() && getRange(anc, rangeInfo)) {

			isRangeFilterActive = true;
			fileHighlighter->rehighlight();
			int st = rangeInfo->start - 1;
			ft->setSelection(st, 0, st, 0); // clear selection
			goToRangeStart();
			return true;
		}
	} else {
		ft->setBold(false); // bold if the first paragraph was highlighted
		fileHighlighter->rehighlight();
		ft->setSelection(rangeInfo->start - 1, 0, rangeInfo->end, 0);
		rangeInfo->clear();
	}
	return false;
}

bool FileContent::lookupAnnotation() {

	if (    st->sha().isEmpty()
	    ||  st->fileName().isEmpty()
	    || !isAnnotationAvailable
	    || !annotateObj)
		return false;

	try {
		d->setThrowOnDelete(true);

		// could call qApp->processEvents()
		curAnn = git->lookupAnnotation(annotateObj, st->fileName(), st->sha());

		if (curAnn) {
			if (!curAnn->lines.empty()) {
				annoLen = annotateLength(curAnn);
				curAnnIt = curAnn->lines.constBegin();
			} else
				curAnn = NULL;
		} else {
			dbp("ASSERT in lookupAnnotation: no annotation for %1", st->fileName());
			clearAnnotate();
		}
		d->setThrowOnDelete(false);

	} catch (int i) {

		d->setThrowOnDelete(false);

		if (d->isThrowOnDeleteRaised(i, "looking up annotation")) {
			EM_THROW_PENDING;
			return false;
		}
		const QString info("Exception \'" + EM_DESC(i) + "\' "
		                   "not handled in lookupAnnotation...re-throw");
		dbs(info);
		throw;
	}
	return (curAnn != NULL);
}

void FileContent::saveScreenState() {

	// getSelection() does not work with RichText
	ss.isValid = (ft->textFormat() != Qt::RichText);
	if (!ss.isValid)
		return;

	ss.topPara = ft->paragraphAt(QPoint(ft->contentsX(), ft->contentsY()));
	ss.hasSelectedText = ft->hasSelectedText();
	if (ss.hasSelectedText) {
		ft->getSelection(&ss.paraFrom, &ss.indexFrom, &ss.paraTo, &ss.indexTo);
		ss.annoLen = annoLen;
		ss.isAnnotationAppended = isAnnotationAppended;
	}
}

void FileContent::restoreScreenState() {

	if (!ss.isValid)
		return;

	if (ss.hasSelectedText) {
		// index without previous annotation
		ss.indexFrom -= (ss.isAnnotationAppended ? ss.annoLen : 0);
		ss.indexTo -= (ss.isAnnotationAppended ? ss.annoLen : 0);

		// index with current annotation
		ss.indexFrom += (isAnnotationAppended ? annoLen : 0);
		ss.indexTo += (isAnnotationAppended ? annoLen : 0);
		ss.indexFrom = QMAX(ss.indexFrom, 0);
		ss.indexTo = QMAX(ss.indexTo, 0);
		ft->setSelection(ss.paraFrom, ss.indexFrom, ss.paraTo, ss.indexTo); // slow

	} else if (ss.topPara != 0) {
		int t = ft->paragraphRect(ss.topPara).bottom(); // slow for big files
		ft->setContentsPos(0, t);
	}
	// leave ss in a consistent state with current screen settings
	ss.isAnnotationAppended = isAnnotationAppended;
	ss.annoLen = annoLen;
}

// ************************************ SLOTS ********************************

void FileContent::on_doubleClicked(int para, int) {

	QString id(ft->text(para));
	id = id.section('.', 0, 0, QString::SectionSkipEmpty);
	emit revIdSelected(id.toInt());
}

void FileContent::on_annotateReady(Annotate* readyAnn, const QString& fileName,
                                   bool ok, const QString& msg) {

	if (readyAnn != annotateObj) // Git::annotateReady() is sent to all receivers
		return;

	if (!ok) {
		d->m()->statusBar()->message("Sorry, annotation not available for this file.");
		return;
	}
	if (st->fileName() != fileName) {
		dbp("ASSERT arrived annotation of wrong file <%1>", fileName);
		return;
	}
	d->m()->statusBar()->message(msg, 7000);

	isAnnotationAvailable = true;
	if (lookupAnnotation())
		emit annotationAvailable(true);
}

void FileContent::procReadyRead(const QString& fileChunk) {

	processData(fileChunk);
}

void FileContent::procFinished(bool emitSignal) {

	if (!fileRowData.endsWith("\n"))
		processData(QString("\n")); // fake a trailing new line

	ft->setText(fileProcessedData); // much faster then ft->append()
	isFileAvailable = true;
	if (ss.isValid)
		restoreScreenState(); // could be slow for big files

	if (emitSignal)
		emit fileAvailable(true);
}

uint FileContent::processData(SCRef fileChunk) {

	fileRowData.append(fileChunk);

	QString newLines;
	if (!QGit::stripPartialParaghraps(fileChunk, &newLines, &halfLine))
		return 0;

	const QStringList sl(QStringList::split('\n', newLines, true)); // allowEmptyEntries

	if (fileProcessedData.isEmpty() && isShowAnnotate) { // one shot at the beginning

		// check if it is possible to add annotation while appending data
		// if annotation is not available we will defer this in a separated
		// step, calling setShowAnnotate() at proper time
		isAnnotationAppended = (curAnn != NULL);
	}
	bool isHtmlHeader = (isHtmlSource && curLine == 1);
	bool isHtmlFirstContentLine = false;

	FOREACH_SL (it, sl) {

		if (isHtmlHeader) {
			if ((*it).startsWith(HTML_FILE_START)) {
				isHtmlHeader = false;
				isHtmlFirstContentLine = true;
			} else {
				fileProcessedData.append(*it).append('\n');
				continue;
			}
		}
		// add HTML page header
		if (isHtmlFirstContentLine)
			fileProcessedData.append(HTML_FILE_START);

		// add color tag head
		if (isHtmlSource)
			fileProcessedData.append(HTML_HEAD);

		// add annotation
		if (isAnnotationAppended && curAnn) { // curAnn can change while loading

			if (curAnnIt == curAnn->lines.constEnd()) {

				if (isHtmlSource && (*it) == HTML_FILE_END) {
					fileProcessedData.append(HTML_TAIL).append(*it).append('\n');
					continue;
				} else {
					dbs("ASSERT in FileContent::processData: bad annotate");
					clearAnnotate();
					return 0;
				}
			}
			fileProcessedData.append((*curAnnIt).leftJustify(annoLen));
			++curAnnIt;
		}
		// add line number
		fileProcessedData.append(QString("%1 ").arg(curLine++, MAX_LINE_NUM));

		// add color tag tail
		if (isHtmlSource)
			fileProcessedData.append(HTML_TAIL);

		// finally add content
		if (isHtmlFirstContentLine) {
			isHtmlFirstContentLine = false;
			fileProcessedData.append((*it).section(HTML_FILE_START, 1));
		} else
			fileProcessedData.append(*it);

		fileProcessedData.append('\n'); // removed by stripPartialParaghraps()
	}
	return sl.count();
}
