/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QScrollBar>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QTemporaryFile>
#include "domain.h"
#include "myprocess.h"
#include "mainimpl.h"
#include "git.h"
#include "annotate.h"
#include "filecontent.h"

#define MAX_LINE_NUM 5

static const QString HTML_HEAD       = "<font color=\"#C0C0C0\">"; // light gray
static const QString HTML_TAIL       = "</font>";
static const QString HTML_HEAD_B     = "<b><font color=\"#808080\">"; // bolded dark gray
static const QString HTML_TAIL_B     = "</font></b>";
static const QString HTML_FILE_START = "<pre><tt>";
static const QString HTML_FILE_END   = "</tt></pre>";

class FileHighlighter : public QSyntaxHighlighter {
public:
	FileHighlighter(FileContent* fc) : QSyntaxHighlighter(fc), f(fc) {};
	virtual void highlightBlock(const QString& p) {

		// state is used to count lines, starting from 0
		if (currentBlockState() == -1) // only once
			setCurrentBlockState(previousBlockState() + 1);

		if (f->isHtmlSource)
			return;

		QTextCharFormat fileFormat;
		if (!f->isRangeFilterActive)
			setFormat(0, p.length(), fileFormat);

		fileFormat.setForeground(Qt::lightGray);
		int headLen = MAX_LINE_NUM;

		if (f->isAnnotationAppended) {
			headLen += f->annoLen;
			int annId = (f->curAnn ? f->curAnn->annId : -1);
			int curId = p.section('.', 0, 0).toInt();
			if (curId == annId) {
				fileFormat.setForeground(Qt::darkGray);
				fileFormat.setFontWeight(QFont::Bold);
			}
		}
		setFormat(0, headLen, fileFormat);

		if (f->isRangeFilterActive && f->rangeInfo->start != 0)
			if (   f->rangeInfo->start <= currentBlockState()
			    && f->rangeInfo->end >= currentBlockState()) {

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
	isRangeFilterActive = isHtmlSource = isImageFile = false;
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
	clearText(!optEmitSignal);

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
	fileProcessedData = halfLine = "";
	fileRowData.clear();
	isFileAvailable = isAnnotationAppended = false;
	curLine = 1;

	if (curAnn)
		curAnnIt = curAnn->lines.constBegin();

	if (emitSignal)
		emit fileAvailable(false);
}

void FileContent::clearAll() {

	clearAnnotate();
	clearText(optEmitSignal);
}

void FileContent::setShowAnnotate(bool b) {
// add an annotation if is available and still not appended, this
// can happen if annotation became available while loading the file.
// If isShowAnnotate is unset try to remove any annotation.

	isShowAnnotate = b;

	if (    !isFileAvailable
	    || (!curAnn && isShowAnnotate)
	    || (isAnnotationAppended == isShowAnnotate))
		return;

	// re-feed with file content processData()
	saveScreenState();
	const QByteArray tmp(fileRowData);
	clearText(!optEmitSignal);
	fileRowData = tmp;
	processData(fileRowData);
	procFinished(!optEmitSignal); // print and call restoreScreenState()
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

	if (fileNameChanged) {
		clearAll();
		isImageFile = Git::isImageFile(st->fileName());
	} else
		clearText(optEmitSignal);

	lookupAnnotation(); // before file loading

	// both calls bound procFinished() and procReadyRead() slots
	if (isHtmlSource && !isImageFile)
		proc = git->getHighlightedFile(st->fileName(), st->sha(), this);
	else
		proc = git->getFile(st->fileName(), st->sha(), this); // non blocking

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

bool FileContent::startAnnotate(FileHistory* fh) {

	if (!isImageFile)
		annotateObj = git->startAnnotate(fh, d); // non blocking

	return (annotateObj != NULL);
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

void FileContent::scrollCursorToTop() {

	QRect r = cursorRect();
	QScrollBar* vsb = verticalScrollBar();
	vsb->setValue(vsb->value() + r.top());
}

void FileContent::scrollLineToTop(int lineNum) {

	QTextCursor tc = textCursor();
	tc.movePosition(QTextCursor::Start);
	tc.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineNum);
	setTextCursor(tc);
	scrollCursorToTop();
}

int FileContent::positionToLineNum(int pos) {

	QTextCursor tc = textCursor();
	tc.setPosition(pos);
	return tc.blockNumber();
}

void FileContent::setSelection(int paraFrom, int indexFrom, int paraTo, int indexTo) {

	scrollLineToTop(paraFrom);
	QTextCursor tc = textCursor();
	tc.setPosition(tc.position() + indexFrom);
	tc.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
	int delta = paraTo - paraFrom;
	tc.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, delta);
	tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, indexTo);
	setTextCursor(tc);
}

void FileContent::goToAnnotation(int revId) {

	if (!isAnnotationAppended || !curAnn || (revId == 0))
		return;

	const QString firstLine(QString::number(revId) + ".");
	FOREACH (QLinkedList<QString>, it, curAnn->lines) {
		if ((*it).trimmed().startsWith(firstLine)) {
			moveCursor(QTextCursor::Start);
			if (find(*it))
				scrollCursorToTop();
			break;
		}
	}
}

bool FileContent::goToRangeStart() {

	if (   !isRangeFilterActive
	    || !curAnn
	    || (rangeInfo->start == 0))
		return false;

	scrollLineToTop(rangeInfo->start);
	return true;
}

void FileContent::copySelection() {

	QTextCursor tc = textCursor();
	if (!tc.hasSelection())
		return;

	int headLen = isAnnotationAppended ? annoLen + MAX_LINE_NUM : MAX_LINE_NUM;
	headLen++; // to count the space after line number

	QClipboard* cb = QApplication::clipboard();
	QString sel(tc.selectedText());
	tc.setPosition(tc.selectionStart());
	int colNum = tc.columnNumber();
	if (colNum < headLen) {
		sel.remove(0, headLen - colNum);
		if (sel.isEmpty()) { // an header part, like the author name, was selected
			cb->setText(textCursor().selectedText(), QClipboard::Clipboard);
			return;
		}
	}
	/*
	   Workaround a Qt issue, QTextCursor::selectedText()
	   substitutes '\n' with '\0'. Restore proper content.
	   QString::replace() doesn't seem to work, go with a loop.
	*/
	for (int i = 0; i < sel.length(); i++) {
		const char c = sel.at(i).toLatin1();
		if (c == 0)
			sel[i] = '\n';
	}
	// remove annotate info if any
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
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		EM_PROCESS_EVENTS_NO_INPUT;
		QString ancestor;
		QTextCursor tc = textCursor();
		int paraFrom = positionToLineNum(tc.selectionStart());
		int paraTo = positionToLineNum(tc.selectionEnd());

		try {
			d->setThrowOnDelete(true);
			// could call qApp->processEvents()
			ancestor = annotateObj->computeRanges(st->sha(), paraFrom, paraTo);
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

		if (!ancestor.isEmpty() && getRange(ancestor, rangeInfo)) {

			isRangeFilterActive = true;
			fileHighlighter->rehighlight();
			goToRangeStart();
			return true;
		}
	} else {
		setFontWeight(QFont::Normal); // bold if the first line was highlighted
		fileHighlighter->rehighlight();
 		setSelection(rangeInfo->start, 0, rangeInfo->end, 0);
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

	ss.isValid = true;
	QTextCursor tc = textCursor();
	ss.hasSelectedText = tc.hasSelection();
	if (ss.hasSelectedText) {
		int endPos = tc.selectionEnd();
		ss.paraFrom = positionToLineNum(tc.selectionStart());
		ss.paraTo = positionToLineNum(tc.selectionEnd());
		tc.setPosition(tc.selectionStart());
		ss.indexFrom = tc.columnNumber();
		tc.setPosition(endPos);
		ss.indexTo = tc.columnNumber();
		ss.annoLen = annoLen;
		ss.isAnnotationAppended = isAnnotationAppended;
	} else
		ss.topPara = cursorForPosition(QPoint(1, 1)).blockNumber();
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

		setSelection(ss.paraFrom, ss.indexFrom, ss.paraTo, ss.indexTo);
	} else
		scrollLineToTop(ss.topPara);

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
		d->showStatusBarMessage("Sorry, annotation not available for this file.");
		return;
	}
	if (st->fileName() != fileName) {
		dbp("ASSERT arrived annotation of wrong file <%1>", fileName);
		return;
	}
	d->showStatusBarMessage(msg, 7000);
	isAnnotationAvailable = true;
	if (lookupAnnotation())
		emit annotationAvailable(true);
}

