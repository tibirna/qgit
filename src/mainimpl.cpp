/*
	Description: qgit main view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QWheelEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QStatusBar>
#include <QFileDialog>
#include <q3accel.h>
#include "config.h" // defines PACKAGE_VERSION
#include "help.h"
#include "ui_help.h"
#include "consoleimpl.h"
#include "customactionimpl.h"
#include "settingsimpl.h"
#include "ui_revsview.h"
#include "ui_fileview.h"
#include "ui_patchview.h"
#include "common.h"
#include "git.h"
#include "listview.h"
#include "treeview.h"
#include "patchview.h"
#include "fileview.h"
#include "commitimpl.h"
#include "revsview.h"
#include "revdesc.h"
#include "mainimpl.h"

using namespace QGit;

MainImpl::MainImpl(SCRef cd, QWidget* p) : QMainWindow(p, "", Qt::WDestructiveClose) {

	EM_INIT(exExiting, "Exiting");

	setupUi(this);
	// manual setup widgets not buildable with Qt designer
	lineEditSHA = new QLineEdit(NULL);
	lineEditFilter = new QLineEdit(NULL);
	cmbSearch = new QComboBox(NULL);
	QString list("Short log,Log msg,Author,SHA1,File,Patch,Patch (regExp)");
	cmbSearch->addItems(list.split(","));
	toolBar->addWidget(lineEditSHA);
	QAction* act = toolBar->insertWidget(ActSearchAndFilter, lineEditFilter);
	toolBar->insertWidget(act, cmbSearch);
	connect(lineEditSHA, SIGNAL(returnPressed()), this, SLOT(lineEditSHA_returnPressed()));
	connect(lineEditFilter, SIGNAL(returnPressed()), this, SLOT(lineEditFilter_returnPressed()));

	// create light and dark colors for alternate background
	ODD_LINE_COL = palette().color(QPalette::Base);
	EVEN_LINE_COL = ODD_LINE_COL.dark(103);

	git = new Git(this);
	Q3Accel* accel = new Q3Accel(this);
	setupAccelerator(accel);
	qApp->installEventFilter(this);

	// init native types
	setRepositoryBusy = false;

	// init filter match highlighters
	shortLogRE.setMinimal(true);
	shortLogRE.setCaseSensitive(false);
	longLogRE.setMinimal(true);
	longLogRE.setCaseSensitive(false);

	// set-up typewriter (fixed width) font
	QSettings settings;
	QString font(settings.value(TYPWRT_FNT_KEY).toString());
	if (font.isEmpty()) { // choose a sensible default
		QFont fnt = QApplication::font();
		fnt.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
		fnt.setFixedPitch(true);
		fnt.setFamily(fnt.defaultFamily()); // the family corresponding
		font = fnt.toString();              // to current style hint
	}
	QGit::TYPE_WRITER_FONT.fromString(font);

	// set-up tab view
	delete tabWdg->currentPage(); // cannot be done in Qt Designer
	rv = new RevsView(this, git, true); // set has main domain

	// set-up tab corner widget ('close tab' button)
	QToolButton* ct = new QToolButton(tabWdg);
	ct->setIcon(QIcon(QString::fromUtf8(":/icons/resources/tab_remove.png")));
	ct->setToolTip("Close tab");
	ct->setEnabled(false);
	tabWdg->setCornerWidget(ct);
	connect(ct, SIGNAL(clicked()), this, SLOT(pushButtonCloseTab_clicked()));
	connect(this, SIGNAL(closeTabButtonEnabled(bool)), ct, SLOT(setEnabled(bool)));

	// set-up tree view
	treeView->hide();

	// set-up menu for recent visited repositories
	connect(File, SIGNAL(activated(int)), this, SLOT(openRecent_activated(int)));
	recentRepoMenuPos = 0;
	while (File->idAt(recentRepoMenuPos) != -1)
		recentRepoMenuPos++;
	doUpdateRecentRepoMenu("");

	// set-up menu for custom actions
	connect(Actions, SIGNAL(activated(int)), this, SLOT(customAction_activated(int)));
	doUpdateCustomActionMenu(settings.value(ACT_LIST_KEY).toStringList());

	// manual adjust lineEditSHA width
	QString tmp;
	tmp.fill('8', 41);
	int wd = lineEditSHA->fontMetrics().boundingRect(tmp).width();
	lineEditSHA->setMinimumWidth(wd);

	connect(git, SIGNAL(newRevsAdded(const FileHistory*, const QVector<QString>&)),
	        this, SLOT(newRevsAdded(const FileHistory*, const QVector<QString>&)));

	// connect cross-domain update signals
	connect(rv->tab()->listViewLog, SIGNAL(doubleClicked(const QModelIndex&)),
	        this, SLOT(listViewLog_doubleClicked(const QModelIndex&)));

	connect(rv->tab()->fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
	        this, SLOT(fileList_itemDoubleClicked(QListWidgetItem*)));

	connect(this->treeView, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
	        this, SLOT(treeView_doubleClicked(QTreeWidgetItem*, int)));

	// MainImpl c'tor is called before to enter event loop,
	// but some stuff requires event loop to init properly
	startUpDir = (cd.isEmpty()) ? QDir::current().absPath() : cd;
	QTimer::singleShot(10, this, SLOT(initWithEventLoopActive()));
}

void MainImpl::initWithEventLoopActive() {

	git->checkEnvironment();
	setRepository(startUpDir, false, false);
	startUpDir = ""; // one shot
}

void MainImpl::lineEditSHA_returnPressed() {

	rv->st.setSha(lineEditSHA->text());
	UPDATE_DOMAIN(rv);
}

void MainImpl::ActBack_activated() {

	lineEditSHA->undo(); // first for insert(text)
	if (lineEditSHA->text().isEmpty())
		lineEditSHA->undo(); // double undo, see RevsView::updateLineEditSHA()

	lineEditSHA_returnPressed();
}

void MainImpl::ActForward_activated() {

	lineEditSHA->redo(); // redo skips empty fields so one is enough
	lineEditSHA_returnPressed();
}

// *************************** ExternalDiffViewer ***************************

void MainImpl::ActExternalDiff_activated() {

	QStringList args;
	getExternalDiffArgs(&args);
	ExternalDiffProc* externalDiff = new ExternalDiffProc(args, this);
	externalDiff->setWorkingDirectory(curDir);

	if (!QGit::startProcess(externalDiff, args)) {
		QString text("Cannot start external viewer: ");
		text.append(externalDiff->args[0]);
		QMessageBox::warning(this, "Error - QGit", text);
		delete externalDiff;
	}
}

void MainImpl::getExternalDiffArgs(QStringList* args) {

	// save files to diff in working directory,
	// will be removed by ExternalDiffProc on exit
	QFileInfo f(rv->st.fileName());
	QString prevRevSha(rv->st.diffToSha());
	if (prevRevSha.isEmpty()) { // default to first parent
		const Rev* r = git->revLookup(rv->st.sha());
		prevRevSha = (r && r->parentsCount() > 0) ? r->parent(0) : rv->st.sha();
	}
	QString fName1(curDir + "/" + rv->st.sha().left(6) + "_" + f.baseName(true));
	QString fName2(curDir + "/" + prevRevSha.left(6) + "_" + f.baseName(true));

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	QString fileContent;
	git->getFile(rv->st.fileName(), rv->st.sha(), NULL, &fileContent);
	if (!writeToFile(fName1, fileContent))
		statusBar()->message("Unable to save " + fName1);

	git->getFile(rv->st.fileName(), prevRevSha, NULL, &fileContent);
	if (!writeToFile(fName2, fileContent))
		statusBar()->message("Unable to save " + fName2);

	// get external diff viewer
	QSettings settings;
	SCRef extDiff(settings.value(EXT_DIFF_KEY, EXT_DIFF_DEF).toString());

	QApplication::restoreOverrideCursor();

	// finally set process arguments
	args->append(extDiff);
	args->append(fName2);
	args->append(fName1);
}

// ********************** Repository open or changed *************************

void MainImpl::setRepository(SCRef newDir, bool refresh, bool keepSelection,
                             QStringList* filterList) {

	// Git::stop() and Git::init() are not re-entrant and must not
	// be active at the same time. Because Git::init calls processEvents()
	// we need to guard against reentrancy here
	if (setRepositoryBusy)
		return;

	setRepositoryBusy = true;

	// check for a refresh or open of a new repository while in filtered view
	if (ActFilterTree->isChecked() && filterList == NULL)
		// toggle() triggers a refresh and a following setRepository()
		// call that is filtered out by setRepositoryBusy guard flag
		ActFilterTree->toggle(); // triggers ActFilterTree_toggled()

	try {
		EM_REGISTER(exExiting);

		bool archiveChanged;
		curDir = git->getBaseDir(&archiveChanged, newDir);

		if (!git->stop(archiveChanged)) { // stop all pending processes, non blocking

			// some process is still running, schedule a deferred
			// call and return, waiting for end of activity
			setRepositoryBusy = false;
			EM_REMOVE(exExiting);

			// this object will delete itself when done
			new setRepoDelayed(this, newDir, refresh, keepSelection, filterList);
			return;
		}
		if (archiveChanged && refresh)
			dbs("ASSERT in setRepository: different dir with no range select");

		// now we can clear all our data
		setCaption(curDir + " - QGit");
		bool complete = !refresh || !keepSelection;
		rv->clear(complete);
		if (archiveChanged)
			emit closeAllTabs();

		// disable all actions
		updateGlobalActions(false);
		updateContextActions("", "", false, false);
		ActCommit_setEnabled(false);

		if (ActFilterTree->isChecked())
			setCaption(caption() + " - FILTER ON < " + filterList->join(" ") + " >");

		// tree name should be set before init because in case of
		// StGIT archives the first revs are sent before init returns
		QString n(curDir);
		this->treeView->setTreeName(n.prepend('/').section('/', -1, -1));

		bool quit;
		bool ok = git->init(curDir, !refresh, filterList, &quit); // blocking call
		if (quit)
			goto exit;

		updateCommitMenu(ok && git->isStGITStack());
		ActCheckWorkDir->setChecked(testFlag(DIFF_INDEX_F)); // could be changed in Git::init()

		if (ok) {
			updateGlobalActions(true);
			if (archiveChanged)
				updateRecentRepoMenu(curDir);
		} else
			statusBar()->message("Not a git archive");

exit:
		setRepositoryBusy = false;
		EM_REMOVE(exExiting);

		if (quit && !startUpDir.isEmpty())
			close();

	} catch (int i) {
		EM_REMOVE(exExiting);

		if (EM_MATCH(i, exExiting, "loading repository")) {
			EM_THROW_PENDING;
			return;
		}
		const QString info("Exception \'" + EM_DESC(i) + "\' not "
		                   "handled in setRepository...re-throw");
		dbs(info);
		throw;
	}
}

void MainImpl::updateGlobalActions(bool b) {

	ActRefresh->setEnabled(b);
	ActCheckWorkDir->setEnabled(b);
	ActViewRev->setEnabled(b);
	ActViewDiff->setEnabled(b);
	ActViewDiffNewTab->setEnabled(b && firstTab<PatchView>());
	ActShowTree->setEnabled(b);
	ActMailApplyPatch->setEnabled(b);
	ActMailFormatPatch->setEnabled(b);

	rv->setEnabled(b);
}

void MainImpl::updateContextActions(SCRef newRevSha, SCRef newFileName,
                                    bool isDir, bool found) {

	bool pathActionsEnabled = !newFileName.isEmpty();
	bool fileActionsEnabled = (pathActionsEnabled && !isDir);

	ActViewFile->setEnabled(fileActionsEnabled);
	ActViewFileNewTab->setEnabled(fileActionsEnabled && firstTab<FileView>());
	ActExternalDiff->setEnabled(fileActionsEnabled);
	ActSaveFile->setEnabled(fileActionsEnabled);
	ActFilterTree->setEnabled(pathActionsEnabled || ActFilterTree->isChecked());

	bool isTag, isUnApplied, isApplied;
	isTag = isUnApplied = isApplied = false;

	if (found) {
		const Rev* r = git->revLookup(newRevSha);
		isTag = git->checkRef(newRevSha, Git::TAG);
		isUnApplied = r->isUnApplied;
		isApplied = r->isApplied;
	}
	ActTag->setEnabled(found && (newRevSha != ZERO_SHA) && !isUnApplied);
	ActTagDelete->setEnabled(found && isTag && (newRevSha != ZERO_SHA) && !isUnApplied);
	ActPush->setEnabled(found && isUnApplied && git->isNothingToCommit());
	ActPop->setEnabled(found && isApplied && git->isNothingToCommit());
}

// ************************* cross-domain update Actions ***************************

void MainImpl::listViewLog_doubleClicked(const QModelIndex& index) {

	if (index.isValid() && ActViewDiff->isEnabled())
		ActViewDiff->activate(QAction::Trigger);
}

void MainImpl::histListView_doubleClicked(const QModelIndex& index) {

	if (index.isValid() && ActViewRev->isEnabled())
		ActViewRev->activate(QAction::Trigger);
}

void MainImpl::fileList_itemDoubleClicked(QListWidgetItem* item) {

	bool isFirst = (item && item->listWidget()->item(0) == item);
	if (isFirst && rv->st.isMerge())
		return;

	bool isMainView = (item && item->listWidget() == rv->tab()->fileList);
	if (isMainView && ActViewDiff->isEnabled())
		ActViewDiff->activate(QAction::Trigger);

	if (item && !isMainView && ActViewFile->isEnabled())
		ActViewFile->activate(QAction::Trigger);
}

void MainImpl::treeView_doubleClicked(QTreeWidgetItem* item, int) {

	if (item && ActViewFile->isEnabled())
		ActViewFile->activate(QAction::Trigger);
}

void MainImpl::pushButtonCloseTab_clicked() {

	int curPos = tabWdg->currentPageIndex();
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		break;
	case TAB_PATCH:
		t->deleteWhenDone();
		emit tabClosed(curPos);
		ActViewDiffNewTab->setEnabled(ActViewDiff->isEnabled() && firstTab<PatchView>());
		break;
	case TAB_FILE:
		t->deleteWhenDone();
		emit tabClosed(curPos);
		ActViewFileNewTab->setEnabled(ActViewFile->isEnabled() && firstTab<FileView>());
		break;
	default:
		dbs("ASSERT in pushButtonCloseTab_clicked: unknown current page");
		break;
	}
}

void MainImpl::ActViewRev_activated() {

	Domain* t;
	if (currentTabType(&t) == TAB_FILE) {
		rv->st = t->st;
		UPDATE_DOMAIN(rv);
	}
	tabWdg->setCurrentPage(rv->tabPos());
}

void MainImpl::ActViewFile_activated() {

	openFileTab(firstTab<FileView>());
}

void MainImpl::ActViewFileNewTab_activated() {

	openFileTab();
}

void MainImpl::openFileTab(FileView* fv) {

	if (!fv) {
		fv = new FileView(this, git);

		connect(fv->tab()->histListView, SIGNAL(doubleClicked(const QModelIndex&)),
		        this, SLOT(histListView_doubleClicked(const QModelIndex&)));

		connect(this, SIGNAL(closeAllTabs()), fv, SLOT(on_closeAllTabs()));

		ActViewFileNewTab->setEnabled(ActViewFile->isEnabled());
	}
	tabWdg->setCurrentPage(fv->tabPos());
	fv->st = rv->st;
	UPDATE_DOMAIN(fv);
}

void MainImpl::ActViewDiff_activated() {

	Domain* t;
	if (currentTabType(&t) == TAB_FILE) {
		rv->st = t->st;
		UPDATE_DOMAIN(rv);
	}
	rv->viewPatch(false);
	ActViewDiffNewTab->setEnabled(true);
}

void MainImpl::ActViewDiffNewTab_activated() {

	rv->viewPatch(true);
}

bool MainImpl::eventFilter(QObject* obj, QEvent* ev) {

	if (ev->type() == QEvent::Wheel) {

		QWheelEvent* e = static_cast<QWheelEvent*>(ev);
		if (e->state() == Qt::AltButton) {

			int idx = tabWdg->currentPageIndex();
			if (e->delta() < 0)
				idx = (++idx == tabWdg->count()) ? 0 : idx;
			else
				idx = (--idx < 0) ? tabWdg->count() - 1 : idx;

			tabWdg->setCurrentPage(idx);
			return true;
		}
	}
	return QWidget::eventFilter(obj, ev);
}

// ******************************* Filter ******************************

void MainImpl::newRevsAdded(const FileHistory* fh, const QVector<QString>&) {

	if (!git->isMainHistory(fh))
		return;

	if (ActSearchAndFilter->isChecked())
		ActSearchAndFilter_toggled(true); // filter again on new arrived data

	if (ActSearchAndHighlight->isChecked())
		ActSearchAndHighlight_toggled(true); // filter again on new arrived data

	// first rev could be a StGIT unapplied patch so check more then once
	if (   (!git->isNothingToCommit() || git->isUnknownFiles())
	    && !ActCommit->isEnabled()
	    && !git->isCommittingMerge())
		ActCommit_setEnabled(true);
}

void MainImpl::lineEditFilter_returnPressed() {

	ActSearchAndFilter->setChecked(true);
}

void MainImpl::ActSearchAndFilter_toggled(bool isOn) {

	ActSearchAndHighlight->setEnabled(!isOn);
	ActSearchAndFilter->setEnabled(false);
	filterList(isOn, false); // blocking call
	ActSearchAndFilter->setEnabled(true);
}

void MainImpl::ActSearchAndHighlight_toggled(bool isOn) {

	ActSearchAndFilter->setEnabled(!isOn);
	ActSearchAndHighlight->setEnabled(false);
	filterList(isOn, true); // blocking call
	ActSearchAndHighlight->setEnabled(true);
}

void MainImpl::filterList(bool isOn, bool onlyHighlight) {

	lineEditFilter->setEnabled(!isOn);
	cmbSearch->setEnabled(!isOn);

	SCRef filter(lineEditFilter->text());
	if (filter.isEmpty())
		return;

	QMap<QString, bool> shaMap;
	bool patchNeedsUpdate, isRegExp;
	patchNeedsUpdate = isRegExp = false;
	int idx = cmbSearch->currentItem(), colNum = 0;
	if (isOn) {
		switch (idx) {
		case 0:
			colNum = LOG_COL;
			shortLogRE.setPattern(filter);
			break;
		case 1:
			colNum = LOG_MSG_COL;
			longLogRE.setPattern(filter);
			break;
		case 2:
			colNum = AUTH_COL;
			break;
		case 3:
			colNum = COMMIT_COL;
			break;
		case 4:
		case 5:
		case 6:
			colNum = SHA_MAP_COL;
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			EM_PROCESS_EVENTS; // to paint wait cursor
			if (idx == 4)
				git->getFileFilter(filter, shaMap);
			else {
				isRegExp = (idx == 6);
				if (!git->getPatchFilter(filter, isRegExp, shaMap)) {
					QApplication::restoreOverrideCursor();
					ActSearchAndFilter->toggle();
					return;
				}
				patchNeedsUpdate = (shaMap.count() > 0);
			}
			QApplication::restoreOverrideCursor();
			break;
		}
	} else {
		patchNeedsUpdate = (idx == 5 || idx == 6);
		shortLogRE.setPattern("");
		longLogRE.setPattern("");
	}
	// TODO move in list view or in revsview below this point
	QTreeView* lv = rv->tab()->listViewLog;
	int visibleRows = 0, row = 0, rowCnt = rv->model()->rowCount();
	QSet<int> highlightedRows;
	QModelIndex parent;
	for ( ; row < rowCnt; row++) {
		if (isOn) {
			if (passFilter(rv->model()->sha(row), filter, colNum, shaMap)) {
				visibleRows++;
				if (onlyHighlight)
					highlightedRows.insert(row);

			} else if (!onlyHighlight)
				lv->setRowHidden(row, parent, true);

		} else if (lv->isRowHidden(row, parent))
			lv->setRowHidden(row, parent, false);
	}
	emit highlightedRowsChanged(highlightedRows);
	emit updateRevDesc(); // could be highlighted
	if (patchNeedsUpdate)
		emit highlightPatch(isOn ? filter : "", isRegExp);

	QModelIndex cur = lv->currentIndex();
	if (cur.isValid() && lv->isRowHidden(cur.row(), parent) && visibleRows > 0) {
		// we have an hidden current item so main list is
		// out of sync with description and file list
		// a workaround could be to select the first item in list
		QModelIndex first = lv->indexAt(QPoint(0, 0));
		if (first.isValid()) {
			rv->st.setSha(rv->model()->sha(first.row()));
			UPDATE_DOMAIN(rv);
		}
	}
	QString msg;
	if (isOn)
		msg = QString("Found %1 matches. Toggle filter/highlight "
		              "button to remove the filter").arg(visibleRows);
	QApplication::postEvent(rv, new MessageEvent(msg)); // deferred message, after update
}

bool MainImpl::passFilter(SCRef sha, SCRef filter, int colNum, const QMap<QString, bool>& shaMap) {
// TODO move in git

	if (colNum == SHA_MAP_COL)
		// in this case shaMap contains all good sha to search for
		return shaMap.contains(sha);

	const Rev* r = git->revLookup(sha);
	if (!r) {
		dbs("ASSERT in MainImpl::passFilter, sha <%1> not found");
		return false;
	}
	QString target;
	if (colNum == LOG_COL)
		target = r->shortLog();
	else if (colNum == AUTH_COL)
		target = r->author();
	else if (colNum == LOG_MSG_COL)
		target = r->longLog();
	else if (colNum == COMMIT_COL)
		target = sha;

	// wildcard search, case insensitive
	return target.contains(QRegExp(filter, false, true));
}

bool MainImpl::event(QEvent* e) {

	BaseEvent* de = dynamic_cast<BaseEvent*>(e);
	if (de == NULL)
		return QWidget::event(e);

	SCRef data = de->myData();
	bool ret = true;

	switch (e->type()) {
	case ERROR_EV: {
		QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
		EM_PROCESS_EVENTS;
		MainExecErrorEvent* me = (MainExecErrorEvent*)e;
		QString text("An error occurred while executing command:\n\n");
		text.append(me->command() + "\n\nGit says: \n\n" + me->report());
		QMessageBox::warning(this, "Error - QGit", text);
		QApplication::restoreOverrideCursor(); }
		break;
	case MSG_EV:
		statusBar()->message(data);
		break;
	case POPUP_LIST_EV:
		doContexPopup(data);
		break;
	case POPUP_FILE_EV:
	case POPUP_TREE_EV:
		doFileContexPopup(data, e->type());
		break;
	default:
		dbp("ASSERT in MainImpl::event unhandled event %1", e->type());
		ret = false;
		break;
	}
	return ret;
}

int MainImpl::currentTabType(Domain** t) {

	*t = NULL;
	int curPos = tabWdg->currentPageIndex();
	if (curPos == rv->tabPos()) {
		*t = rv;
		return TAB_REV;
	}
	QList<PatchView*>* l = getTabs<PatchView>(curPos);
	if (l->count() > 0) {
		*t = l->first();
		delete l;
		return TAB_PATCH;
	}
	delete l;
	QList<FileView*>* l2 = getTabs<FileView>(curPos);
	if (l2->count() > 0) {
		*t = l2->first();
		delete l2;
		return TAB_FILE;
	}
	if (l2->count() > 0)
		dbs("ASSERT in tabType file not found");

	delete l2;
	return -1;
}

template<class X> QList<X*>* MainImpl::getTabs(int tabPos) {

	X dummy;
	QObjectList l = this->queryList(dummy.className());
	QList<X*>* ret = new QList<X*>;
	for (int i = 0; i < l.size(); ++i) {

		X* x = static_cast<X*>(l.at(i));
		if (tabPos == -1 || x->tabPos() == tabPos)
			ret->append(x);
	}
	return ret; // 'ret' must be deleted by caller
}

template<class X> X* MainImpl::firstTab(int startPos) {

	int minVal = 99, firstVal = 99;
	X* min = NULL;
	X* first = NULL;
	QList<X*>* l = getTabs<X>();
	for (int i = 0; i < l->size(); ++i) {

		X* d = l->at(i);
		if (d->tabPos() < minVal) {
			minVal = d->tabPos();
			min = d;
		}
		if (d->tabPos() < firstVal && d->tabPos() > startPos) {
			firstVal = d->tabPos();
			first = d;
		}
	}
	delete l;
	return first ? first : min;
}

void MainImpl::tabWdg_currentChanged(QWidget* w) {

	if (w == NULL)
		return;

	// set correct focus for keyboard browsing
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		static_cast<RevsView*>(t)->tab()->listViewLog->setFocus();
		emit closeTabButtonEnabled(false);
		break;
	case TAB_PATCH:
		static_cast<PatchView*>(t)->tab()->textEditDiff->setFocus();
		emit closeTabButtonEnabled(true);
		break;
	case TAB_FILE:
		static_cast<FileView*>(t)->tab()->histListView->setFocus();
		emit closeTabButtonEnabled(true);
		break;
	default:
		dbs("ASSERT in tabWdg_currentChanged: unknown current page");
		break;
	}
}

void MainImpl::accelActivated(int id) {

	switch (id) {
	case KEY_UP:
		scrollListView(-1);
		break;
	case KEY_DOWN:
		scrollListView(1);
		break;
	case SHIFT_KEY_UP:
		goMatch(-1);
		break;
	case SHIFT_KEY_DOWN:
		goMatch(1);
		break;
	case KEY_LEFT:
		ActBack_activated();
		break;
	case KEY_RIGHT:
		ActForward_activated();
		break;
	case CTRL_PLUS:
		adjustFontSize(1);
		break;
	case CTRL_MINUS:
		adjustFontSize(-1);
		break;
	case KEY_U:
		scrollTextEdit(-18);
		break;
	case KEY_D:
		scrollTextEdit(18);
		break;
	case KEY_DELETE:
	case KEY_B:
	case KEY_BCKSPC:
		scrollTextEdit(-1);
		break;
	case KEY_SPACE:
		scrollTextEdit(1);
		break;
	case KEY_R:
		tabWdg->setCurrentPage(rv->tabPos());
		break;
	case KEY_P:
	case KEY_F:{
		int cp = tabWdg->currentPageIndex();
		Domain* d = (id == KEY_P) ? static_cast<Domain*>(firstTab<PatchView>(cp)) :
		                            static_cast<Domain*>(firstTab<FileView>(cp));
		if (d)
			tabWdg->setCurrentPage(d->tabPos()); }
		break;
	}
}

void MainImpl::setupAccelerator(Q3Accel* accel) {

	accel->insertItem(Qt::Key_Up,         KEY_UP);
	accel->insertItem(Qt::Key_I,          KEY_UP);
	accel->insertItem(Qt::Key_Down,       KEY_DOWN);
	accel->insertItem(Qt::Key_N,          KEY_DOWN);
	accel->insertItem(Qt::Key_K,          KEY_DOWN);
	accel->insertItem(Qt::Key_Left,       KEY_LEFT);
	accel->insertItem(Qt::Key_Right,      KEY_RIGHT);
	accel->insertItem(Qt::SHIFT+Qt::Key_Up,   SHIFT_KEY_UP);
	accel->insertItem(Qt::SHIFT+Qt::Key_Down, SHIFT_KEY_DOWN);
	accel->insertItem(Qt::CTRL+Qt::Key_Plus,  CTRL_PLUS);
	accel->insertItem(Qt::CTRL+Qt::Key_Minus, CTRL_MINUS);
	accel->insertItem(Qt::Key_U,          KEY_U);
	accel->insertItem(Qt::Key_D,          KEY_D);
	accel->insertItem(Qt::Key_Delete,     KEY_DELETE);
	accel->insertItem(Qt::Key_B,          KEY_B);
	accel->insertItem(Qt::Key_Backspace,  KEY_BCKSPC);
	accel->insertItem(Qt::Key_Space,      KEY_SPACE);
	accel->insertItem(Qt::Key_R,          KEY_R);
	accel->insertItem(Qt::Key_P,          KEY_P);
	accel->insertItem(Qt::Key_F,          KEY_F);

	connect(accel, SIGNAL(activated(int)), this, SLOT(accelActivated(int)));
}

void MainImpl::goMatch(int delta) {

// 	if (!ActSearchAndHighlight->isChecked()) FIXME
// 		return;
//
// 	Q3ListViewItemIterator it(rv->tab()->listViewLog->currentItem());
// 	if (delta > 0)
// 		++it;
// 	else
// 		--it;
//
// 	while (it.current()) {
// 		ListViewItem* item = static_cast<ListViewItem*>(it.current());
// 		if (item->highlighted()) {
// 			Q3ListView* lv = rv->tab()->listViewLog;
// 			lv->clearSelection();
// 			lv->setCurrentItem(item);
// 			lv->ensureItemVisible(lv->currentItem());
// 			return;
// 		}
// 		if (delta > 0)
// 			++it;
// 		else
// 			--it;
// 	}
}

QTextEdit* MainImpl::getCurrentTextEdit() {

	QTextEdit* te = NULL;
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		te = static_cast<RevsView*>(t)->tab()->textBrowserDesc;
		break;
	case TAB_PATCH:
 		te = static_cast<PatchView*>(t)->tab()->textEditDiff;
		break;
	case TAB_FILE:
// 		te = static_cast<FileView*>(t)->tab()->textEditFile; FIXME
		break;
	default:
		break;
	}
	return te;
}

void MainImpl::scrollTextEdit(int delta) {

	QTextEdit* te = getCurrentTextEdit();
	if (!te)
		return;

	QScrollBar* vs = te->verticalScrollBar();
	if (delta == 1 || delta == -1)
		vs->setValue(vs->value() + delta * (vs->pageStep() - vs->singleStep()));
	else
		vs->setValue(vs->value() + delta * vs->singleStep());
}

void MainImpl::scrollListView(int delta) {

	QWidget* lv = NULL;
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		lv = static_cast<RevsView*>(t)->tab()->listViewLog;
		break;
	case TAB_FILE:
		lv = static_cast<FileView*>(t)->tab()->histListView;
		break;
	default:
		lv = qApp->focusWidget();
		break;
	}
	if (!lv)
		return;

	int key = (delta == 1) ? Qt::Key_Down : Qt::Key_Up;
	QKeyEvent p(QEvent::KeyPress, key, 0, 0);
	QKeyEvent r(QEvent::KeyRelease, key, 0, 0);
	QApplication::sendEvent(lv, &p);
	QApplication::sendEvent(lv, &r);
}

void MainImpl::adjustFontSize(int delta) {
// font size is changed on a 'per instance' base and only on list views

	int ps = listViewFont.pointSize() + delta;
	if (ps < 2)
		return;

	listViewFont.setPointSize(ps);
	emit repaintListViews(listViewFont);
}

// ****************************** Menu *********************************

void MainImpl::updateCommitMenu(bool isStGITStack) {

	int i = 0;
	bool found = false;
	while (!found && Edit->idAt(i) != -1) {
		SCRef txt(Edit->text(Edit->idAt(i++)));
		found = (txt == "&Commit..." || txt == "St&GIT patch...");
	}
	if (!found)
		return;

	const QString newText(isStGITStack ? "St&GIT patch..." : "&Commit...");
	Edit->changeItem(Edit->idAt(--i), newText);
}

void MainImpl::updateRecentRepoMenu(SCRef newEntry) {

	// update menu of all windows
	foreach (QWidget* widget, QApplication::topLevelWidgets()) {

		MainImpl* w = dynamic_cast<MainImpl*>(widget);
		if (w)
			w->doUpdateRecentRepoMenu(newEntry);
	}
}

void MainImpl::doUpdateRecentRepoMenu(SCRef newEntry) {

	while (File->idAt(recentRepoMenuPos) != -1)
		File->removeItemAt(recentRepoMenuPos); // removes also any separator

	QSettings settings;
	QStringList recents(settings.value(REC_REP_KEY).toStringList());
	if (recents.isEmpty() && newEntry.isEmpty())
		return;

	QStringList::iterator it = recents.find(newEntry);
	if (it != recents.end())
		recents.remove(it);

	if (!newEntry.isEmpty())
		recents.prepend(newEntry);

	File->insertSeparator();

	QStringList::const_iterator it2 = recents.constBegin();
	for (int i = 1; it2 != recents.constEnd() && i <= MAX_RECENT_REPOS; ++it2, ++i)
		File->insertItem(QString::number(i) + " " + *it2);

	for (int i = recents.count() - MAX_RECENT_REPOS; i > 0; i--)
		recents.pop_back();

	settings.setValue(REC_REP_KEY, recents);
}

void MainImpl::doContexPopup(SCRef sha) {

	// we need to use popup() to be non blocking and we need a
	// global scope because we use a signal/slot connection
	delete contextMenu;
	delete contextSubMenu;
	contextMenu = new Q3PopupMenu(this);
	contextSubMenu = new Q3PopupMenu(this);
	connect(contextMenu, SIGNAL(activated(int)), this, SLOT(goRef_activated(int)));
	connect(contextSubMenu, SIGNAL(activated(int)), this, SLOT(goRef_activated(int)));

	Domain* t;
	int tt = currentTabType(&t);
	bool isRevPage = (tt == TAB_REV);
	bool isPatchPage = (tt == TAB_PATCH);
	bool isFilePage = (tt == TAB_FILE);

	if (!isFilePage && ActCheckWorkDir->isEnabled()) {
		ActCheckWorkDir->addTo(contextMenu);
		contextMenu->insertSeparator();
	}
	if (isFilePage && ActViewRev->isEnabled())
		ActViewRev->addTo(contextMenu);

	if (!isPatchPage && ActViewDiff->isEnabled())
		ActViewDiff->addTo(contextMenu);

	if (isRevPage && ActViewDiffNewTab->isEnabled())
		ActViewDiffNewTab->addTo(contextMenu);

	if (!isFilePage && ActExternalDiff->isEnabled())
		ActExternalDiff->addTo(contextMenu);

	if (isRevPage) {
		if (ActCommit->isEnabled() && (sha == ZERO_SHA))
			ActCommit->addTo(contextMenu);
		if (ActTag->isEnabled())
			ActTag->addTo(contextMenu);
		if (ActTagDelete->isEnabled())
			ActTagDelete->addTo(contextMenu);
		if (ActMailFormatPatch->isEnabled())
			ActMailFormatPatch->addTo(contextMenu);
		if (ActPush->isEnabled())
			ActPush->addTo(contextMenu);
		if (ActPop->isEnabled())
			ActPop->addTo(contextMenu);

		const QStringList& bn(git->getAllRefNames(Git::BRANCH, Git::optOnlyLoaded));
		const QStringList& tn(git->getAllRefNames(Git::TAG, Git::optOnlyLoaded));
		if (bn.empty() && tn.empty()) {
			contextMenu->exec(QCursor::pos());
			return;
		}
		int id = 1;
		if (!bn.empty()) {
			contextMenu->insertSeparator();
			QStringList::const_iterator it = bn.constBegin();
			for ( ; it != bn.constEnd(); ++it, id++) {
				// branch names have id > 0 to disambiguate them from actions,
				// Qt assigns negative id as default
				if (id < MAX_MENU_ENTRIES)
					contextMenu->insertItem(*it, id);
				else
					contextSubMenu->insertItem(*it, id);
			}
		}
		if (!tn.empty()) {
			contextMenu->insertSeparator();
			QStringList::const_iterator it = tn.constBegin();
			for ( ; it != tn.constEnd(); ++it, id++) {
				// tag names have id > 0 to disambiguate them from actions,
				// Qt assigns negative id as default
				if (id < MAX_MENU_ENTRIES)
					contextMenu->insertItem(*it, id);
				else
					contextSubMenu->insertItem(*it, id);
			}
		}
		if (contextSubMenu->count() > 0)
			contextMenu->insertItem("More...", contextSubMenu);
	}
	contextMenu->popup(QCursor::pos());
}

void MainImpl::doFileContexPopup(SCRef fileName, int type) {

	Q3PopupMenu contextMenu;

	Domain* t;
	int tt = currentTabType(&t);
	bool isRevPage = (tt == TAB_REV);
	bool isPatchPage = (tt == TAB_PATCH);
	bool isDir = this->treeView->isDir(fileName);

	if (type == POPUP_FILE_EV)
		if (!isPatchPage && ActViewDiff->isEnabled())
			ActViewDiff->addTo(&contextMenu);

	if (!isDir && ActViewFile->isEnabled())
		ActViewFile->addTo(&contextMenu);

	if (!isDir && ActViewFileNewTab->isEnabled())
		ActViewFileNewTab->addTo(&contextMenu);

	if (!isRevPage && (type == POPUP_FILE_EV) && ActViewRev->isEnabled())
		ActViewRev->addTo(&contextMenu);

	if (ActFilterTree->isEnabled())
		ActFilterTree->addTo(&contextMenu);

	if (!isDir) {
		if (ActSaveFile->isEnabled())
			ActSaveFile->addTo(&contextMenu);
		if ((type == POPUP_FILE_EV) && ActExternalDiff->isEnabled())
			ActExternalDiff->addTo(&contextMenu);
	}
	contextMenu.exec(QCursor::pos());
}

void MainImpl::goRef_activated(int id) {

	if (id <= 0) // not a tag name entry
		return;

	SCRef refSha(git->getRefSha(contextMenu->text(id)));
	rv->st.setSha(refSha);
	UPDATE_DOMAIN(rv);
}

void MainImpl::ActSplitView_activated() {

	bool hide;
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV: {
		RevsView* rv = static_cast<RevsView*>(t);
		hide = rv->tab()->textBrowserDesc->isVisible();
		rv->tab()->textBrowserDesc->setHidden(hide);
		rv->tab()->fileList->setHidden(hide); }
		break;
	case TAB_PATCH: {
		PatchView* pv = static_cast<PatchView*>(t);
		hide = pv->tab()->textBrowserDesc->isVisible();
		pv->tab()->textBrowserDesc->setHidden(hide);
		}
		break;
	case TAB_FILE: {
		FileView* fv = static_cast<FileView*>(t);
		hide = fv->tab()->histListView->isVisible();
		fv->tab()->histListView->setHidden(hide); }
		break;
	default:
		dbs("ASSERT in ActSplitView_activated: unknown current page");
		break;
	}
}

void MainImpl::ActShowTree_toggled(bool b) {

	if (b) {
		treeView->show();
		UPDATE_DOMAIN(rv);
	} else
		treeView->hide();
}

void MainImpl::ActSaveFile_activated() {

	QFileInfo f(rv->st.fileName());
	const QString fileName(QFileDialog::getSaveFileName(f.fileName(), "",
	                       this, "save file dialog", "Save file as"));

	if (fileName.isEmpty())
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	if (!git->saveFile(rv->st.fileName(), rv->st.sha(), fileName))
		statusBar()->message("Unable to save " + fileName);

	QApplication::restoreOverrideCursor();
}

void MainImpl::openRecent_activated(int id) {

	bool ok;
	File->text(id).left(1).toInt(&ok);
	if (!ok) // only recent repos entries have a number in first char
		return;

	const QString workDir(File->text(id).section(' ', 1));
	if (!workDir.isEmpty())
		setRepository(workDir, false, false);
}

void MainImpl::ActOpenRepo_activated() {

	const QString dirName(QFileDialog::getExistingDirectory(curDir,
	                      this, "", "Choose a directory"));

	if (!dirName.isEmpty()) {
		QDir d(dirName);
		setRepository(d.absPath(), false, false);
	}
}

void MainImpl::ActOpenRepoNewWindow_activated() {

	const QString dirName(QFileDialog::getExistingDirectory(curDir,
	                      this, "", "Choose a directory"));

	if (!dirName.isEmpty()) {
		QDir d(dirName);
		MainImpl* newWin = new MainImpl(d.absPath());
		newWin->show();
	}
}

void MainImpl::refreshRepo(bool b) {

	setRepository(curDir, true, b);
}

void MainImpl::ActRefresh_activated() {

	refreshRepo(true);
}

void MainImpl::ActMailFormatPatch_activated() {

	QStringList selectedItems;
	rv->tab()->listViewLog->getSelectedItems(selectedItems);
	if (selectedItems.isEmpty()) {
		statusBar()->message("At least one selected revision needed");
		return;
	}
	if (selectedItems.contains(ZERO_SHA)) {
		statusBar()->message("Unable to format patch for not committed content");
		return;
	}
	QSettings settings;
	QString outDir(settings.value(PATCH_DIR_KEY, curDir).toString());
	QString dirPath(QFileDialog::getExistingDirectory(outDir, this, "",
	                "Choose destination directory - Format Patch"));
	if (dirPath.isEmpty())
		return;

	QDir d(dirPath);
	settings.setValue(PATCH_DIR_KEY, d.absPath());
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	git->formatPatch(selectedItems, d.absPath());
	QApplication::restoreOverrideCursor();
}

bool MainImpl::askApplyPatchParameters(bool* commit, bool* fold) {

	int ret = QMessageBox::question(this, "Apply Patch",
	          "Do you want to commit or just to apply changes to "
	          "working directory?", "&Cancel", "&Working dir", "&Commit", 0, 0);
	if (ret == 0)
		return false;

	*commit = (ret == 2);
	*fold = false;
	if (*commit && git->isStGITStack()) {
		ret = QMessageBox::question(this, "Apply Patch", "Do you want to "
		      "import or fold the patch?", "&Cancel", "&Fold", "&Import", 0, 0);
		if (ret == 0)
			return false;

		*fold = (ret == 1);
	}
	return true;
}

void MainImpl::ActMailApplyPatch_activated() {

	QSettings settings;
	QString outDir(settings.value(PATCH_DIR_KEY, curDir).toString());
	QString patchName(QFileDialog::getOpenFileName(outDir, NULL, this,
	                  "", "Choose the patch file - Apply Patch"));
	if (patchName.isEmpty())
		return;

	QFileInfo f(patchName);
	settings.setValue(PATCH_DIR_KEY, f.dirPath(true));

	bool commit, fold;
	if (!askApplyPatchParameters(&commit, &fold))
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	if (git->applyPatchFile(f.absFilePath(), commit, fold, !Git::optDragDrop) && !commit)
		git->resetCommits(1);

	QApplication::restoreOverrideCursor();
	refreshRepo(false);
}

void MainImpl::ActCheckWorkDir_toggled(bool b) {

	if (!ActCheckWorkDir->isEnabled()) // to avoid looping with setChecked()
		return;

	setFlag(DIFF_INDEX_F, b);
	bool keepSelection = (rv->st.sha() != ZERO_SHA);
	refreshRepo(keepSelection);
}

void MainImpl::ActSettings_activated() {

	SettingsImpl setView(this, git);
	setView.exec();

	// update ActCheckWorkDir if necessary
	if (ActCheckWorkDir->isChecked() != testFlag(DIFF_INDEX_F))
		ActCheckWorkDir->toggle();
}

void MainImpl::ActCustomActionSetup_activated() {

	CustomActionImpl* ca = new CustomActionImpl(); // has Qt::WA_DeleteOnClose

	connect(this, SIGNAL(closeAllWindows()), ca, SLOT(close()));
	connect(ca, SIGNAL(listChanged(const QStringList&)),
	        this, SLOT(customActionListChanged(const QStringList&)));

	ca->show();
}

void MainImpl::customActionListChanged(const QStringList& list) {

	// update menu of all windows
	foreach (QWidget* widget, QApplication::topLevelWidgets()) {

		MainImpl* w = dynamic_cast<MainImpl*>(widget);
		if (w)
			w->doUpdateCustomActionMenu(list);
	}
}

void MainImpl::doUpdateCustomActionMenu(const QStringList& list) {

	while (Actions->idAt(1) != -1) // clear menu
		Actions->removeItemAt(1);

	if (list.isEmpty())
		return;

	Actions->insertSeparator();
	FOREACH_SL (it, list)
		Actions->insertItem(*it);
}

void MainImpl::customAction_activated(int id) {

	const QString actionName(Actions->text(id));
	if (actionName == "Setup actions...")
		return;

	QSettings set;
	if (!set.value(ACT_LIST_KEY).toStringList().contains(actionName)) {
		dbp("ASSERT in customAction_activated, action %1 not found", actionName);
		return;
	}
	QString cmdArgs;
	if (testFlag(ACT_CMD_LINE_F, ACT_GROUP_KEY + actionName + ACT_FLAGS_KEY)) {
		bool ok;
		cmdArgs = QInputDialog::getText("Run action - QGit", "Enter command line "
		          "arguments for '" + actionName + "'", QLineEdit::Normal, "", &ok, this);
		cmdArgs.prepend(' ');
		if (!ok)
			return;
	}
	SCRef cmd = set.value(ACT_GROUP_KEY + actionName + ACT_TEXT_KEY).toString();
	if (cmd.isEmpty())
		return;

	ConsoleImpl* c = new ConsoleImpl(actionName, git); // has Qt::WA_DeleteOnClose attribute

	connect(this, SIGNAL(closeAllWindows()), c, SLOT(close()));
	connect(c, SIGNAL(customAction_exited(const QString&)),
	        this, SLOT(customAction_exited(const QString&)));

	if (c->start(cmd, cmdArgs))
		c->show();
}

void MainImpl::customAction_exited(const QString& name) {

	const QString flags(ACT_GROUP_KEY + name + ACT_FLAGS_KEY);
	if (testFlag(ACT_REFRESH_F, flags))
		QTimer::singleShot(10, this, SLOT(refreshRepo())); // outside of event handler
}

void MainImpl::ActCommit_activated() {

	CommitImpl* c = new CommitImpl(git); // has Qt::WA_DeleteOnClose attribute
	connect(this, SIGNAL(closeAllWindows()), c, SLOT(close()));
	connect(c, SIGNAL(changesCommitted(bool)), this, SLOT(changesCommitted(bool)));
	c->show();
}

void MainImpl::changesCommitted(bool ok) {

	if (ok)
		refreshRepo(false);
	else
		statusBar()->message("Failed to commit changes");
}

void MainImpl::ActCommit_setEnabled(bool b) {

	// pop and push commands fail if there are local changes,
	// so in this case we disable ActPop and ActPush
	if (b) {
		ActPush->setEnabled(false);
		ActPop->setEnabled(false);
	}
	ActCommit->setEnabled(b);
}

void MainImpl::ActTag_activated() {

	QString tag(rv->tab()->listViewLog->currentText(LOG_COL));
	bool ok;
	tag = QInputDialog::getText("Make tag - QGit", "Enter tag name:",
	                            QLineEdit::Normal, tag, &ok, this);
	if (!ok || tag.isEmpty())
		return;

	QString tmp(tag.simplifyWhiteSpace());
	if (tag != tmp.remove(' ')) {
		QMessageBox::warning(this, "Make tag - QGit",
		             "Sorry, control characters or spaces\n"
		             "are not allowed in tag name.");
		return;
	}
	if (!git->getRefSha(tag, Git::TAG, false).isEmpty()) {
		QMessageBox::warning(this, "Make tag - QGit",
		             "Sorry, tag name already exists.\n"
		             "Please choose a different name.");
		return;
	}
	QString msg(QInputDialog::getText("Make tag - QGit",
	        "Enter tag message, if any:", QLineEdit::Normal, "", &ok, this));

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	ok = git->makeTag(lineEditSHA->text(), tag, msg);
	QApplication::restoreOverrideCursor();
	if (ok)
		refreshRepo(true);
	else
		statusBar()->message("Sorry, unable to tag the revision");
}

void MainImpl::ActTagDelete_activated() {

	if (QMessageBox::question(this, "Delete tag - QGit",
	                 "Do you want to un-tag selected revision?",
	                 "&Yes", "&No", QString(), 0, 1) == 1)
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	bool ok = git->deleteTag(lineEditSHA->text());
	QApplication::restoreOverrideCursor();
	if (ok)
		refreshRepo(true);
	else
		statusBar()->message("Sorry, unable to un-tag the revision");
}

void MainImpl::ActPush_activated() {

	QStringList selectedItems;
	rv->tab()->listViewLog->getSelectedItems(selectedItems);
	for (int i = 0; i < selectedItems.count(); i++) {
		if (!git->checkRef(selectedItems[i], Git::UN_APPLIED)) {
			statusBar()->message("Please, select only unapplied patches");
			return;
		}
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	bool ok = true;
	for (int i = 0; i < selectedItems.count(); i++) {
		const QString tmp(QString("Pushing patch %1 of %2")
		                  .arg(i+1).arg(selectedItems.count()));
		statusBar()->message(tmp);
		SCRef sha = selectedItems[selectedItems.count() - i - 1];
		if (!git->stgPush(sha)) {
			statusBar()->message("Failed to push patch " + sha);
			ok = false;
			break;
		}
	}
	if (ok)
		statusBar()->clear();

	QApplication::restoreOverrideCursor();
	refreshRepo(false);
}

void MainImpl::ActPop_activated() {

	QStringList selectedItems;
	rv->tab()->listViewLog->getSelectedItems(selectedItems);
	if (selectedItems.count() > 1) {
		statusBar()->message("Please, select one revision only");
		return;
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	git->stgPop(selectedItems[0]);
	QApplication::restoreOverrideCursor();
	refreshRepo(false);
}

void MainImpl::ActFilterTree_toggled(bool b) {

	if (!ActFilterTree->isEnabled()) {
		dbs("ASSERT ActFilterTree_toggled while disabled");
		return;
	}
	if (b) {
		QStringList selectedItems;
		if (!treeView->isVisible())
			this->treeView->updateTree(); // force tree updating

		this->treeView->getTreeSelectedItems(selectedItems);
		if (selectedItems.count() == 0) {
			dbs("ASSERT tree filter action activated with no selected items");
			return;
		}
		statusBar()->message("Filter view on " + selectedItems.join(" "));
		setRepository(curDir, true, true, &selectedItems);
	} else
		refreshRepo(true);
}

void MainImpl::ActFindNext_activated() {

	QTextEdit* te = getCurrentTextEdit();
	if (!te || textToFind.isEmpty())
		return;

	bool endOfDocument = false;
	while (true) {
		if (te->find(textToFind, false, false))
			return;

		if (endOfDocument) {
			QMessageBox::warning(this, "Find text - QGit", "Text \"" +
			             textToFind + "\" not found!", QMessageBox::Ok, 0);
			return;
		}
		if (QMessageBox::question(this, "Find text - QGit", "End of document "
		    "reached\n\nDo you want to continue from beginning?", QMessageBox::Yes,
		    QMessageBox::No | QMessageBox::Escape) == QMessageBox::No)
			return;

		endOfDocument = true;
 		te->moveCursor(QTextCursor::Start);
	}
}

void MainImpl::ActFind_activated() {

	QTextEdit* te = getCurrentTextEdit();
	if (!te)
		return;

	QString def(textToFind);
	if (te->textCursor().hasSelection())
		def = te->textCursor().selectedText().section('\n', 0, 0);
	else
		te->moveCursor(QTextCursor::Start);

	bool ok;
	QString str(QInputDialog::getText("Find text - QGit", "Text to find:",
	                                  QLineEdit::Normal, def, &ok, this));
	if (!ok || str.isEmpty())
		return;

	textToFind = str; // update with valid data only
	ActFindNext_activated();
}

void MainImpl::ActHelp_activated() {

	QDialog* dlg = new QDialog();
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	Ui::HelpBase ui;
	ui.setupUi(dlg);
	ui.textEditHelp->setHtml(QString::fromLatin1(helpInfo)); // defined in help.h
	connect(this, SIGNAL(closeAllWindows()), dlg, SLOT(close()));
	dlg->show();
	dlg->raise();
}

void MainImpl::ActAbout_activated() {

	static const char* aboutMsg =
	"<center><p><b>QGit version " PACKAGE_VERSION "</b></p><br>"
	"<p>Copyright (c) 2005, 2006 Marco Costalba</p>"
	"<p>Use and redistribute under the "
	"terms of the GNU General Public License</p></center>";
	QMessageBox::about(this, "About QGit", QString::fromLatin1(aboutMsg));
}

void MainImpl::closeEvent(QCloseEvent* ce) {

	// lastWindowClosed() signal is emitted by close(), after sending
	// closeEvent(), so we need to close _here_ all secondary windows before
	// the close() method checks for lastWindowClosed flag to avoid missing
	// the signal and stay in the main loop forever, because lastWindowClosed()
	// signal is connected to qApp->quit()
	//
	// note that we cannot rely on setting 'this' parent in secondary windows
	// because when close() is called children are still alive and, finally,
	// when children are deleted, d'tor do not call close() anymore. So we miss
	// lastWindowClosed() signal in this case.
	emit closeAllWindows();
	hide();

	EM_RAISE(exExiting);

	if (!git->stop(Git::optSaveCache)) {
		// if not all processes have been deleted, there is
		// still some run() call not returned somewhere, it is
		// not safe to delete run() callers objects now
		QTimer::singleShot(100, this, SLOT(ActClose_activated()));
		return;
	}
	emit closeAllTabs();
	delete rv;
	QWidget::closeEvent(ce);
}

void MainImpl::ActClose_activated() {

	close();
}

void MainImpl::ActExit_activated() {

	qApp->closeAllWindows();
}
