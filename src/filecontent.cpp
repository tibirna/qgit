/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QStatusBar>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
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

class FileHighlighter : public QSyntaxHighlighter {
public:
	FileHighlighter(FileContent* fc) : QSyntaxHighlighter(fc), f(fc) {};
	virtual void highlightBlock(const QString& p) {

		// state is used to count paragraphs, starting from 0
		setCurrentBlockState(previousBlockState() + 1);
		if (f->isHtmlSource)
			return;

		QTextCharFormat fileFormat;
		if (!f->isRangeFilterActive)
			setFormat(0, p.length(), fileFormat);

		fileFormat.setForeground(Qt::lightGray);
		int headLen = f->isAnnotationAppended ? f->annoLen + MAX_LINE_NUM : MAX_LINE_NUM;
		setFormat(0, headLen, fileFormat);

		if (f->isRangeFilterActive && f->rangeInfo->start != 0)
			if (   f->rangeInfo->start - 1 <= currentBlockState()
			    && f->rangeInfo->end - 1 >= currentBlockState()) {

				fileFormat.setFontWeight(QFont::Bold);
				fileFormat.setForeground(Qt::blue);
				setFormat(0, p.length(), fileFormat);
			}
		return;
	}
private:
	FileContent* f;
};

FileContent::FileContent(QWidget* parent) : QTextEdit(parent) {

	// init native types
	isRangeFilterActive = isHtmlSource = false;
	isShowAnnotate = true;

	rangeInfo = new RangeInfo();
	fileHighlighter = new FileHighlighter(this);

	setFont(QGit::TYPE_WRITER_FONT);
}

void FileContent::setup(Domain* dm, Git* g) {

	d = dm;
	git = g;
	st = &(d->st);

	clearAnnotate();
	clearText(false);

	connect(git, SIGNAL(annotateReady(Annotate*, const QString&, bool, const QString&)),
	        this, SLOT(on_annotateReady(Annotate*,const QString&,bool, const QString&)));
}

FileContent::~FileContent() {

	clearAll();
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
	QTextEdit::clear(); // explicit call because our clear() is only declared
	fileRowData = fileProcessedData = halfLine = "";
	isFileAvailable = isAnnotationAppended = false;
	curLine = 1;

	if (curAnn)
		curAnnIt = curAnn->lines.constBegin();

	if (emitSignal)
		emit fileAvailable(false);
}

void FileContent::clearAll() {

	clearAnnotate();
	clearText(true);
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
		clearAll();
	else
		clearText(true);

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
	    ) //|| (textFormat() == Qt::RichText)) // setSelection() fails in this case FIXME
		return;

	const QString firstLine(QString::number(revId) + ".");
// 	int idx = 0;
	FOREACH (QLinkedList<QString>, it, curAnn->lines) {
		if ((*it).trimmed().startsWith(firstLine)) {
			find(*it);
// 			ft->setSelection(idx, 0, idx, (*it).length());
// 			return;
			break;
		}
// 		++idx;
	}
}

bool FileContent::goToRangeStart() {

	if (   !isRangeFilterActive
	    || !curAnn
	    || (rangeInfo->start == 0))
		return false;

	// scroll the viewport so that range is at top
// 	ft->setCursorPosition(rangeInfo->start - 2, 0);
// 	int t = ft->paragraphRect(rangeInfo->start - 2).top();
// 	ft->setContentsPos(0, t);
	return true;
}

void FileContent::copySelection() {

	if (!textCursor().hasSelection())
		return;

	int headLen = isAnnotationAppended ? annoLen + MAX_LINE_NUM : MAX_LINE_NUM;
	headLen++; // to count the space after line number

// 	Qt::TextFormat tf = ft->textFormat();
// 	ft->setTextFormat(Qt::PlainText); // we want text without formatting tags, this
// 	QString sel(ft->selectedText());  // trick does not work with getSelection()
// 	ft->setTextFormat(tf);

// 	int indexFrom, dummy;
// 	ft->getSelection(&dummy, &indexFrom, &dummy, &dummy);
	QClipboard* cb = QApplication::clipboard();
	QString sel(textCursor().selectedText());
	int indexFrom = textCursor().columnNumber();
	if (indexFrom < headLen) { // && (tf != Qt::RichText)
		sel.remove(0, headLen - indexFrom);
		if (sel.isEmpty()) { // an header part, like the author name, was selected
			cb->setText(textCursor().selectedText(), QClipboard::Clipboard);
			return;
		}
	}
	QRegExp re("\n.{0," + QString::number(headLen) + "}");
	sel.replace(re, "\n");
	cb->setText(sel, QClipboard::Clipboard);
}

