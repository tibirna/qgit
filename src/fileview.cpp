/*
	Description: file viewer window

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QHelpEvent>
#include "FileHistory.h"
#include "mainimpl.h"
#include "git.h"
#include "annotate.h"
#include "listview.h"
#include "filecontent.h"
#include "fileview.h"

#define MAX_LINE_NUM 5

FileView::FileView(MainImpl* mi, Git* g) : Domain(mi, g, false) {

	fileTab = new Ui_TabFile();
	fileTab->setupUi(container);
	fileTab->histListView->setup(this, git);
	fileTab->textEditFile->setup(this, git, fileTab->listWidgetAnn);

	// an empty string turn off the special-value text display
	fileTab->spinBoxRevision->setSpecialValueText(" ");

	// Add GNU source-highlight version to tooltip, or add a message that it's not installed.
	QToolButton* highlight = fileTab->toolButtonHighlightText;
	highlight->setToolTip(highlight->toolTip().arg(git->textHighlighterVersion()));

	clear(true); // init some stuff

	fileTab->listWidgetAnn->installEventFilter(this);

	chk_connect_a(git, SIGNAL(loadCompleted(const FileHistory*, const QString&)),
	        this, SLOT(on_loadCompleted(const FileHistory*, const QString&)));

	chk_connect_a(m(), SIGNAL(changeFont(const QFont&)),
	        fileTab->histListView, SLOT(on_changeFont(const QFont&)));

	chk_connect_a(fileTab->histListView, SIGNAL(contextMenu(const QString&, int)),
	        this, SLOT(on_contextMenu(const QString&, int)));

	chk_connect_a(fileTab->textEditFile, SIGNAL(annotationAvailable(bool)),
	        this, SLOT(on_annotationAvailable(bool)));

	chk_connect_a(fileTab->textEditFile, SIGNAL(fileAvailable(bool)),
	        this, SLOT(on_fileAvailable(bool)));

	chk_connect_a(fileTab->textEditFile, SIGNAL(revIdSelected(int)),
	        this, SLOT(on_revIdSelected(int)));

	chk_connect_a(fileTab->toolButtonCopy, SIGNAL(clicked()),
	        this, SLOT(on_toolButtonCopy_clicked()));

	chk_connect_a(fileTab->toolButtonShowAnnotate, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonShowAnnotate_toggled(bool)));

	chk_connect_a(fileTab->toolButtonFindAnnotate, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonFindAnnotate_toggled(bool)));

	chk_connect_a(fileTab->toolButtonGoNext, SIGNAL(clicked()),
	        this, SLOT(on_toolButtonGoNext_clicked()));

	chk_connect_a(fileTab->toolButtonGoPrev, SIGNAL(clicked()),
	        this, SLOT(on_toolButtonGoPrev_clicked()));

	chk_connect_a(fileTab->toolButtonRangeFilter, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonRangeFilter_toggled(bool)));

	chk_connect_a(fileTab->toolButtonPin, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonPin_toggled(bool)));

	chk_connect_a(fileTab->toolButtonHighlightText, SIGNAL(toggled(bool)),
	        this, SLOT(on_toolButtonHighlightText_toggled(bool)));

	chk_connect_a(fileTab->spinBoxRevision, SIGNAL(valueChanged(int)),
	        this, SLOT(on_spinBoxRevision_valueChanged(int)));
}

FileView::~FileView() {

	if (!parent())
		return;

	delete fileTab->textEditFile; // must be deleted before fileTab
	delete fileTab;
	showStatusBarMessage(""); // cleanup any pending progress info
	QApplication::restoreOverrideCursor();
}

bool FileView::eventFilter(QObject* obj, QEvent* e) {

	QListWidget* lw = fileTab->listWidgetAnn;
	if (e->type() == QEvent::ToolTip && obj == lw) {
		QHelpEvent* h = static_cast<QHelpEvent*>(e);
		int id = fileTab->textEditFile->itemAnnId(lw->itemAt(h->pos()));
		QRegExp re;
		SCRef sha(fileTab->histListView->shaFromAnnId(id));
		SCRef d(git->getDesc(sha, re, re, false, model()));
		lw->setToolTip(d);
	}
	return QObject::eventFilter(obj, e);
}

void FileView::clear(bool complete) {

	Domain::clear(complete);

	if (complete) {
		setTabCaption("File");
		fileTab->toolButtonCopy->setEnabled(false);
	}
	fileTab->textEditFile->clearAll(); // emits file/ann available signals

	fileTab->toolButtonPin->setEnabled(false);
	fileTab->toolButtonPin->setChecked(false); // TODO signals pressed() and clicked() are not emitted
	fileTab->spinBoxRevision->setEnabled(false);
	fileTab->spinBoxRevision->setValue(fileTab->spinBoxRevision->minimum()); // clears the box
}

bool FileView::goToCurrentAnnotation(int direction) {

	SCRef ids = fileTab->histListView->currentText(QGit::ANN_ID_COL);
	int id = (!ids.isEmpty() ? ids.toInt() : 0);
	fileTab->textEditFile->goToAnnotation(id, direction);
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

	showStatusBarMessage(msg);
	QApplication::postEvent(this, new MessageEvent(msg)); // deferred message, after update
}

bool FileView::doUpdate(bool force) {

	if (st.fileName().isEmpty())
		return false;

	if (st.isChanged(StateInfo::FILE_NAME) || force) {

		clear(false);
		setTabCaption(st.fileName());

		if (git->startFileHistory(st.sha(), st.fileName(), model())) {
			QApplication::restoreOverrideCursor();
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			showStatusBarMessage("Retrieving history of '" +
			                      st.fileName() + "'...");
		}
	} else if (fileTab->histListView->update() || st.sha().isEmpty()) {

		updateSpinBoxValue();
		showStatusBarMessage(git->getRevInfo(st.sha()));
	}
	if (!fileTab->toolButtonPin->isChecked())
		fileTab->textEditFile->doUpdate();

	return true; // always accept new state
}

// ************************************ SLOTS ********************************

void FileView::updateEnabledButtons() {

	QToolButton* copy = fileTab->toolButtonCopy;
	QToolButton* showAnnotate = fileTab->toolButtonShowAnnotate;
	QToolButton* findAnnotate = fileTab->toolButtonFindAnnotate;
	QToolButton* goPrev = fileTab->toolButtonGoPrev;
	QToolButton* goNext = fileTab->toolButtonGoNext;
	QToolButton* rangeFilter = fileTab->toolButtonRangeFilter;
	QToolButton* highlight = fileTab->toolButtonHighlightText;

	bool fileAvailable = fileTab->textEditFile->isFileAvailable();
	bool annotateAvailable = fileTab->textEditFile->isAnnotateAvailable();

	// first enable
	copy->setEnabled(fileAvailable);
	showAnnotate->setEnabled(annotateAvailable);
	findAnnotate->setEnabled(annotateAvailable);
	goPrev->setEnabled(annotateAvailable);
	goNext->setEnabled(annotateAvailable);
	rangeFilter->setEnabled(annotateAvailable);
	highlight->setEnabled(fileAvailable && git->isTextHighlighter());

	// then disable
	if (!showAnnotate->isChecked()) {
		findAnnotate->setEnabled(false);
		goPrev->setEnabled(false);
		goNext->setEnabled(false);
	}
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

void FileView::on_toolButtonGoNext_clicked() {

	goToCurrentAnnotation(1);
}

void FileView::on_toolButtonGoPrev_clicked() {

	goToCurrentAnnotation(-1);
}

void FileView::on_toolButtonPin_toggled(bool b) {
// button is enabled and togglable only if st.sha() is found

	fileTab->spinBoxRevision->setDisabled(b);

	if (!b) {
		updateSpinBoxValue(); // UPDATE() call is filtered in this case
		fileTab->textEditFile->doUpdate(true);
	}
}

void FileView::on_toolButtonRangeFilter_toggled(bool b) {

	updateEnabledButtons();
	if (b) {
		if (!fileTab->textEditFile->isAnnotateAvailable()) {
			dbs("ASSERT in on_toolButtonRangeFilter_toggled: annotate not available");
			return;
		}
		if (!fileTab->textEditFile->textCursor().hasSelection()) {
			showStatusBarMessage("Please select some text");
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

		SCRef selRev(fileTab->histListView->shaFromAnnId(id));
		if (st.sha() != selRev) { // to avoid looping
			st.setSha(selRev);
			st.setSelectItem(true);
			UPDATE();
		}
	}
}

void FileView::on_loadCompleted(const FileHistory* f, const QString& msg) {

	QApplication::restoreOverrideCursor();

	if (f != model())
		return;

	showStatusBarMessage("");
	fileTab->histListView->showIdValues();
	int maxId = model()->rowCount();
	if (maxId == 0)
		return;

	fileTab->spinBoxRevision->setMaximum(maxId);
	fileTab->toolButtonPin->setEnabled(true);
	fileTab->spinBoxRevision->setEnabled(true);

	// update histListView now to avoid to miss
	// following status bar messages
	doUpdate(false);

	QString histTime = msg.section(" ms", 0, 0).section(" ", -1);
	if (fileTab->textEditFile->startAnnotate(model(), histTime))
		showStatusBarMessage("Annotating revisions of '" + st.fileName() + "'...");
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

	updateEnabledButtons();
	if (b)
		showAnnotation(); // in case annotation got ready after file
}

void FileView::on_fileAvailable(bool b) {

	updateEnabledButtons();
	if (b) {
		// code range is independent from annotation
		if (fileTab->toolButtonRangeFilter->isChecked())
			fileTab->textEditFile->goToRangeStart();

		showAnnotation(); // in case file got ready after annotation
	}
}

void FileView::on_revIdSelected(int id) {

	if (id == 0)
		return;

	if (fileTab->spinBoxRevision->isEnabled())
		fileTab->spinBoxRevision->setValue(id);
	else {
		ListView* h = fileTab->histListView;
		int row = h->model()->rowCount() - id;
		QModelIndex idx = h->model()->index(row, 0);
		h->setCurrentIndex(idx);
	}
}
