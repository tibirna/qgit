/*
	Description: patch viewer window

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QButtonGroup>
#include <QScrollBar>
#include "common.h"
#include "git.h"
#include "mainimpl.h"
#include "patchcontent.h"
#include "patchview.h"

PatchView::PatchView(MainImpl* mi, Git* g) : Domain(mi, g, false) {

	patchTab = new Ui_TabPatch();
	patchTab->setupUi(container);
	SCRef ic(QString::fromUtf8(":/icons/resources/plusminus.svg"));
	patchTab->buttonFilterPatch->setIcon(QIcon(ic));

	QButtonGroup* bg = new QButtonGroup(this);
	bg->addButton(patchTab->radioButtonParent, DIFF_TO_PARENT);
	bg->addButton(patchTab->radioButtonHead, DIFF_TO_HEAD);
	bg->addButton(patchTab->radioButtonSha, DIFF_TO_SHA);
	connect(bg, SIGNAL(buttonClicked(int)), this, SLOT(button_clicked(int)));

	patchTab->textBrowserDesc->setup(this);
	patchTab->textEditDiff->setup(this, git);
	patchTab->fileList->setup(this, git);

	connect(m(), SIGNAL(typeWriterFontChanged()),
	        patchTab->textEditDiff, SLOT(typeWriterFontChanged()));

	connect(m(), SIGNAL(changeFont(const QFont&)),
	       patchTab->fileList, SLOT(on_changeFont(const QFont&)));

	connect(patchTab->lineEditDiff, SIGNAL(returnPressed()),
	        this, SLOT(lineEditDiff_returnPressed()));

	connect(patchTab->fileList, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(patchTab->buttonFilterPatch, SIGNAL(clicked()),
	        this, SLOT(buttonFilterPatch_clicked()));
}

PatchView::~PatchView() {

	if (!parent())
		return;

	clear(); // to cancel any data loading
	delete patchTab;
}

void PatchView::clear(bool complete) {

	if (complete) {
		st.clear();
		patchTab->textBrowserDesc->clear();
		patchTab->fileList->clear();
	}
	patchTab->textEditDiff->clear();
}

void PatchView::buttonFilterPatch_clicked() {

	QString ic;
	PatchContent* pc = patchTab->textEditDiff;
	pc->prevFilter = pc->curFilter;
	if (pc->curFilter == PatchContent::VIEW_ALL) {
		pc->curFilter = PatchContent::VIEW_ADDED;
		ic = QString::fromUtf8(":/icons/resources/plusonly.svg");

	} else if (pc->curFilter == PatchContent::VIEW_ADDED) {
		pc->curFilter = PatchContent::VIEW_REMOVED;
		ic = QString::fromUtf8(":/icons/resources/minusonly.svg");

	} else if (pc->curFilter == PatchContent::VIEW_REMOVED) {
		pc->curFilter = PatchContent::VIEW_ALL;
		ic = QString::fromUtf8(":/icons/resources/plusminus.svg");
	}
	patchTab->buttonFilterPatch->setIcon(QIcon(ic));
	patchTab->textEditDiff->refresh();
}

void PatchView::on_contextMenu(const QString& data, int type) {

	if (isLinked()) // skip if not linked to main view
		Domain::on_contextMenu(data, type);
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
	normalizedSha = (sha.length() != 40 && !sha.isEmpty() ? git->getRefSha(sha) : sha);

	if (normalizedSha != st.diffToSha()) { // avoid looping
		st.setDiffToSha(normalizedSha); // could be empty
		UPDATE();
	}
}

void PatchView::on_updateRevDesc() {

	SCRef d = m()->getRevisionDesc(st.sha());
	patchTab->textBrowserDesc->setHtml(d);
}

void PatchView::updatePatch() {

	PatchContent* pc = patchTab->textEditDiff;
	pc->clear();

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
	pc->update(st); // non blocking
}

bool PatchView::doUpdate(bool force) {

	const RevFile* files = NULL;
	bool newFiles = false;

	if (st.isChanged(StateInfo::SHA) || force) {

		if (!isLinked()) {
			QString caption(git->getShortLog(st.sha()));
			if (caption.length() > 30)
				caption = caption.left(30 - 3).trimmed().append("...");

			setTabCaption(caption);
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
		patchTab->textEditDiff->centerOnFileHeader(st);

	return true;
}
