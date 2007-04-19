/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QMenu>
#include "common.h"
#include "git.h"
#include "domain.h"
#include "treeview.h"
#include "listview.h"
#include "filelist.h"
#include "revdesc.h"
#include "patchview.h"
#include "mainimpl.h"
#include "revsview.h"

RevsView::RevsView(MainImpl* mi, Git* g, bool isMain) : Domain(mi, g, isMain) {

	revTab = new Ui_TabRev();
	revTab->setupUi(container);

	tab()->listViewLog->setup(this, g);
	tab()->textBrowserDesc->setup(this);
	tab()->fileList->setup(this, git);
	m()->treeView->setup(this, git);

	connect(git, SIGNAL(loadCompleted(const FileHistory*, const QString&)),
	        this, SLOT(on_loadCompleted(const FileHistory*, const QString&)));

	connect(m(), SIGNAL(repaintListViews(const QFont&)),
	        tab()->listViewLog, SLOT(on_repaintListViews(const QFont&)));

	connect(m(), SIGNAL(updateRevDesc()), this, SLOT(on_updateRevDesc()));

	connect(tab()->listViewLog, SIGNAL(lanesContextMenuRequested(const QStringList&,
	        const QStringList&)), this, SLOT(on_lanesContextMenuRequested
	       (const QStringList&, const QStringList&)));

	connect(tab()->listViewLog, SIGNAL(revisionsDragged(const QStringList&)),
	        m(), SLOT(revisionsDragged(const QStringList&)));

	connect(tab()->listViewLog, SIGNAL(revisionsDropped(const QStringList&)),
	        m(), SLOT(revisionsDropped(const QStringList&)));

	connect(tab()->listViewLog, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(m()->treeView, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(tab()->fileList, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));
}

RevsView::~RevsView() {

	if (!parent())
		return;

	delete linkedPatchView;
	delete revTab;
}

void RevsView::clear(bool complete) {

	Domain::clear(complete);

	tab()->textBrowserDesc->clear();
	tab()->fileList->clear();
	m()->treeView->clear();
	updateLineEditSHA(true);
	if (linkedPatchView)
		linkedPatchView->clear();
}

void RevsView::setEnabled(bool b) {

	container->setEnabled(b);
 	if (linkedPatchView)
 		linkedPatchView->tabPage()->setEnabled(b);
}

void RevsView::viewPatch(bool newTab) {

	if (!newTab && linkedPatchView) {
		m()->tabWdg->setCurrentWidget(linkedPatchView->tabPage());
		return;
	}
	PatchView* pv = new PatchView(m(), git);
	m()->tabWdg->addTab(pv->tabPage(), "&Patch");
	m()->tabWdg->setCurrentWidget(pv->tabPage());

	if (!newTab) { // linkedPatchView == NULL
		linkedPatchView = pv;
		linkDomain(linkedPatchView);

		connect(m(), SIGNAL(highlightPatch(const QString&, bool)),
			linkedPatchView, SLOT(on_highlightPatch(const QString&, bool)));

		connect(linkedPatchView->tab()->fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
			m(), SLOT(fileList_itemDoubleClicked(QListWidgetItem*)));
	}
	connect(m(), SIGNAL(updateRevDesc()), pv, SLOT(on_updateRevDesc()));
	connect(m(), SIGNAL(closeAllTabs()), pv, SLOT(on_closeAllTabs()));
	pv->st = st;
	UPDATE_DM_MASTER(pv, false);
}

void RevsView::on_loadCompleted(const FileHistory* fh, const QString& stats) {

	if (!git->isMainHistory(fh))
		return;

	if (st.sha().isEmpty()) { // point to first one in list

		if (fh->rowCount() > 0) {
			st.setSha(fh->sha(0));
			st.setSelectItem(true);
		}
	}
	UPDATE();
	QApplication::postEvent(this, new MessageEvent(stats));
}

void RevsView::on_updateRevDesc() {

	SCRef d = m()->getRevisionDesc(st.sha());
	tab()->textBrowserDesc->setHtml(d);
}

bool RevsView::doUpdate(bool force) {

	force = force || m()->lineEditSHA->text().isEmpty();

	bool found = tab()->listViewLog->update();

	if (!found && !st.sha().isEmpty()) {

		const QString tmp("Sorry, revision " + st.sha() +
		                  " has not been found in main view");
		showStatusBarMessage(tmp);

	} else { // sha could be NULL

		if (st.isChanged(StateInfo::SHA) || force) {

			updateLineEditSHA();
			on_updateRevDesc();
			showStatusBarMessage(git->getRevInfo(st.sha()));
		}
		const RevFile* files = NULL;
		bool newFiles = false;

		if (st.isChanged(StateInfo::ANY & ~StateInfo::FILE_NAME) || force) {

			tab()->fileList->clear();
			files = git->getFiles(st.sha(), st.diffToSha(), st.allMergeFiles());
			newFiles = true;
		}
		// call always to allow a simple refresh
		tab()->fileList->update(files, newFiles);

		// update the tree at startup or when releasing a no-match toolbar search
		if (m()->treeView->isVisible() || st.sha(false).isEmpty())
			m()->treeView->updateTree(); // blocking call

		if (st.selectItem()) {
			bool isDir = m()->treeView->isDir(st.fileName());
			m()->updateContextActions(st.sha(), st.fileName(), isDir, found);
		}
		// at the end update diffs that is the slowest and must be
		// run after update of file list for 'diff to sha' to work
		if (linkedPatchView) {
			linkedPatchView->st = st;
			UPDATE_DM_MASTER(linkedPatchView, force); // async call
		}
	}
// 	Q3ListViewItem* item = tab()->listViewLog->currentItem(); FIXME
// 	if (item && item->isVisible() && !found && force) {
// 		// we are in an inconsistent state: list view current item is
// 		// not selected and secondary panes are empty.
// 		// This could happen as example after removing a tree filter.
// 		// At least populate secondary panes
// 		st.setSha(((ListViewItem*)item)->sha());
// 		st.setSelectItem(false);
// 		UpdateDomainEvent* e = new UpdateDomainEvent(false);
// 		this->event(e); // will be queued immediately
// 		delete e;
// 	}
	return (found || st.sha().isEmpty());
}

void RevsView::updateLineEditSHA(bool clear) {

	QLineEdit* l = m()->lineEditSHA;

	if (clear)
		l->setText(""); // clears history

	else if (l->text() != st.sha()) {

		if (l->text().isEmpty())
			l->setText(st.sha()); // first rev clears history
		else {
			// setText() clears undo/redo history so
			// we use clear() + insert() instead
			l->clear();
			l->insert(st.sha());
		}
	}
	m()->ActBack->setEnabled(l->isUndoAvailable());
	m()->ActForward->setEnabled(l->isRedoAvailable());
}

void RevsView::on_lanesContextMenuRequested(SCList parents, SCList childs) {

	QMenu contextMenu;
	FOREACH_SL (it, childs)
		contextMenu.addAction("Child: " + git->getShortLog(*it));

	FOREACH_SL (it, parents) {
		QString log(git->getShortLog(*it));
		contextMenu.addAction("Parent: " + (log.isEmpty() ? *it : log));
	}
	QAction* act = contextMenu.exec(QCursor::pos()); // modal exec
	if (!act)
		return;

	int cc = childs.count();
	int id = contextMenu.actions().indexOf(act);
	SCRef target(id < cc ? childs[id] : parents[id - cc]);
	st.setSha(target);
	UPDATE();
}
