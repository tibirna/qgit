/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#include <QDrag>
#include <QApplication>
#include <QMouseEvent>
#include "git.h"
#include "domain.h"
#include "filelist.h"

FileList::FileList(QWidget* p) : QListWidget(p), d(NULL), git(NULL), st(NULL) {}

void FileList::setup(Domain* dm, Git* g) {

	d = dm;
	git = g;
	st = &(d->st);

	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(on_customContextMenuRequested(const QPoint&)));

	connect(this, SIGNAL(currentItemChanged(QListWidgetItem*, QListWidgetItem*)),
	        this, SLOT(on_currentItemChanged(QListWidgetItem*, QListWidgetItem*)));
}

void FileList::addItem(const QString& label, const QColor& clr) {

	QListWidgetItem* item = new QListWidgetItem(label, this);
	item->setForeground(clr);
}

QString FileList::currentText() {

	QListWidgetItem* item = currentItem();
	return (item ? item->data(Qt::DisplayRole).toString() : "");
}

void FileList::focusInEvent(QFocusEvent*) {

	// Workaround a Qt4.2 bug
	//
	// When an item is clicked and FileList still doesn't have the
	// focus we could have a double currentItemChanged() signal,
	// the first with current set to first item, the second with
	// current correctly set to the clicked one. Unluckily in case
	// the clicked one is the first in list we have only one event.
	// Oddly enough overriding this virtual function we remove
	// the spurious first signal if any.
}

void FileList::on_currentItemChanged(QListWidgetItem* current, QListWidgetItem*) {

	if (!current)
		return;

	if (st->isMerge() && row(current) == 0) { // header clicked

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
		d->tabPage()->setFocus();
		st->setAllMergeFiles(!st->allMergeFiles());

	} else {
		QString fileName(currentText());
		git->removeExtraFileInfo(&fileName);
		// if we are called by updateFileList() fileName is already updated
		if (st->fileName() == fileName) // avoid loops
			return;

		st->setFileName(fileName);
	}
	st->setSelectItem(true);
	UPDATE_DOMAIN(d);
}

void FileList::on_customContextMenuRequested(const QPoint&) {

	int row = currentRow();
	if (row == -1 || (row == 0 && st->isMerge())) // header clicked
		return;

	emit contextMenu(currentText(), QGit::POPUP_FILE_EV);
}

void FileList::mousePressEvent(QMouseEvent* e) {

	if (currentItem() && e->button() == Qt::LeftButton) {
		d->setReadyToDrag(true);
		dragFileName = currentText();
	}
	QListWidget::mousePressEvent(e);
}

void FileList::mouseReleaseEvent(QMouseEvent* e) {

	d->setReadyToDrag(false); // in case of just click without moving
	QListWidget::mouseReleaseEvent(e);
}

void FileList::mouseMoveEvent(QMouseEvent* e) {

	if (d->isReadyToDrag()) {

		if (!d->setDragging(true))
			return;

		if (dragFileName.isEmpty())
			dbs("ASSERT in FileList::mouseMoveEvent() empty drag name");

		QDrag* drag = new QDrag(this);
		QMimeData* mimeData = new QMimeData;
		mimeData->setText(dragFileName);
		drag->setMimeData(mimeData);
		dragFileName = "";
		drag->start(); // blocking until drop event

		d->setDragging(false);
	}
	QListWidget::mouseMoveEvent(e);
}

void FileList::insertFiles(const RevFile* files) {

	clear();
	if (!files)
		return;

	if (st->isMerge()) {
		const QString header((st->allMergeFiles()) ?
		      "Click to view only interesting files" : "Click to view all merge files");
		addItem(header, Qt::blue);
	}
	if (files->names.empty())
		return;

	bool isMergeParents = !files->mergeParent.isEmpty();
	int prevPar = (isMergeParents ? files->mergeParent.first() : 1);
	setUpdatesEnabled(false);
	for (int i = 0; i < files->count(); ++i) {

		if (files->statusCmp(i, RevFile::UNKNOWN))
			continue;

		QColor clr = Qt::black;
		if (isMergeParents && files->mergeParent.at(i) != prevPar) {
			prevPar = files->mergeParent.at(i);
			new QListWidgetItem("", this);
			new QListWidgetItem("", this);
		}
		QString extSt(files->extendedStatus(i));
		if (extSt.isEmpty()) {
			if (files->statusCmp(i, RevFile::MODIFIED))
				; // common case
			else if (files->statusCmp(i, RevFile::NEW))
				clr = Qt::darkGreen;
			else if (files->statusCmp(i, RevFile::DELETED))
				clr = Qt::red;
		} else {
			clr = Qt::darkBlue;
			// in case of rename deleted file is not shown and...
			if (files->statusCmp(i, RevFile::DELETED))
				continue;

			// ...new file is shown with extended info
			if (files->statusCmp(i, RevFile::NEW)) {
				addItem(extSt, clr);
				continue;
			}
		}
		addItem(git->filePath(*files, i), clr);
	}
	setUpdatesEnabled(true);
}

void FileList::update(const RevFile* files, bool newFiles) {

	QPalette pl = QApplication::palette();
	if (!st->diffToSha().isEmpty())
		pl.setColor(QPalette::Base, QGit::LIGHT_BLUE);

	setPalette(pl);
	if (newFiles)
		insertFiles(files);

	QString fileName(currentText());
	git->removeExtraFileInfo(&fileName); // could be a renamed/copied file

	if (!fileName.isEmpty() && (fileName == st->fileName())) {
		currentItem()->setSelected(st->selectItem()); // just a refresh
		return;
	}
	clearSelection();

	if (st->fileName().isEmpty())
		return;

	QList<QListWidgetItem*> l = findItems(st->fileName(), Qt::MatchExactly);
	if (l.isEmpty()) { // could be a renamed/copied file, try harder

		fileName = st->fileName();
		git->addExtraFileInfo(&fileName, st->sha(), st->diffToSha(), st->allMergeFiles());
		l = findItems(fileName, Qt::MatchExactly);
	}
	if (!l.isEmpty()) {
		setCurrentItem(l.first());
		l.first()->setSelected(st->selectItem());
	}
}
