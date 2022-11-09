/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QListWidget>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QScrollBar>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QTemporaryFile>
#include <QAbstractTextDocumentLayout>
#include "domain.h"
#include "myprocess.h"
#include "mainimpl.h"
#include "git.h"
#include "annotate.h"
#include "filecontent.h"

class FileHighlighter : public QSyntaxHighlighter {
public:
	FileHighlighter(FileContent* fc) : QSyntaxHighlighter(fc), f(fc) {}
	virtual void highlightBlock(const QString& p) override {

		// state is used to count lines, starting from 0
		if (currentBlockState() == -1) // only once
			setCurrentBlockState(previousBlockState() + 1);

		if (f->isHtmlSource)
			return;

		if (   f->isRangeFilterActive
		    && f->rangeInfo->start != 0
		    && f->rangeInfo->start <= currentBlockState()
		    && f->rangeInfo->end >= currentBlockState()) {

			QTextCharFormat myFormat;
			myFormat.setFontWeight(QFont::Bold);
			myFormat.setForeground(Qt::blue);
			setFormat(0, p.length(), myFormat);
		}
		return;
	}
private:
	FileContent* f;
};

FileContent::FileContent(QWidget* parent) : QTextEdit(parent) {

	isRangeFilterActive = isHtmlSource = isImageFile = isAnnotationAppended = false;
	isShowAnnotate = true;

	rangeInfo = new RangeInfo();
	fileHighlighter = new FileHighlighter(this);

	setFont(QGit::TYPE_WRITER_FONT);
}

FileContent::~FileContent() {

	clearAll(!optEmitSignal);
	delete fileHighlighter;
	delete rangeInfo;
}

void FileContent::setup(Domain* dm, Git* g, QListWidget* lw) {

	d = dm;
	git = g;
	st = &(d->st);

	listWidgetAnn = lw;
	lw->setParent(this);
	lw->setSelectionMode(QAbstractItemView::NoSelection);
	QPalette pl = lw->palette();
	pl.setColor(QPalette::Text, Qt::lightGray);
	lw->setPalette(pl);

	clearAll(!optEmitSignal);

	connect(d->m(), SIGNAL(typeWriterFontChanged()),
	        this, SLOT(typeWriterFontChanged()));

	connect(git, SIGNAL(annotateReady(Annotate*, bool, const QString&)),
	        this, SLOT(on_annotateReady(Annotate*, bool, const QString&)));

	connect(listWidgetAnn, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
	        this, SLOT(on_list_doubleClicked(QListWidgetItem*)));

	QScrollBar* vsb = verticalScrollBar();
	connect(vsb, SIGNAL(valueChanged(int)),
	        this, SLOT(on_scrollBar_valueChanged(int)));
        vsb->setSingleStep(fontMetrics().lineSpacing());

	vsb = listWidgetAnn->verticalScrollBar();
	connect(vsb, SIGNAL(valueChanged(int)),
	        this, SLOT(on_listScrollBar_valueChanged(int)));
        vsb->setSingleStep(fontMetrics().lineSpacing());
}

void FileContent::on_scrollBar_valueChanged(int value) {

	listWidgetAnn->verticalScrollBar()->setValue(value);
}

void FileContent::on_listScrollBar_valueChanged(int value) {

	verticalScrollBar()->setValue(value);
}

int FileContent::itemAnnId(QListWidgetItem* item) {

	if (item == NULL)
		return 0;
	QString id(item->text());
	if (!id.contains('.'))
		return 0;

	return id.section('.', 0, 0).toInt();
}

void FileContent::on_list_doubleClicked(QListWidgetItem* item) {

	int id = itemAnnId(item);
	if (id)
		emit revIdSelected(id);
}

void FileContent::clearAnnotate(bool emitSignal) {

	git->cancelAnnotate(annotateObj);
	annotateObj = NULL;
	curAnn = NULL;
	isAnnotationLoading = false;

	if (emitSignal)
		emit annotationAvailable(false);
}

void FileContent::clearText(bool emitSignal) {

	git->cancelProcess(proc);
	proc = NULL;
	fileRowData.clear();
	QTextEdit::clear(); // explicit call because our clear() is only declared
	listWidgetAnn->clear();
	isFileAvail = isAnnotationAppended = false;

	if (emitSignal)
		emit fileAvailable(false);
}

void FileContent::clearAll(bool emitSignal) {

	clearAnnotate(emitSignal);
	clearText(emitSignal);
}

