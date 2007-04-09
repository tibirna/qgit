/*
	Description: patch viewer window

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSyntaxHighlighter>
#include <QScrollBar>
#include "common.h"
#include "git.h"
#include "myprocess.h"
#include "mainimpl.h"
#include "filelist.h" // TODO remove
#include "patchview.h"

class DiffHighlighter : public QSyntaxHighlighter {
public:
	DiffHighlighter(PatchView* p, QTextEdit* e) : QSyntaxHighlighter(e), pv(p), cl(0) {}
	void setCombinedLength(uint c) { cl = c; }
	virtual void highlightBlock(const QString& text) {

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

		if (pv->matches.count() > 0) {
			int indexFrom, indexTo;
			if (pv->getMatch(currentBlockState(), &indexFrom, &indexTo)) {

				QTextCharFormat fmt;
				fmt.setFont(document()->defaultFont()); // FIXME use currentFont()
				fmt.setFontWeight(QFont::Bold);
				fmt.setForeground(Qt::blue);
				if (indexTo == 0)
					indexTo = text.length();

				setFormat(indexFrom, indexTo - indexFrom, fmt);
			}
		}
	}
private:
	PatchView* pv;
	uint cl;
};

PatchView::PatchView(MainImpl* mi, Git* g) : Domain(mi, g, false) {

	seekTarget = diffLoaded = false;
	pickAxeRE.setMinimal(true);
	pickAxeRE.setCaseSensitivity(Qt::CaseInsensitive);

	container = new QWidget(NULL); // will be reparented to m()->tabWdg
	patchTab = new Ui_TabPatch();
	patchTab->setupUi(container);

	QButtonGroup* bg = new QButtonGroup(this);
	bg->addButton(patchTab->radioButtonParent, DIFF_TO_PARENT);
	bg->addButton(patchTab->radioButtonHead, DIFF_TO_HEAD);
	bg->addButton(patchTab->radioButtonSha, DIFF_TO_SHA);
	connect(bg, SIGNAL(buttonClicked(int)), this, SLOT(button_clicked(int)));

	m()->tabWdg->addTab(container, "&Patch");
	tabPosition = m()->tabWdg->count() - 1;

	patchTab->textEditDiff->setFont(QGit::TYPE_WRITER_FONT);
	patchTab->textBrowserDesc->setup(this);
	patchTab->fileList->setup(this, git);

	diffHighlighter = new DiffHighlighter(this, patchTab->textEditDiff);

	connect(patchTab->lineEditDiff, SIGNAL(returnPressed()),
	        this, SLOT(lineEditDiff_returnPressed()));

	connect(patchTab->fileList, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));
}

PatchView::~PatchView() {

	if (!parent())
		return;

	git->cancelProcess(proc);
	delete diffHighlighter;

	// remove before to delete, avoids a Qt warning in QInputContext()
	m()->tabWdg->removeTab(m()->tabWdg->indexOf(container));
	delete patchTab;
	delete container;
}

void PatchView::clear(bool complete) {

	if (complete) {
		st.clear();
		patchTab->textBrowserDesc->clear();
		patchTab->fileList->clear();
	}
	patchTab->textEditDiff->clear();
	matches.clear();
	diffLoaded = false;
	seekTarget = !target.isEmpty();
	partialParagraphs = "";
}

void PatchView::centerOnFileHeader(const QString& fileName) {

	if (st.fileName().isEmpty())
		return;

	target = fileName;
	bool combined = (st.isMerge() && !st.allMergeFiles());
	git->formatPatchFileHeader(&target, st.sha(), st.diffToSha(), combined, st.allMergeFiles());
	seekTarget = !target.isEmpty();
	if (seekTarget)
		centerTarget();
}

void PatchView::on_contextMenu(const QString& data, int type) {

	if (isLinked()) // skip if not linked to main view
		Domain::on_contextMenu(data, type);
}

void PatchView::centerTarget() {

	QTextEdit* te = patchTab->textEditDiff;
	te->moveCursor(QTextCursor::Start);

	// find() updates cursor position
	if (!te->find(target, QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords))
		return;

	// target found, remove selection
	seekTarget = false;
	QTextCursor tc = te->textCursor();
 	tc.clearSelection();
	te->setTextCursor(tc);

	int ps = te->verticalScrollBar()->pageStep();
	if (tc.position() > ps) { // find() places cursor at the end of the page
		int v = te->verticalScrollBar()->value();
		te->verticalScrollBar()->setValue(v + ps - te->fontMetrics().height());
	}
}

void PatchView::centerMatch(int id) {

	if (matches.count() <= id)
		return;
//FIXME
// 	patchTab->textEditDiff->setSelection(matches[id].paraFrom, matches[id].indexFrom,
// 	                                     matches[id].paraTo, matches[id].indexTo);
}

void PatchView::procReadyRead(const QByteArray& data) {

	QString txt(data);
	patchTab->textEditDiff->insertPlainText(txt);
	if (seekTarget && txt.contains(target))
		centerTarget();
}

void PatchView::procFinished() {

	diffLoaded = true;
	computeMatches();
	diffHighlighter->rehighlight();
	centerMatch();
}

int PatchView::doSearch(SCRef txt, int pos) {

	if (isRegExp)
		return txt.indexOf(pickAxeRE, pos);

	return txt.indexOf(pickAxeRE.pattern(), pos, Qt::CaseInsensitive);
}

void PatchView::computeMatches() {

	matches.clear();
	if (pickAxeRE.isEmpty())
		return;

	SCRef txt = patchTab->textEditDiff->toPlainText();
	int pos, lastPos = 0, lastPara = 0;

	// must be at the end to catch patterns across more the one chunk
	while ((pos = doSearch(txt, lastPos)) != -1) {

		matches.append(MatchSelection());
		MatchSelection& s = matches.last();

		s.paraFrom = txt.mid(lastPos, pos - lastPos).count('\n');
		s.paraFrom += lastPara;
		s.indexFrom = pos - txt.lastIndexOf('\n', pos) - 1; // index starts from 0

		lastPos = pos;
		pos += (isRegExp) ? pickAxeRE.matchedLength() : pickAxeRE.pattern().length();
		pos--;

		s.paraTo = s.paraFrom + txt.mid(lastPos, pos - lastPos).count('\n');
		s.indexTo = pos - txt.lastIndexOf('\n', pos) - 1;
		s.indexTo++; // in QTextEdit::setSelection() indexTo is not included

		lastPos = pos;
		lastPara = s.paraTo;
	}
}

bool PatchView::getMatch(int para, int* indexFrom, int* indexTo) {

	for (int i = 0; i < matches.count(); i++)
		if (matches[i].paraFrom <= para && matches[i].paraTo >= para) {

			*indexFrom = (para == matches[i].paraFrom) ? matches[i].indexFrom : 0;
			*indexTo = (para == matches[i].paraTo) ? matches[i].indexTo : 0;
			return true;
		}
	return false;
}

void PatchView::on_highlightPatch(const QString& exp, bool re) {

	pickAxeRE.setPattern(exp);
	isRegExp = re;
	if (diffLoaded)
		procFinished();
}

void PatchView::lineEditDiff_returnPressed() {

	if (patchTab->lineEditDiff->text().isEmpty())
		return;

	patchTab->radioButtonSha->setChecked(true); // could be called by code
	button_clicked(DIFF_TO_SHA);
}

void PatchView::button_clicked(int diffType) {

	QString sha;
	switch (diffType) {
	case DIFF_TO_PARENT:
		break;
	case DIFF_TO_HEAD:
		sha = "HEAD";
		break;
	case DIFF_TO_SHA:
		sha = patchTab->lineEditDiff->text();
		break;
	}
	if (sha == QGit::ZERO_SHA)
		return;

	// check for a ref name or an abbreviated form
	normalizedSha = (sha.length() != 40 && !sha.isEmpty()) ? git->getRefSha(sha) : sha;

	if (normalizedSha != st.diffToSha()) { // avoid looping
		st.setDiffToSha(normalizedSha); // could be empty
		UPDATE();
	}
}

void PatchView::on_updateRevDesc() {

	bool showHeader = m()->ActShowDescHeader->isChecked();
	SCRef d(git->getDesc(st.sha(), m()->shortLogRE, m()->longLogRE, showHeader));
	patchTab->textBrowserDesc->setHtml(d);
}

void PatchView::updatePatch() {

	git->cancelProcess(proc);
	clear(false); // only patch content

	bool combined = (st.isMerge() && !st.allMergeFiles());
	if (combined) {
		const Rev* r = git->revLookup(st.sha());
		if (r)
			diffHighlighter->setCombinedLength(r->parentsCount());
	} else
		diffHighlighter->setCombinedLength(0);

	if (normalizedSha != st.diffToSha()) { // note <(null)> != <(empty)>

		if (!st.diffToSha().isEmpty()) {
			patchTab->lineEditDiff->setText(st.diffToSha());
			lineEditDiff_returnPressed();

		} else if (!normalizedSha.isEmpty()) {
			normalizedSha = "";
			// we cannot uncheck radioButtonSha directly
			// because "Parent" button will stay off
			patchTab->radioButtonParent->toggle();
		}
	}
	proc = git->getDiff(st.sha(), this, st.diffToSha(), combined); // non blocking
}

bool PatchView::doUpdate(bool force) {

	const RevFile* files = NULL;
	bool newFiles = false;

	if (st.isChanged(StateInfo::SHA) || force) {

		if (!isLinked()) {
			QString caption(git->getShortLog(st.sha()));
			if (caption.length() > 30)
				caption = caption.left(30 - 3).trimmed().append("...");

			int idx = m()->tabWdg->indexOf(container);
			m()->tabWdg->setTabText(idx, caption);
		}
		on_updateRevDesc();
	}

	if (st.isChanged(StateInfo::ANY & ~StateInfo::FILE_NAME) || force) {

		updatePatch();
		patchTab->fileList->clear();
		files = git->getFiles(st.sha(), st.diffToSha(), st.allMergeFiles());
		newFiles = true;
	}
	// call always to allow a simple refresh
	patchTab->fileList->update(files, newFiles);

	if (st.isChanged() || force)
		centerOnFileHeader(st.fileName());

	return true;
}
