/*
	Description: file viewer window

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QStatusBar>
#include "mainimpl.h"
#include "git.h"
#include "annotate.h"
#include "listview.h"
#include "filecontent.h"
#include "fileview.h"

#define MAX_LINE_NUM 5

FileView::FileView(MainImpl* mi, Git* g) : Domain(mi, g, false) {

	container = new QWidget(NULL); // will be reparented to m()->tabWdg
	fileTab = new Ui_TabFile();
	fileTab->setupUi(container);
	fileTab->histListView->setup(this, git);
	fileTab->textEditFile->setup(this, git);

	// an empty string turn off the special-value text display
	fileTab->spinBoxRevision->setSpecialValueText(" ");

	clear(true); // init some stuff

	connect(git, SIGNAL(loadCompleted(const FileHistory*, const QString&)),
	        this, SLOT(on_loadCompleted(const FileHistory*, const QString&)));

	connect(m(), SIGNAL(repaintListViews(const QFont&)),
	        fileTab->histListView, SLOT(on_repaintListViews(const QFont&)));

	connect(fileTab->histListView, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	connect(fileTab->textEditFile, SIGNAL(annotationAvailable(bool)),
	        this, SLOT(on_annotationAvailable(bool)));

	connect(fileTab->textEditFile, SIGNAL(fileAvailable(bool)),
	        this, SLOT(on_fileAvailable(bool)));

	connect(fileTab->textEditFile, SIGNAL(revIdSelected(int)),
	        this, SLOT(on_revIdSelected(int)));

	connect(fileTab->toolButtonCopy, SIGNAL(clicked()),
	        this, SLOT(on_toolButtonCopy_clicked()));

	connect(fileTab->toolButtonShowAnnotate, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonShowAnnotate_toggled(bool)));

	connect(fileTab->toolButtonFindAnnotate, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonFindAnnotate_toggled(bool)));

	connect(fileTab->toolButtonRangeFilter, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonRangeFilter_toggled(bool)));

	connect(fileTab->toolButtonPin, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonPin_toggled(bool)));

	connect(fileTab->toolButtonHighlightText, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonHighlightText_toggled(bool)));

	connect(fileTab->spinBoxRevision, SIGNAL(valueChanged(int)),
	        this, SLOT(on_spinBoxRevision_valueChanged(int)));
}

FileView::~FileView() {

	if (!parent())
		return;

	// remove before to delete, avoids a Qt warning in QInputContext()
	m()->tabWdg->removeTab(m()->tabWdg->indexOf(container));

	delete fileTab->textEditFile; // must be deleted before fileTab
	delete fileTab;
	delete container;

	m()->statusBar()->clearMessage(); // cleanup any pending progress info
	QApplication::restoreOverrideCursor();
}

void FileView::clear(bool complete) {

	Domain::clear(complete);

	if (complete) {
		int idx = m()->tabWdg->indexOf(container);
		m()->tabWdg->setTabText(idx, "File");
		fileTab->toolButtonCopy->setEnabled(false);
	}
	fileTab->textEditFile->clearAll();

	annotateAvailable = fileAvailable = false;
	updateEnabledButtons();

	fileTab->toolButtonPin->setEnabled(false);
	fileTab->toolButtonPin->setChecked(false); // TODO signals pressed() and clicked() are not emitted
	fileTab->spinBoxRevision->setEnabled(false);
	fileTab->spinBoxRevision->setValue(fileTab->spinBoxRevision->minimum()); // clears the box
}

bool FileView::goToCurrentAnnotation() {

	SCRef ids = fileTab->histListView->currentText(QGit::ANN_ID_COL);
	int id = (!ids.isEmpty() ? ids.toInt() : 0);
	fileTab->textEditFile->goToAnnotation(id);
	return (id != 0);
}

void FileView::updateSpinBoxValue() {

	SCRef ids = fileTab->histListView->currentText(QGit::ANN_ID_COL);
	if (    ids.isEmpty()
	    || !fileTab->spinBoxRevision->isEnabled()
	    ||  fileTab->spinBoxRevision->value() == ids.toInt())
		return;

	fileTab->spinBoxRevision->setValue(ids.toInt()); // emit QSpinBox::valueChanged()
}

bool FileView::isMatch(SCRef sha) {

	static RangeInfo r; // fast path here, avoid allocation on each call
	if (!fileTab->textEditFile->getRange(sha, &r))
		return false;

	return r.modified;
}

void FileView::filterOnRange(bool isOn) {

	int matchedCnt = fileTab->histListView->filterRows(isOn, false);
	QString msg;
	if (isOn)
		msg = QString("Found %1 matches. Toggle filter "
		              "button to remove the filter").arg(matchedCnt);

// 	m()->statusBar()->message(msg);
	QApplication::postEvent(this, new MessageEvent(msg)); // deferred message, after update
}

bool FileView::doUpdate(bool force) {

	if (st.fileName().isEmpty())
		return false;

	if (st.isChanged(StateInfo::FILE_NAME) || force) {

		clear(false);
		model()->setFileName(st.fileName());

		int idx = m()->tabWdg->indexOf(container);
		m()->tabWdg->setTabText(idx, st.fileName());

		if (git->startFileHistory(model())) {
			QApplication::restoreOverrideCursor();
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			m()->statusBar()->showMessage("Retrieving history of '" +
			                              st.fileName() + "'...");
		}
	} else if (fileTab->histListView->update() || st.sha().isEmpty()) {

		updateSpinBoxValue();
		m()->statusBar()->showMessage(git->getRevInfo(st.sha()));
	}
	if (!fileTab->toolButtonPin->isChecked())
		fileTab->textEditFile->update();

	return true; // always accept new state
}

// ************************************ SLOTS ********************************

void FileView::updateEnabledButtons() {

	QToolButton* copy = fileTab->toolButtonCopy;
	QToolButton* showAnnotate = fileTab->toolButtonShowAnnotate;
	QToolButton* findAnnotate = fileTab->toolButtonFindAnnotate;
	QToolButton* rangeFilter = fileTab->toolButtonRangeFilter;
	QToolButton* highlight = fileTab->toolButtonHighlightText;

	// first enable
	copy->setEnabled(fileAvailable);
	showAnnotate->setEnabled(annotateAvailable);
	findAnnotate->setEnabled(annotateAvailable);
	rangeFilter->setEnabled(annotateAvailable);
	highlight->setEnabled(fileAvailable && git->isTextHighlighter());

	// then disable
	if (!showAnnotate->isChecked())
		findAnnotate->setEnabled(false);

	if (highlight->isChecked())
		rangeFilter->setEnabled(false);

	if (rangeFilter->isChecked())
		highlight->setEnabled(false);

	// special case: reset range filter when changing file
	if (!annotateAvailable && rangeFilter->isChecked())
		rangeFilter->toggle();
}

void FileView::on_toolButtonCopy_clicked() {

	fileTab->textEditFile->copySelection();
}

void FileView::on_toolButtonShowAnnotate_toggled(bool b) {

	updateEnabledButtons();
	fileTab->textEditFile->setShowAnnotate(b);

	if (b && fileTab->toolButtonFindAnnotate->isChecked())
		goToCurrentAnnotation();
}

void FileView::on_toolButtonFindAnnotate_toggled(bool b) {

	updateEnabledButtons();
	if (b)
		goToCurrentAnnotation();
}

void FileView::on_toolButtonPin_toggled(bool b) {
// button is enabled and togglable only if st.sha() is found

	fileTab->spinBoxRevision->setDisabled(b);

	if (!b) {
		updateSpinBoxValue();
		fileTab->textEditFile->update(true);
	}
}

void FileView::on_toolButtonRangeFilter_toggled(bool b) {

	updateEnabledButtons();
	if (b) {
		if (!fileTab->textEditFile->annotateAvailable()) {
			dbs("ASSERT in on_toolButtonRangeFilter_toggled: annotate not available");
			return;
		}
		if (!fileTab->textEditFile->textCursor().hasSelection()) {
			m()->statusBar()->showMessage("Please select some text");
			return;
		}
	}
	bool rangeFilterActive = fileTab->textEditFile->rangeFilter(b);
	filterOnRange(rangeFilterActive);
}

void FileView::on_toolButtonHighlightText_toggled(bool b) {

	updateEnabledButtons();
	fileTab->textEditFile->setHighlightSource(b);
}

void FileView::on_spinBoxRevision_valueChanged(int id) {

	if (id != fileTab->spinBoxRevision->minimum()) {

		SCRef selRev(fileTab->histListView->getSha(id));
		if (st.sha() != selRev) { // to avoid looping
			st.setSha(selRev);
			st.setSelectItem(true);
			UPDATE();
		}
	}
}

void FileView::on_loadCompleted(const FileHistory* f, const QString&) {

	QApplication::restoreOverrideCursor();

	if (f != model())
		return;

	m()->statusBar()->clearMessage();
	fileTab->histListView->showIdValues();
	int maxId = model()->rowCount();
	if (maxId == 0)
		return;

	fileTab->spinBoxRevision->setMaximum(maxId);
	fileTab->toolButtonPin->setEnabled(true);
	fileTab->spinBoxRevision->setEnabled(true);

	UPDATE();

	if (fileTab->textEditFile->startAnnotate(model()))
		updateProgressBar(0);
}

void FileView::showAnnotation() {

	if (  !fileTab->toolButtonPin->isChecked()
	    && fileTab->toolButtonShowAnnotate->isEnabled()
	    && fileTab->toolButtonShowAnnotate->isChecked()) {

		fileTab->textEditFile->setShowAnnotate(true);

		if (   fileTab->toolButtonFindAnnotate->isEnabled()
		    && fileTab->toolButtonFindAnnotate->isChecked())

			goToCurrentAnnotation();
	}
}

void FileView::on_annotationAvailable(bool b) {

	annotateAvailable = b;
	updateEnabledButtons();

	if (b)
		showAnnotation(); // in case annotation got ready after file
}

void FileView::on_fileAvailable(bool b) {

	fileAvailable = b;
	updateEnabledButtons();

	if (b) {
		// code range is independent from annotation
		if (fileTab->toolButtonRangeFilter->isChecked())
			fileTab->textEditFile->goToRangeStart();

		showAnnotation(); // in case file got ready after annotation
	}
}

void FileView::on_revIdSelected(int id) {

	if (id != 0 && fileTab->spinBoxRevision->isEnabled())
		fileTab->spinBoxRevision->setValue(id);
}

// ******************************* data events ****************************

bool FileView::event(QEvent* e) {

	if (e->type() == (int)QGit::ANN_PRG_EV) {
		updateProgressBar(((AnnotateProgressEvent*)e)->myData().toInt());
		return true;
	}
	return Domain::event(e);
}

void FileView::updateProgressBar(int annotatedNum) {

	uint tot = model()->rowCount();
	if (tot == 0)
		return;

	int cc = (annotatedNum * 100) / tot;
	int idx = (annotatedNum * 40) / tot;
	QString head("Annotating '" + st.fileName() + "' [");
	QString tail("] " + QString::number(cc) + " %");
	QString done, toDo;
	done.fill('.', idx);
	toDo.fill(' ', 40 - idx);
	m()->statusBar()->showMessage(head + done + toDo + tail);
}