void FileContent::setShowAnnotate(bool b) {
// add an annotation if is available and still not appended, this
// can happen if annotation became available while loading the file.
// If isShowAnnotate is false try to remove any annotation.

	isShowAnnotate = b;

	if (    !isFileAvail
	    || (!curAnn && isShowAnnotate)
	    || (isAnnotationAppended == isShowAnnotate))
		return;

	setAnnList();
}

void FileContent::setHighlightSource(bool b) {

	if (b && !git->isTextHighlighter()) {
		dbs("ASSERT in setHighlightSource: no highlighter found");
		return;
	}
	isHtmlSource = b;
	doUpdate(true);
}

void FileContent::doUpdate(bool force) {

	bool shaChanged = (st->sha(true) != st->sha(false));
	bool fileNameChanged = (st->fileName(true) != st->fileName(false));

	if (!fileNameChanged && !shaChanged && !force)
		return;

	saveScreenState();

	if (fileNameChanged) {
		clearAll(optEmitSignal);
		isImageFile = Git::isImageFile(st->fileName());
	} else
		clearText(optEmitSignal);

	lookupAnnotation(); // before file loading

	QString fileSha;
	if (curAnn)
		fileSha = curAnn->fileSha;
	else
		fileSha = git->getFileSha(st->fileName(), st->sha());

	// both calls bound procFinished() and procReadyRead() slots
	if (isHtmlSource && !isImageFile)
		proc = git->getHighlightedFile(fileSha, this, NULL, st->fileName());
	else
		proc = git->getFile(fileSha, this, NULL, st->fileName()); // non blocking

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

bool FileContent::startAnnotate(FileHistory* fh, SCRef ht) {

	if (!isImageFile)
		annotateObj = git->startAnnotate(fh, d); // non blocking

	histTime = ht;
	isAnnotationLoading = (annotateObj != NULL);
	return isAnnotationLoading;
}

uint FileContent::annotateLength(const FileAnnotation* annFile) {

	int maxLen = 0;
	FOREACH_SL (it, annFile->lines)
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

	QScrollBar* vsb = verticalScrollBar();
	vsb->setValue(vsb->value() + cursorRect().top());
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
	if (pos != -1)
		tc.setPosition(pos);

	return tc.blockNumber();
}

int FileContent::lineAtTop() {

	return cursorForPosition(QPoint(1, 1)).blockNumber();
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
	} else
		ss.topPara = lineAtTop();
}

void FileContent::restoreScreenState() {

	if (!ss.isValid)
		return;

	if (ss.hasSelectedText)
		setSelection(ss.paraFrom, ss.indexFrom, ss.paraTo, ss.indexTo);
	else
		scrollLineToTop(ss.topPara);
}