void FileContent::procReadyRead(const QByteArray& fileChunk) {

	fileRowData.append(fileChunk);
	if (!isImageFile)
		processData(fileChunk);
}

void FileContent::procFinished(bool emitSignal) {

	if (isImageFile)
		showFileImage();
	else {
		if (!fileRowData.endsWith("\n"))
			processData("\n"); // fake a trailing new line

		if (isHtmlSource)
			setHtml(fileProcessedData);
		else
			// much faster then append()
			setPlainText(fileProcessedData);

		if (ss.isValid)
			restoreScreenState(); // could be slow for big files
	}
	isFileAvailable = true;

	if (emitSignal)
		emit fileAvailable(true);
}

uint FileContent::processData(const QByteArray& fileChunk) {

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
	const QString* html_head = NULL;
	const QString* html_tail = NULL;

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

		// do we have to highlight annotation info line?
		if (isHtmlSource) {
			html_head = &HTML_HEAD;
			html_tail = &HTML_TAIL;

			if (isAnnotationAppended && curAnn &&
			    curAnnIt != curAnn->lines.constEnd()) {

				int curId = (*curAnnIt).section('.', 0, 0).toInt();
				if (curId == curAnn->annId) {
					html_head = &HTML_HEAD_B;
					html_tail = &HTML_TAIL_B;
				}
			}
		}
		// add color tag head
		if (isHtmlSource)
			fileProcessedData.append(*html_head);

		// add annotation
		if (isAnnotationAppended && curAnn) { // curAnn can change while loading

			if (curAnnIt == curAnn->lines.constEnd()) {

				if (isHtmlSource && (*it) == HTML_FILE_END) {
					fileProcessedData.append(*html_tail).append(*it).append('\n');
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
			fileProcessedData.append(*html_tail);

		// finally add content
		if (isHtmlFirstContentLine) {
			isHtmlFirstContentLine = false;
			fileProcessedData.append((*it).section(HTML_FILE_START, 1));
		} else
			fileProcessedData.append(*it);

		fileProcessedData.append('\n'); // removed by QString::split()
	}
	return sl.count();
}

void FileContent::showFileImage() {

	QTemporaryFile f;
     	if (f.open()) {

		QString header("<p class=Image><img src=\"" +
		               f.fileName() + "\"></p>");

	     	QGit::writeToFile(f.fileName(), fileRowData);
		setHtml(header);
	}
}
