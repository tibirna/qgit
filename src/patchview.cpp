/*
	Description: patch viewer window

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <q3textedit.h>
#include <qlineedit.h>
#include <qapplication.h>
#include <q3syntaxhighlighter.h>
#include <qradiobutton.h>
#include <q3buttongroup.h>
#include <qtabwidget.h>
#include "common.h"
#include "git.h"
#include "domain.h"
#include "myprocess.h"
#include "mainimpl.h"
#include "revdesc.h"
#include "filelist.h"
#include "patchview.h"

class DiffHighlighter : public Q3SyntaxHighlighter {
public:
	DiffHighlighter(PatchView* p, Q3TextEdit* te) :
	                Q3SyntaxHighlighter(te), pv(p), combinedLenght(0) {}

	void setCombinedLength(uint cl) { combinedLenght = cl; }
	virtual int highlightParagraph (const QString& text, int) {

		QColor myColor;
		const char firstChar = text[0].latin1();
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
			else if (combinedLenght > 0 && text.startsWith("diff --combined"))
				myColor = Qt::darkBlue;
			break;
		case ' ':
			if (combinedLenght > 0) {
				if (text.left(combinedLenght).contains('+'))
					myColor = Qt::darkGreen;
				else if (text.left(combinedLenght).contains('-'))
					myColor = Qt::red;
			}
			break;
		}
		if (myColor.isValid())
			setFormat(0, text.length(), myColor);

		if (pv->matches.count() > 0) {
			int indexFrom, indexTo;
			if (pv->getMatch(currentParagraph(), &indexFrom, &indexTo)) {

				QFont f = textEdit()->currentFont();
				f.setUnderline(true);
				f.setBold(true);
				if (indexTo == 0)
					indexTo = text.length();

				setFormat(indexFrom, indexTo - indexFrom, f, Qt::blue);
			}
		}
		return 0;
	}
private:
	PatchView* pv;
	uint combinedLenght;
};

PatchView::PatchView(MainImpl* mi, Git* g) : Domain(mi, g) {

	seekTarget = diffLoaded = false;
	pickAxeRE.setMinimal(true);
	pickAxeRE.setCaseSensitive(false);

	container = new QWidget(NULL); // will be reparented to m()->tabWdg
	patchTab = new Ui_TabPatch();
	patchTab->setupUi(container);

	m()->tabWdg->addTab(container, "&Patch");
	tabPosition = m()->tabWdg->count() - 1;

	patchTab->textEditDiff->setFont(QGit::TYPE_WRITER_FONT);
	patchTab->textBrowserDesc->setDomain(this);

	listBoxFiles = new ListBoxFiles(this, git, patchTab->listBoxFiles);
	diffHighlighter = new DiffHighlighter(this, patchTab->textEditDiff);

	connect(patchTab->lineEditDiff, SIGNAL(returnPressed()),
	        this, SLOT(lineEditDiff_returnPressed()));

	connect(patchTab->buttonGroupDiff, SIGNAL(clicked(int)),
	        this, SLOT(buttonGroupDiff_clicked(int)));

	connect(listBoxFiles, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));
}

PatchView::~PatchView() {

	if (!parent())
		return;

	git->cancelProcess(proc);
	delete diffHighlighter;
	delete listBoxFiles;

	// remove before to delete, avoids a Qt warning in QInputContext()
	m()->tabWdg->removePage(container);
	delete patchTab;
	delete container;
}

void PatchView::clear(bool complete) {

	if (complete) {
		st.clear();
		patchTab->textBrowserDesc->clear();
		listBoxFiles->clear();
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

	patchTab->textEditDiff->setCursorPosition(0, 0);
	if (!patchTab->textEditDiff->find(target, true, true)) // updates cursor position
		return;

	// target found
	seekTarget = false;
	int para, index;
	patchTab->textEditDiff->getCursorPosition(&para, &index);
	QPoint p = patchTab->textEditDiff->paragraphRect(para).topLeft();
	patchTab->textEditDiff->setContentsPos(p.x(), p.y());
	patchTab->textEditDiff->removeSelection();
}

void PatchView::centerMatch(int id) {

	if (matches.count() <= id)
		return;

	patchTab->textEditDiff->setSelection(matches[id].paraFrom, matches[id].indexFrom,
	                                     matches[id].paraTo, matches[id].indexTo);
}

void PatchView::procReadyRead(const QString& data) {

	int X = patchTab->textEditDiff->contentsX();
	int Y = patchTab->textEditDiff->contentsY();

	bool targetInNewChunk = false;
	if (seekTarget)
		targetInNewChunk = (data.find(target) != -1);

	// QTextEdit::append() adds a new paragraph, i.e. inserts a LF
	// if not already present. For performance reasons we cannot use
	// QTextEdit::text() + QString::append() + QTextEdit::setText()
	// so we append only \n terminating text
	//
	// NOTE: last char of diff data MUST always be '\n' for this to work
	QString newPara;
	if (!QGit::stripPartialParaghraps(data, &newPara, &partialParagraphs))
		return;

	patchTab->textEditDiff->append(newPara);

	if (targetInNewChunk)
		centerTarget();
	else {
		patchTab->textEditDiff->setContentsPos(X, Y);
		patchTab->textEditDiff->sync();
	}
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

	SCRef txt = patchTab->textEditDiff->text();
	int pos, lastPos = 0, lastPara = 0;

	// must be at the end to catch patterns across more the one chunk
	while ((pos = doSearch(txt, lastPos)) != -1) {

		matches.append(MatchSelection());
		MatchSelection& s = matches.last();

		s.paraFrom = txt.mid(lastPos, pos - lastPos).count('\n');
		s.paraFrom += lastPara;
		s.indexFrom = pos - txt.findRev('\n', pos) - 1; // index starts from 0

		lastPos = pos;
		pos += (isRegExp) ? pickAxeRE.matchedLength() : pickAxeRE.pattern().length();
		pos--;

		s.paraTo = s.paraFrom + txt.mid(lastPos, pos - lastPos).count('\n');
		s.indexTo = pos - txt.findRev('\n', pos) - 1;
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
	buttonGroupDiff_clicked(DIFF_TO_SHA);
}

void PatchView::buttonGroupDiff_clicked(int diffType) {

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

	SCRef d(git->getDesc(st.sha(), m()->shortLogRE, m()->longLogRE));
	patchTab->textBrowserDesc->setText(d);
// 	patchTab->textBrowserDesc->setCursorPosition(0, 0); FIXME
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
			patchTab->radioButtonSha->group()->buttons()[0]->toggle();
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
				caption = caption.left(30 - 3).stripWhiteSpace().append("...");

			m()->tabWdg->changeTab(container, caption);
		}
		on_updateRevDesc();
	}

	if (st.isChanged(StateInfo::ANY & ~StateInfo::FILE_NAME) || force) {

		updatePatch();
		files = git->getFiles(st.sha(), st.diffToSha(), st.allMergeFiles());
		newFiles = true;
	}
	// call always to allow a simple refresh
	listBoxFiles->update(files, newFiles);

	if (st.isChanged() || force)
		centerOnFileHeader(st.fileName());

	return true;
}