void FileContent::goToAnnotation(int revId, int dir) {

	if (!isAnnotationAppended || !curAnn || (revId == 0))
		return;

	const QString header(QString::number(revId) + ".");
	int row = (dir == 0 ? -1 : lineAtTop());
	QListWidgetItem* itm = NULL;
	do {
		row += (dir >= 0 ? 1 : -1);
		itm = listWidgetAnn->item(row);

		if (itm && itm->text().trimmed().startsWith(header)) {
			scrollLineToTop(row);
			break;
		}
	} while (itm);
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

	QClipboard* cb = QApplication::clipboard();
	QString sel(textCursor().selectedText());
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
	    || isAnnotationLoading
	    || !annotateObj)
		return false;

	try {
		d->setThrowOnDelete(true);

		// could call qApp->processEvents()
		curAnn = git->lookupAnnotation(annotateObj, st->sha());

		if (!curAnn) {
			dbp("ASSERT in lookupAnnotation: no annotation for %1", st->fileName());
			clearAnnotate(optEmitSignal);

		} else if (curAnn->lines.empty())
			curAnn = NULL;

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

void FileContent::on_annotateReady(Annotate* readyAnn, bool ok, const QString& msg) {

	if (readyAnn != annotateObj) // Git::annotateReady() is sent to all receivers
		return;

	isAnnotationLoading = false;

	if (!ok) {
		d->showStatusBarMessage("Sorry, annotation not available for this file.");
		return;
	}
	QString fileNum = msg.section(' ', 0, 0);
	QString annTime = msg.section(' ', 1, 1);
	QString stats("File '%1': revisions %2, history loaded in %3 ms, files annotated in %4 ms");
	d->showStatusBarMessage(stats.arg(st->fileName(), fileNum, histTime, annTime), 12000);

	if (lookupAnnotation())
		emit annotationAvailable(true);
}

void FileContent::typeWriterFontChanged() {

	setFont(QGit::TYPE_WRITER_FONT);

	if (!isHtmlSource && !isImageFile && isFileAvail) {
		setPlainText(toPlainText());
		setAnnList();
	}
}

void FileContent::procReadyRead(const QByteArray& fileChunk) {

	fileRowData.append(fileChunk);
	// set text at the end of loading, much faster
}

void FileContent::procFinished(bool emitSignal) {

	if (isImageFile)
		showFileImage();
	else {
		if (!fileRowData.endsWith("\n"))
			fileRowData.append('\n'); // fake a trailing new line

		if (isHtmlSource)
			setHtml(fileRowData);
		else {
			QTextCharFormat cf; // to restore also default color
			cf.setFont(font());
			setCurrentCharFormat(cf);
			setPlainText(fileRowData); // much faster then append()
		}
	}
	setAnnList();
	isFileAvail = true;
	if (ss.isValid)
		restoreScreenState(); // could be slow for big files
	else
		moveCursor(QTextCursor::Start);

	if (emitSignal)
		emit fileAvailable(true);
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

void FileContent::setAnnList() {

	int linesNum = document()->blockCount();
	int linesNumDigits = QString::number(linesNum).length();
	int curId = 0, annoMaxLen = 0;
	QStringList::const_iterator it, endIt;

	isAnnotationAppended = isShowAnnotate && curAnn;

	if (isAnnotationAppended) {
		annoMaxLen = annotateLength(curAnn);
		it = curAnn->lines.constBegin();
		endIt = curAnn->lines.constEnd();
		curId = curAnn->annId;
	}
	listWidgetAnn->setFont(currentFont());

	QString tmp;
	tmp.fill('M', annoMaxLen + 1 + linesNumDigits + 2);
	int width = listWidgetAnn->fontMetrics().boundingRect(tmp).width();

	QStringList sl;
	QVector<int> curIdLines;
	for (int i = 0; i < linesNum; i++) {

		if (isAnnotationAppended) {
			if (it != endIt)
				tmp = (*(it++)).leftJustified(annoMaxLen);
			else
				tmp = QString().leftJustified(annoMaxLen);

			if (tmp.section('.',0 ,0).toInt() == curId)
				curIdLines.append(i);
		} else
			tmp.clear();

		tmp.append(QString(" %1 ").arg(i + 1, linesNumDigits));
		sl.append(tmp);
	}
        sl.append(QString());  // QTextEdit adds a blank line after content
	listWidgetAnn->setUpdatesEnabled(false);
	listWidgetAnn->clear();
	listWidgetAnn->addItems(sl);

	QAbstractTextDocumentLayout *layout = document()->documentLayout();
	if (layout != NULL) {
                qreal previousBottom = 0.;
		QTextBlock block = document()->begin();
                for (int i = 0; i < linesNum; i++) {
                        qreal bottom = layout->blockBoundingRect(block).bottom();
			QListWidgetItem* item = listWidgetAnn->item(i);
                        item->setSizeHint(QSize(0, static_cast<int>(bottom - previousBottom)));
                        item->setTextAlignment(Qt::AlignVCenter);  // Move down a pixel or so.

			previousBottom = bottom;
			block = block.next();
		}
	}

	QBrush fore(Qt::darkRed);
	QBrush back(Qt::lightGray);
	QFont f(listWidgetAnn->font());
	f.setBold(true);
	FOREACH (QVector<int>, it, curIdLines) {
		QListWidgetItem* item = listWidgetAnn->item(*it);
		item->setForeground(fore);
		item->setBackground(back);
		item->setFont(f);
	}
	/* When listWidgetAnn get focus for the first time the current
	   item, if not already present, is set to the first row and
	   scrolling starts from there, so set a proper current item here
	*/
	int topRow = lineAtTop() + 1;
	listWidgetAnn->setCurrentRow(topRow);
        listWidgetAnn->adjustSize(); // update scrollbar state
        adjustAnnListSize(width);
        listWidgetAnn->setUpdatesEnabled(true);
}

void FileContent::adjustAnnListSize(int width) {

        QRect r = listWidgetAnn->geometry();
        r.setWidth(width);
        int height = geometry().height();
        if (horizontalScrollBar()->isVisible()) height -= horizontalScrollBar()->height();
        r.setHeight(height);
        listWidgetAnn->setGeometry(r);
        setViewportMargins(width, 0, 0, 0); // move textedit view to the left of listWidgetAnn
}

void FileContent::resizeEvent(QResizeEvent* e) {

	QTextEdit::resizeEvent(e);
	int width = listWidgetAnn->geometry().width();
	adjustAnnListSize(width); // update list width
}