bool FileContent::rangeFilter(bool b) {

	isRangeFilterActive = false;

// 	if (b) {
// 		if (!annotateObj) {
// 			dbs("ASSERT in rangeFilter: annotateObj not available");
// 			return false;
// 		}
// 		int indexFrom, paraFrom, paraTo, dummy;
// 		ft->getSelection(&paraFrom, &indexFrom, &paraTo, &dummy); // does not work with RichText
//
// 		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
// 		EM_PROCESS_EVENTS_NO_INPUT;
// 		QString anc;
//
// 		try {
// 			d->setThrowOnDelete(true);
// 			// could call qApp->processEvents()
// 			anc = annotateObj->computeRanges(st->sha(), ++paraFrom, ++paraTo);
// 			d->setThrowOnDelete(false);
//
// 		} catch (int i) {
// 			d->setThrowOnDelete(false);
// 			QApplication::restoreOverrideCursor();
// 			if (d->isThrowOnDeleteRaised(i, "range filtering")) {
// 				EM_THROW_PENDING;
// 				return false;
// 			}
// 			const QString info("Exception \'" + EM_DESC(i) + "\' "
// 			                   "not handled in lookupAnnotation...re-throw");
// 			dbs(info);
// 			throw;
// 		}
// 		QApplication::restoreOverrideCursor();
//
// 		if (!anc.isEmpty() && getRange(anc, rangeInfo)) {
//
// 			isRangeFilterActive = true;
// 			fileHighlighter->rehighlight();
// 			int st = rangeInfo->start - 1;
// 			ft->setSelection(st, 0, st, 0); // clear selection
// 			goToRangeStart();
// 			return true;
// 		}
// 	} else {
// 		ft->setBold(false); // bold if the first paragraph was highlighted
// 		fileHighlighter->rehighlight();
// 		ft->setSelection(rangeInfo->start - 1, 0, rangeInfo->end, 0);
// 		rangeInfo->clear();
// 	}
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
// 	ss.isValid = (textFormat() != Qt::RichText); FIXME
	ss.isValid = true;
	if (!ss.isValid)
		return;

// 	ss.topPara = ft->paragraphAt(QPoint(ft->contentsX(), ft->contentsY()));
// 	ss.hasSelectedText = ft->hasSelectedText();
// 	if (ss.hasSelectedText) {
// 		ft->getSelection(&ss.paraFrom, &ss.indexFrom, &ss.paraTo, &ss.indexTo);
// 		ss.annoLen = annoLen;
// 		ss.isAnnotationAppended = isAnnotationAppended;
// 	}
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
		ss.indexFrom = qMax(ss.indexFrom, 0);
		ss.indexTo = qMax(ss.indexTo, 0);
// 		ft->setSelection(ss.paraFrom, ss.indexFrom, ss.paraTo, ss.indexTo); // slow FIXME

// 	} else if (ss.topPara != 0) { FIXME
// 		int t = ft->paragraphRect(ss.topPara).bottom(); // slow for big files
// 		ft->setContentsPos(0, t);
	}
	// leave ss in a consistent state with current screen settings
	ss.isAnnotationAppended = isAnnotationAppended;
	ss.annoLen = annoLen;
}

// ************************************ SLOTS ********************************

void FileContent::mouseDoubleClickEvent(QMouseEvent* e) {

	QTextCursor tc = cursorForPosition(e->pos());
	tc.select(QTextCursor::LineUnderCursor);
	QString id(tc.selectedText().section('.', 0, 0, QString::SectionSkipEmpty));
	tc.clearSelection();
	emit revIdSelected(id.toInt());
}

void FileContent::on_annotateReady(Annotate* readyAnn, const QString& fileName,
                                   bool ok, const QString& msg) {

	if (readyAnn != annotateObj) // Git::annotateReady() is sent to all receivers
		return;

	if (!ok) {
		d->m()->statusBar()->showMessage("Sorry, annotation not available for this file.");
		return;
	}
	if (st->fileName() != fileName) {
		dbp("ASSERT arrived annotation of wrong file <%1>", fileName);
		return;
	}
	d->m()->statusBar()->showMessage(msg, 7000);

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

	if (isHtmlSource)
		setHtml(fileProcessedData);
	else
		setPlainText(fileProcessedData); // much faster then append()

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

	const QStringList sl(newLines.split('\n', QString::KeepEmptyParts));

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
			fileProcessedData.append((*curAnnIt).leftJustified(annoLen));
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
