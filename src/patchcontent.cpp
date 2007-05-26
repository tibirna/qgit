/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QScrollBar>
#include <QTextCharFormat>
#include "common.h"
#include "domain.h"
#include "git.h"
#include "myprocess.h"
#include "patchcontent.h"

void DiffHighlighter::highlightBlock(const QString& text) {

	// state is used to count paragraphs, starting from 0
	setCurrentBlockState(previousBlockState() + 1);
	if (text.isEmpty())
		return;

	QColor myColor;
	const char firstChar = text.at(0).toLatin1();
	switch (firstChar) {
	case '@':
		myColor = Qt::darkMagenta;
		break;
	case '+':
		myColor = Qt::darkGreen;
		break;
	case '-':
		myColor = Qt::red;
		break;
	case 'c':
	case 'd':
	case 'i':
	case 'n':
	case 'o':
	case 'r':
	case 's':
		if (   text.startsWith("diff --git a/")
			|| text.startsWith("copy ")
			|| text.startsWith("index ")
			|| text.startsWith("new ")
			|| text.startsWith("old ")
			|| text.startsWith("rename ")
			|| text.startsWith("similarity "))
			myColor = Qt::darkBlue;

		else if (cl > 0 && text.startsWith("diff --combined"))
			myColor = Qt::darkBlue;
		break;
	case ' ':
		if (cl > 0) {
			if (text.left(cl).contains('+'))
				myColor = Qt::darkGreen;
			else if (text.left(cl).contains('-'))
				myColor = Qt::red;
		}
		break;
	}
	if (myColor.isValid())
		setFormat(0, text.length(), myColor);

	PatchContent* pc = static_cast<PatchContent*>(parent());
	if (pc->matches.count() > 0) {
		int indexFrom, indexTo;
		if (pc->getMatch(currentBlockState(), &indexFrom, &indexTo)) {

			QTextEdit* te = dynamic_cast<QTextEdit*>(parent());
			QTextCharFormat fmt;
			fmt.setFont(te->currentFont());
			fmt.setFontWeight(QFont::Bold);
			fmt.setForeground(Qt::blue);
			if (indexTo == 0)
				indexTo = text.length();

			setFormat(indexFrom, indexTo - indexFrom, fmt);
		}
	}
}

PatchContent::PatchContent(QWidget* parent) : QTextEdit(parent) {

	diffLoaded = seekTarget = false;
	curFilter = prevFilter = VIEW_ALL;

	pickAxeRE.setMinimal(true);
	pickAxeRE.setCaseSensitivity(Qt::CaseInsensitive);

	setFont(QGit::TYPE_WRITER_FONT);
	diffHighlighter = new DiffHighlighter(this);
}

void PatchContent::setup(Domain*, Git* g) {

	git = g;
}

void PatchContent::clear() {

	git->cancelProcess(proc);
	QTextEdit::clear();
	patchRowData.clear();
	halfLine = "";
	matches.clear();
	diffLoaded = false;
	seekTarget = !target.isEmpty();
}

void PatchContent::refresh() {

	int topPara = topToLineNum();
	setUpdatesEnabled(false);
	QByteArray tmp(patchRowData);
	clear();
	patchRowData = tmp;
	processData(patchRowData, &topPara);
	scrollLineToTop(topPara);
	setUpdatesEnabled(true);
}

void PatchContent::scrollCursorToTop() {

	QRect r = cursorRect();
	QScrollBar* vsb = verticalScrollBar();
	vsb->setValue(vsb->value() + r.top());
}

void PatchContent::scrollLineToTop(int lineNum) {

	QTextCursor tc = textCursor();
	tc.movePosition(QTextCursor::Start);
	tc.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineNum);
	setTextCursor(tc);
	scrollCursorToTop();
}

int PatchContent::positionToLineNum(int pos) {

	QTextCursor tc = textCursor();
	tc.setPosition(pos);
	return tc.blockNumber();
}

int PatchContent::topToLineNum() {

	return cursorForPosition(QPoint(1, 1)).blockNumber();
}

bool PatchContent::centerTarget(SCRef target) {

	moveCursor(QTextCursor::Start);

	// find() updates cursor position
	if (!find(target, QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords))
		return false;

	// target found, remove selection
	QTextCursor tc = textCursor();
	tc.clearSelection();
	setTextCursor(tc);

	int ps = verticalScrollBar()->pageStep();
	if (tc.position() > ps) { // find() places cursor at the end of the page
		int v = verticalScrollBar()->value();
		verticalScrollBar()->setValue(v + ps - fontMetrics().height());
	}
	return true;
}

void PatchContent::centerOnFileHeader(StateInfo& st) {

	if (st.fileName().isEmpty())
		return;

	target = st.fileName();
	bool combined = (st.isMerge() && !st.allMergeFiles());
	git->formatPatchFileHeader(&target, st.sha(), st.diffToSha(), combined, st.allMergeFiles());
	seekTarget = !target.isEmpty();
	if (seekTarget)
		seekTarget = !centerTarget(target);
}

void PatchContent::centerMatch(int id) {

	if (matches.count() <= id)
		return;
//FIXME
//	patchTab->textEditDiff->setSelection(matches[id].paraFrom, matches[id].indexFrom,
//	                                     matches[id].paraTo, matches[id].indexTo);
}

void PatchContent::procReadyRead(const QByteArray& data) {

	patchRowData.append(data);
	processData(data);
}

void PatchContent::typeWriterFontChanged() {

	setFont(QGit::TYPE_WRITER_FONT);
	setPlainText(toPlainText());
}

