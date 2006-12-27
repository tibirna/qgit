/*
	Description: files tree view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qapplication.h>
#include <qcursor.h>
#include <qpainter.h>
#include <qaction.h>
#include <qlabel.h>
//Added by qt3to4:
#include <QPixmap>
#include "git.h"
#include "domain.h"
#include "mainimpl.h"
#include "treeview.h"

using namespace QGit;

QString FileItem::fullName() const {

	Q3ListViewItem* p = parent();
	QString s(p ? text(0) : ""); // root directory has no fullName
	while (p && p->parent()) {
		s.prepend(p->text(0) + '/');
		p = p->parent();
	}
	return s;
}

void FileItem::paintCell(QPainter* p, const QColorGroup& cg, int col, int wdt, int ali) {

	p->save();
	if (isModified) {
		QFont f(p->font());
		f.setBold(true);
		p->setFont(f);
	}
	Q3ListViewItem::paintCell(p, cg, col, wdt, ali);
	p->restore();
}

DirItem::DirItem(DirItem* p, SCRef ts, SCRef nm, TreeView* t) : FileItem(p, nm), treeSha(ts),
         tv(t), isWorkingDir(p->isWorkingDir) { setPixmap(0, *tv->folderClosed); }

DirItem::DirItem(Q3ListView* p, SCRef ts, SCRef nm, TreeView* t) : FileItem(p, nm), treeSha(ts),
         tv(t), isWorkingDir(ts == ZERO_SHA) {}

void DirItem::setup() {

	setExpandable(true);
	Q3ListViewItem::setup();
}

bool TreeView::getTree(SCRef treeSha, SList names, SList shas,
                       SList types, bool wd, SCRef treePath) {

	// calls qApp->processEvents()
	treeIsValid = git->getTree(treeSha, names, shas, types, wd, treePath);
	return treeIsValid;
}

void DirItem::setOpen(bool b) {

	setPixmap(0, b ? *tv->folderOpen : *tv->folderClosed);

	bool alreadyWaiting = false;
	if (QApplication::overrideCursor())
		alreadyWaiting = (QApplication::overrideCursor()->shape() == Qt::WaitCursor);

	if (!alreadyWaiting)
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	if (b && !childCount()) {

		QStringList names, types, shas;
		if (!tv->getTree(treeSha, names, shas, types, isWorkingDir, fullName()))
			return;

		if (!names.empty()) {
			QStringList::const_iterator it(names.constBegin());
			QStringList::const_iterator itSha(shas.constBegin());
			QStringList::const_iterator itTypes(types.constBegin());
			while (it != names.constEnd()) {

				if (*itTypes == "tree") {
					DirItem* item = new DirItem(this, *itSha, *it, tv);
					item->setModified(tv->isModified(item->fullName(), true));
				} else {
					FileItem* item = new FileItem(this, *it);
					item->setPixmap(0, *mimePix(*it));
					item->setModified(tv->isModified(item->fullName()));
				}
				++it;
				++itSha;
				++itTypes;
			}
		}
	}
	Q3ListViewItem::setOpen(b);
	if (!alreadyWaiting)
		QApplication::restoreOverrideCursor();
}

// ******************************* TreeView ****************************

TreeView::TreeView(Domain* dm, Git* g, Q3ListView* lv) : QObject(dm),
                   d(dm), git(g), listView(lv) {

	st = &(d->st);
	ignoreCurrentChanged = false;

	// set built-in pixmaps
	folderClosed = mimePix(".#FOLDER_CLOSED");
	folderOpen   = mimePix(".#FOLDER_OPEN");
	fileDefault  = mimePix(".#DEFAULT");

	connect(listView, SIGNAL(currentChanged(Q3ListViewItem*)),
	        this, SLOT(on_currentChanged(Q3ListViewItem*)));

	connect(listView, SIGNAL(contextMenuRequested(Q3ListViewItem*, const QPoint&, int)),
	        this, SLOT(on_contextMenuRequested(Q3ListViewItem*, const QPoint&, int)));
}

void TreeView::on_currentChanged(Q3ListViewItem* item) {

	if (item) {
		SCRef fn = ((FileItem*)item)->fullName();
		if (!ignoreCurrentChanged && fn != st->fileName()) {
			st->setFileName(fn);
			st->setSelectItem(true);
			UPDATE_DOMAIN(d);
		}
	}
}

void TreeView::on_contextMenuRequested(Q3ListViewItem* item, const QPoint&,int) {

	if (item)
		emit contextMenu(fullName(item), POPUP_TREE_EV);
}

void TreeView::clear() {

	rootName = "";
	listView->clear();
}

bool TreeView::isModified(SCRef path, bool isDir) {

	if (isDir)
		return (modifiedDirs.findIndex(path) != -1);

	return (modifiedFiles.findIndex(path) != -1);
}

bool TreeView::isDir(SCRef fileName) {

	// if currentItem is NULL or is different from fileName
	// return false, because treeview is not updated while
	// not visible, so could be out of sync.
	FileItem* item = static_cast<FileItem*>(listView->currentItem());
	if (item == NULL || item->fullName() != fileName)
		return false;

	return dynamic_cast<DirItem*>(item);
}

const QString TreeView::fullName(Q3ListViewItem* item) {

	FileItem* f = static_cast<FileItem*>(item);
	return (item ? f->fullName() : "");
}

void TreeView::getTreeSelectedItems(QStringList& selectedItems) {

	selectedItems.clear();
	Q3ListViewItemIterator it(listView);
	while (it.current()) {
		FileItem* f = static_cast<FileItem*>(it.current());
		if (f->isSelected())
			selectedItems.append(f->fullName());
		++it;
	}
}

void TreeView::setTree(SCRef treeSha) {

	if (listView->childCount() == 0)
		// get working dir info only once after each TreeView::clear()
		git->getWorkDirFiles(modifiedFiles, modifiedDirs);

	listView->clear();
	treeIsValid = true;

	if (!treeSha.isEmpty()) {
		// insert a new dir at the beginning of the list
		DirItem* root = new DirItem(listView, treeSha, rootName, this);
		root->setOpen(true); // be interesting
	}
}

void TreeView::update() {

	if (st->sha().isEmpty())
		return;

	// qt emits currentChanged() signal when populating
	// the list view, so we should ignore while here
	ignoreCurrentChanged = true;
	QApplication::setOverrideCursor(Qt::WaitCursor);

	bool newTree = true;
	DirItem* root = static_cast<DirItem*>(listView->firstChild());
	if (root && treeIsValid)
		newTree = (root->treeSha != st->sha());

	if (   newTree
	    && treeIsValid
	    && root
	    && st->sha() != ZERO_SHA
	    && root->treeSha != ZERO_SHA)
		// root->treeSha could reference a different sha from current
		// one in case the tree is the same, i.e. has the same files.
		// so we prefer to use the previous state sha to call isSameFiles()
		// and benefit from the early skip logic.
		// In case previous sha is the same of current it means an update
		// call has been forced, in that case we use the 'real' root->treeSha
		if (st->sha(true) != st->sha(false))
			newTree = !git->isSameFiles(st->sha(false), st->sha(true));
		else
			newTree = !git->isSameFiles(root->treeSha, st->sha(true));

	if (newTree) // ok, we really need to update the tree
		setTree(st->sha());
	else {
		FileItem* f = static_cast<FileItem*>(listView->currentItem());
		if (f && f->fullName() == st->fileName()) {

			restoreStuff();
			return;
		}
	}
	if (st->fileName().isEmpty()) {
		restoreStuff();
		return;
	}
	listView->setUpdatesEnabled(false);
	const QStringList lst(QStringList::split("/", st->fileName()));
	Q3ListViewItem* item = listView->firstChild();
	item = item->itemBelow(); // first item is repository name
	FOREACH_SL (it, lst) {
		while (item && treeIsValid) {
			if (item->text(0) == *it) {
				// could be a different subdirectory with the
				// same name that appears before in tree view
				// to be sure we need to check the names
				if (st->fileName().startsWith(((FileItem*)item)->fullName())) {
					item->setOpen(true);
					break; // from while loop only
				}
			}
			item = item->itemBelow();
		}
	}
	if (item && treeIsValid) {
		listView->clearSelection();
		listView->setSelected(item, true);
		listView->setCurrentItem(item); // calls on_currentChanged()
		listView->ensureItemVisible(item);
	} else
		; // st->fileName() has been deleted by a patch older than this tree

	listView->setUpdatesEnabled(true);
	listView->triggerUpdate();
	restoreStuff();
}

void TreeView::restoreStuff() {

	ignoreCurrentChanged = false;
	QApplication::restoreOverrideCursor();
}
