/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#include <q3listview.h>
#include <q3listbox.h>
#include <qapplication.h>
#include <qpainter.h>
#include <q3dragobject.h>
#include "mainimpl.h"
#include "git.h"
#include "domain.h"
#include "filelist.h"

ListBoxFileItem::ListBoxFileItem(Q3ListBox* lb, SCRef t, const QColor& c) :
                                 Q3ListBoxText(lb, t), myColor(c) {}

void ListBoxFileItem::paint(QPainter* p) {

	if (myColor != Qt::black) {
		p->save();
		p->setPen(myColor);
		Q3ListBoxText::paint(p);
		p->restore();
	} else
		Q3ListBoxText::paint(p);
}

ListBoxFiles::ListBoxFiles(Domain* dm, Git* g, Q3ListBox* l) :
                           QObject(dm), d(dm), git(g), lb(l) {
	st = &(d->st);

	connect(lb, SIGNAL(currentChanged(Q3ListBoxItem*)),
	        this, SLOT(on_currentChanged(Q3ListBoxItem*)));

	connect(lb, SIGNAL(contextMenuRequested(Q3ListBoxItem*, const QPoint&)),
	        this, SLOT(on_contextMenuRequested(Q3ListBoxItem*)));

	connect(lb, SIGNAL(mouseButtonPressed(int, Q3ListBoxItem*, const QPoint&)),
	        this, SLOT(on_mouseButtonPressed(int, Q3ListBoxItem*, const QPoint&)));

	connect(lb, SIGNAL(onItem(Q3ListBoxItem*)),
	        this,SLOT(on_onItem(Q3ListBoxItem*)));

	connect(lb, SIGNAL(onViewport()),
	        this,SLOT(on_onViewport()));

	connect(lb, SIGNAL(clicked(Q3ListBoxItem*)),
	        this,SLOT(on_clicked(Q3ListBoxItem*)));
}

void ListBoxFiles::clear() {

	lb->clear();
}

void ListBoxFiles::update(const RevFile* files, bool newFiles) {

	if (st->diffToSha().isEmpty())
		lb->unsetPalette();
	else if (lb->paletteBackgroundColor() != QGit::LIGHT_BLUE)
		lb->setPaletteBackgroundColor(QGit::LIGHT_BLUE);

	if (newFiles)
		insertFiles(files);

	QString fileName(lb->currentText());
	git->removeExtraFileInfo(&fileName); // could be a renamed/copied file

	if (!fileName.isEmpty() && (fileName == st->fileName())) {
		lb->setSelected(lb->currentItem(), st->selectItem()); // just a refresh
		return;
	}
	lb->clearSelection();

	if (st->fileName().isEmpty())
		return;

	Q3ListBoxItem* c = lb->findItem(st->fileName(), Q3ListBox::ExactMatch);
	if (c == NULL) { // could be a renamed/copied file, try harder

		fileName = st->fileName();
		git->addExtraFileInfo(&fileName, st->sha(), st->diffToSha(), st->allMergeFiles());
		c = lb->findItem(fileName, Q3ListBox::ExactMatch);
	}
	lb->setSelected(c, st->selectItem()); // calls current changed
}

void ListBoxFiles::insertFiles(const RevFile* files) {

	lb->clear();

	if (!files)
		return;

	if (st->isMerge()) {
		QString header((st->allMergeFiles()) ?
		                "Click to view only interesting files" :
		                "Click to view all merge files");
		new ListBoxFileItem(lb, header, Qt::blue);
	}
	if (files->names.empty())
		return;

	int prevPar = files->mergeParent[0];
	for (int i = 0; i < files->names.count(); ++i) {

		QChar status(files->getStatus(i));
		if (status == QGit::UNKNOWN)
			continue;

		QColor clr = Qt::black;
		if (files->mergeParent[i] != prevPar) {
			prevPar = files->mergeParent[i];
			new ListBoxFileItem(lb, "", clr);
			new ListBoxFileItem(lb, "", clr);
		}
		QString extSt(files->getExtendedStatus(i));
		if (extSt.isEmpty()) {
			if (status == QGit::MODIFIED)
				; // common case
			else if (status == QGit::NEW)
				clr = Qt::darkGreen;
			else if (status == QGit::DELETED)
				clr = Qt::red;
		} else {
			clr = Qt::darkBlue;
			// in case of rename deleted file is not shown and...
			if (status == QGit::DELETED)
				continue;

			// ...new file is shown with extended info
			if (status == QGit::NEW) {
				new ListBoxFileItem(lb, extSt, clr);
				continue;
			}
		}
		new ListBoxFileItem(lb, git->filePath(*files, i), clr);
	}
}

void ListBoxFiles::on_currentChanged(Q3ListBoxItem* item) {

	if (item) {
		if (st->isMerge() && item == lb->firstItem()) { // header clicked

			// In a listbox without current item, as soon as the box
			// gains focus the first item becomes the current item
			// and a spurious currentChanged() signal is sent.
			// In case of a merge the signal arrives here and fakes
			// the user clicking on the header.
			//
			// The problem arise when user clicks on a merge header,
			// then list box gains focus and current item becomes null
			// because the content of the list is cleared and updated.
			//
			// If now tab is changed list box loose the focus and,
			// upon changing back again the tab the signal triggers
			// because Qt gives back the focus to the listbox.
			//
			// The workaround here is to give the focus away as soon
			// as the user clicks on the merge header. Note that a
			// lb->clearFocus() is not enough, we really need to
			// reassign the focus to someone else.
			d->tabContainer()->setFocus();

			st->setAllMergeFiles(!st->allMergeFiles());
			st->setSelectItem(true);
			UPDATE_DOMAIN(d);
			return;
		}
		QString fileName(item->text());
		git->removeExtraFileInfo(&fileName);
		// if we are called by updateFileList() fileName is already updated
		if (st->fileName() != fileName) { // avoid loops
			st->setFileName(fileName);
			st->setSelectItem(true);
			UPDATE_DOMAIN(d);
		}
	}
}

void ListBoxFiles::on_contextMenuRequested(Q3ListBoxItem* item) {

	if (!item)
		return;

	int idx = lb->index(item);
	if (idx == 0 && st->isMerge()) // header clicked
		return;

	emit contextMenu(item->text(), QGit::POPUP_FILE_EV);
}

void ListBoxFiles::on_mouseButtonPressed(int b, Q3ListBoxItem* item, const QPoint&) {

	if (item && b == Qt::LeftButton) {
		d->setReadyToDrag(true);
		dragFileName = item->text();
	}
}

void ListBoxFiles::on_clicked(Q3ListBoxItem*) {

	d->setReadyToDrag(false); // in case of just click without moving
}

void ListBoxFiles::on_onItem(Q3ListBoxItem*) { mouseMoved(); }
void ListBoxFiles::on_onViewport() { mouseMoved(); }

void ListBoxFiles::mouseMoved() {

	if (d->isReadyToDrag()) {

		if (!d->setDragging(true))
			return;

		if (dragFileName.isEmpty())
			dbs("ASSERT in ListBoxFiles::on_onItem: empty drag name");

		Q3DragObject* drObj = new Q3TextDrag(dragFileName, lb);
		dragFileName = "";

		drObj->dragCopy(); // do NOT delete drObj. Blocking until drop event

		d->setDragging(false);
	}
}
