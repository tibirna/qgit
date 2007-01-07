/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qlineedit.h>
#include <qpainter.h>
#include <qstringlist.h>
#include <qapplication.h>
#include <qmessagebox.h>
#include <qstatusbar.h>
#include <qcursor.h>
#include <qaction.h>
#include <qtabwidget.h>
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

	container = new QWidget(NULL); // will be reparented to m()->tabWdg
	revTab = new Ui_TabRev();
	revTab->setupUi(container);

	m()->tabWdg->addTab(container, "&Rev list");
	tabPosition = m()->tabWdg->count() - 1;

	tab()->listViewLog->setup(this, g);
	tab()->textBrowserDesc->setup(this);
	tab()->fileList->setup(this, git);
	treeView = new TreeView(this, git, m()->treeView);

	connect(git, SIGNAL(loadCompleted(const FileHistory*, const QString&)),
	        this, SLOT(on_loadCompleted(const FileHistory*, const QString&)));

	connect(m(), SIGNAL(repaintListViews(const QFont&)),
	        tab()->listViewLog, SLOT(on_repaintListViews(const QFont&)));

	connect(m(), SIGNAL(updateRevDesc()), this, SLOT(on_updateRevDesc()));

	connect(tab()->listViewLog, SIGNAL(lanesContextMenuRequested(const QStringList&,
	        const QStringList&)), this, SLOT(on_lanesContextMenuRequested
	       (const QStringList&, const QStringList&)));

	connect(tab()->listViewLog, SIGNAL(droppedRevisions(const QStringList&)),
	        this, SLOT(on_droppedRevisions(const QStringList&)));

	connect(tab()->listViewLog, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(treeView, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(tab()->fileList, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));
}

RevsView::~RevsView() {

	if (!parent())
		return;

	m()->tabWdg->removePage(container);
	delete linkedPatchView;
	delete revTab;
	delete container;
}

void RevsView::clear(bool complete) {

	Domain::clear(complete);

	tab()->textBrowserDesc->clear();
	tab()->fileList->clear();
	treeView->clear();
	updateLineEditSHA(true);
	if (linkedPatchView)
		linkedPatchView->clear();
}

void RevsView::setEnabled(bool b) {

	container->setEnabled(b);
 	if (linkedPatchView)
 		linkedPatchView->tabContainer()->setEnabled(b);
}

void RevsView::viewPatch(bool newTab) {

	if (!newTab && linkedPatchView) {
		m()->tabWdg->setCurrentPage(linkedPatchView->tabPos());
		return;
	}
	PatchView* pv = new PatchView(m(), git);
	m()->tabWdg->setCurrentPage(pv->tabPos());

	if (!newTab) { // linkedPatchView == NULL
		linkedPatchView = pv;
		linkDomain(linkedPatchView);

		connect(m(), SIGNAL(highlightPatch(const QString&, bool)),
			linkedPatchView, SLOT(on_highlightPatch(const QString&, bool)));

		connect(linkedPatchView->tab()->fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
			m(), SLOT(fileList_itemDoubleClicked(QListWidgetItem*)));

		connect(m(), SIGNAL(updateRevDesc()), pv, SLOT(on_updateRevDesc()));
	}
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

	SCRef d(git->getDesc(st.sha(), m()->shortLogRE, m()->longLogRE));
	tab()->textBrowserDesc->setHtml(d);
}

bool RevsView::doUpdate(bool force) {

	force = force || m()->lineEditSHA->text().isEmpty();

	bool found = tab()->listViewLog->update();

	if (!found && !st.sha().isEmpty()) {

		const QString tmp("Sorry, revision " + st.sha() +
		                  " has not been found in main view");
		m()->statusBar()->message(tmp);

	} else { // sha could be NULL

		if (st.isChanged(StateInfo::SHA) || force) {

			updateLineEditSHA();
			on_updateRevDesc();
			m()->statusBar()->message(git->getRevInfo(st.sha()));
		}
		const RevFile* files = NULL;
		bool newFiles = false;

		if (st.isChanged(StateInfo::ANY & ~StateInfo::FILE_NAME) || force) {

			files = git->getFiles(st.sha(), st.diffToSha(), st.allMergeFiles());
			newFiles = true;
		}
		// call always to allow a simple refresh
		tab()->fileList->update(files, newFiles);

		// update the tree at startup or when releasing a no-match toolbar search
		if (m()->treeView->isVisible() || st.sha(false).isEmpty())
			treeView->update(); // blocking call

		if (st.selectItem()) {
			bool isDir = treeView->isDir(st.fileName());
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

	Q3PopupMenu contextMenu;
	uint i = 0;
	FOREACH_SL (it, childs)
		contextMenu.insertItem("Child: " + git->getShortLog(*it), i++);

	FOREACH_SL (it, parents) {

		QString log(git->getShortLog(*it));
		if (log.isEmpty())
			log = *it;

		contextMenu.insertItem("Parent: " + log, i++);
	}
	int id = contextMenu.exec(QCursor::pos()); // modal exec
	if (id == -1)
		return;

	int cc = (int)childs.count();
	SCRef target(id < cc ? childs[id] : parents[id - cc]);
	st.setSha(target);
	UPDATE();
}

void RevsView::on_droppedRevisions(SCList remoteRevs) {
// remoteRevs is already sanity checked to contain some possible valid data

	if (isDropping()) // avoid reentrancy
		return;

	QDir dr(m()->curDir + QGit::PATCHES_DIR);
	if (dr.exists()) {
		const QString tmp("Please remove stale import directory " + dr.absPath());
		m()->statusBar()->message(tmp);
		return;
	}
	bool commit, fold;
	if (!m()->askApplyPatchParameters(&commit, &fold))
		return;

	// ok, let's go
	setDropping(true);
	dr.setFilter(QDir::Files);
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	m()->raise();
	EM_PROCESS_EVENTS;

	uint revNum = 0;
	QStringList::const_iterator it(remoteRevs.constEnd());
	do {
		--it;

		QString tmp("Importing revision %1 of %2");
		m()->statusBar()->message(tmp.arg(++revNum).arg(remoteRevs.count()));

		SCRef sha((*it).section('@', 0, 0));
		SCRef remoteRepo((*it).section('@', 1));

		if (!dr.exists(remoteRepo))
			break;

		// we create patches one by one
		if (!git->formatPatch(QStringList(sha), dr.absPath(), remoteRepo))
			break;

		dr.refresh();
		if (dr.count() != 1) {
			qDebug("ASSERT in on_droppedRevisions: found %i files "
			       "in %s", dr.count(), QGit::PATCHES_DIR.latin1());
			break;
		}
		SCRef fn(dr.absFilePath(dr[0]));
		if (!git->applyPatchFile(fn, commit, fold, Git::optDragDrop))
			break;

		dr.remove(fn);

	} while (it != remoteRevs.constBegin());

	if (it == remoteRevs.constBegin())
		m()->statusBar()->clear();
	else
		m()->statusBar()->message("Failed to import revision " + QString::number(revNum--));

	if (!commit && (revNum > 0))
		git->resetCommits(revNum);

	dr.rmdir(dr.absPath()); // 'dr' must be already empty
	QApplication::restoreOverrideCursor();
	setDropping(false);
	m()->refreshRepo();
}