void PatchContent::processData(const QByteArray& fileChunk, int* prevLineNum) {

	QString newLines;
	if (!QGit::stripPartialParaghraps(fileChunk, &newLines, &halfLine))
		return;

	if (!prevLineNum && curFilter == VIEW_ALL)
		goto skip_filter; // optimize common case

	{ // scoped code because of goto

	QString filteredLines;
	int notNegCnt = 0, notPosCnt = 0;
	QVector<int> toAdded(1), toRemoved(1); // lines count from 1

	// prevLineNum will be set to the number of corresponding
	// line in full patch. Number is negative just for algorithm
	// reasons, prevLineNum counts lines from 1
	if (prevLineNum && prevFilter == VIEW_ALL)
		*prevLineNum = -(*prevLineNum); // set once

	const QStringList sl(newLines.split('\n', QString::KeepEmptyParts));
	FOREACH_SL (it, sl) {

		// do not remove diff header because of centerTarget
		bool n = (*it).startsWith('-') && !(*it).startsWith("---");
		bool p = (*it).startsWith('+') && !(*it).startsWith("+++");

		if (!p)
			notPosCnt++;
		if (!n)
			notNegCnt++;

		toAdded.append(notNegCnt);
		toRemoved.append(notPosCnt);

		int curLineNum = toAdded.count() - 1;

		bool toRemove = (n && curFilter == VIEW_ADDED) || (p && curFilter == VIEW_REMOVED);
		if (!toRemove)
			filteredLines.append(*it).append('\n');

		if (prevLineNum && *prevLineNum == notNegCnt && prevFilter == VIEW_ADDED)
			*prevLineNum = -curLineNum; // set once

		if (prevLineNum && *prevLineNum == notPosCnt && prevFilter == VIEW_REMOVED)
			*prevLineNum = -curLineNum; // set once
	}
	if (prevLineNum && *prevLineNum <= 0) {
		if (curFilter == VIEW_ALL)
			*prevLineNum = -(*prevLineNum);

		else if (curFilter == VIEW_ADDED)
			*prevLineNum = toAdded.at(-(*prevLineNum));

		else if (curFilter == VIEW_REMOVED)
			*prevLineNum = toRemoved.at(-(*prevLineNum));

		if (*prevLineNum < 0)
			*prevLineNum = 0;
	}
	newLines = filteredLines;

	} // end of scoped code

skip_filter:

	setUpdatesEnabled(false);

	if (prevLineNum || document()->isEmpty()) { // use the faster setPlainText()

		setPlainText(newLines);
		moveCursor(QTextCursor::Start);
	} else {
		QTextCursor tc(cursorForPosition(QPoint(1, 1)));
		append(newLines);
		setTextCursor(tc);
	}
	QScrollBar* vsb = verticalScrollBar();
	vsb->setValue(vsb->value() + cursorRect().top());
	setUpdatesEnabled(true);
}

void PatchContent::procFinished() {

	if (!patchRowData.endsWith("\n"))
		processData("\n"); // flush pending half lines

// 	if (seekTarget) FIXME
// 		seekTarget = !centerTarget();

	diffLoaded = true;
	if (computeMatches()) {
		diffHighlighter->rehighlight(); // slow on big data
		centerMatch();
	}
}

int PatchContent::doSearch(SCRef txt, int pos) {

	if (isRegExp)
		return txt.indexOf(pickAxeRE, pos);

	return txt.indexOf(pickAxeRE.pattern(), pos, Qt::CaseInsensitive);
}

bool PatchContent::computeMatches() {

	matches.clear();
	if (pickAxeRE.isEmpty())
		return false;

	SCRef txt = toPlainText();
	int pos, lastPos = 0, lastPara = 0;

	// must be at the end to catch patterns across more the one chunk
	while ((pos = doSearch(txt, lastPos)) != -1) {

		matches.append(MatchSelection());
		MatchSelection& s = matches.last();

		s.paraFrom = txt.mid(lastPos, pos - lastPos).count('\n');
		s.paraFrom += lastPara;
		s.indexFrom = pos - txt.lastIndexOf('\n', pos) - 1; // index starts from 0

		lastPos = pos;
		pos += (isRegExp ? pickAxeRE.matchedLength() : pickAxeRE.pattern().length());
		pos--;

		s.paraTo = s.paraFrom + txt.mid(lastPos, pos - lastPos).count('\n');
		s.indexTo = pos - txt.lastIndexOf('\n', pos) - 1;
		s.indexTo++; // in QTextEdit::setSelection() indexTo is not included

		lastPos = pos;
		lastPara = s.paraTo;
	}
	return !matches.isEmpty();
}

bool PatchContent::getMatch(int para, int* indexFrom, int* indexTo) {

	for (int i = 0; i < matches.count(); i++)
		if (matches[i].paraFrom <= para && matches[i].paraTo >= para) {

			*indexFrom = (para == matches[i].paraFrom ? matches[i].indexFrom : 0);
			*indexTo = (para == matches[i].paraTo ? matches[i].indexTo : 0);
			return true;
		}
	return false;
}

void PatchContent::on_highlightPatch(const QString& exp, bool re) {

	pickAxeRE.setPattern(exp);
	isRegExp = re;
	if (diffLoaded)
		procFinished();
}

void PatchContent::update(StateInfo& st) {

	bool combined = (st.isMerge() && !st.allMergeFiles());
	if (combined) {
		const Rev* r = git->revLookup(st.sha());
		if (r)
			diffHighlighter->setCombinedLength(r->parentsCount());
	} else
		diffHighlighter->setCombinedLength(0);

	clear();
	proc = git->getDiff(st.sha(), this, st.diffToSha(), combined); // non blocking
}
