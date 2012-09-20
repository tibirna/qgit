/*
	Description: qgit main view

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QCloseEvent>
#include <QDrag>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QWheelEvent>
#include "config.h" // defines PACKAGE_VERSION
#include "consoleimpl.h"
#include "commitimpl.h"
#include "common.h"
#include "customactionimpl.h"
#include "fileview.h"
#include "git.h"
#include "help.h"
#include "listview.h"
#include "mainimpl.h"
#include "patchview.h"
#include "rangeselectimpl.h"
#include "revdesc.h"
#include "revsview.h"
#include "settingsimpl.h"
#include "treeview.h"
#include "ui_help.h"
#include "ui_revsview.h"
#include "ui_fileview.h"
#include "ui_patchview.h"

using namespace QGit;

MainImpl::MainImpl(SCRef cd, QWidget* p) : QMainWindow(p) {

	EM_INIT(exExiting, "Exiting");

	setAttribute(Qt::WA_DeleteOnClose);
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

	// our interface to git world
	git = new Git(this);
	setupShortcuts();
	qApp->installEventFilter(this);

	// init native types
	setRepositoryBusy = false;

	// init filter match highlighters
	shortLogRE.setMinimal(true);
	shortLogRE.setCaseSensitivity(Qt::CaseInsensitive);
	longLogRE.setMinimal(true);
	longLogRE.setCaseSensitivity(Qt::CaseInsensitive);

	// set-up standard revisions and files list font
	QSettings settings;
	QString font(settings.value(STD_FNT_KEY).toString());
	if (font.isEmpty())
		font = QApplication::font().toString();
	QGit::STD_FONT.fromString(font);

	// set-up typewriter (fixed width) font
	font = settings.value(TYPWRT_FNT_KEY).toString();
	if (font.isEmpty()) { // choose a sensible default
		QFont fnt = QApplication::font();
		fnt.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
		fnt.setFixedPitch(true);
		fnt.setFamily(fnt.defaultFamily()); // the family corresponding
		font = fnt.toString();              // to current style hint
	}
	QGit::TYPE_WRITER_FONT.fromString(font);

	// set-up tab view
	delete tabWdg->currentWidget(); // cannot be done in Qt Designer
	rv = new RevsView(this, git, true); // set has main domain
	tabWdg->addTab(rv->tabPage(), "&Rev list");

	// set-up tab corner widget ('close tab' button)
	QToolButton* ct = new QToolButton(tabWdg);
	ct->setIcon(QIcon(QString::fromUtf8(":/icons/resources/tab_remove.png")));
	ct->setToolTip("Close tab");
	ct->setEnabled(false);
	tabWdg->setCornerWidget(ct);
	connect(ct, SIGNAL(clicked()), this, SLOT(pushButtonCloseTab_clicked()));
	connect(this, SIGNAL(closeTabButtonEnabled(bool)), ct, SLOT(setEnabled(bool)));

	// set-up file names loading progress bar
	pbFileNamesLoading = new QProgressBar(statusBar());
	pbFileNamesLoading->setTextVisible(false);
	pbFileNamesLoading->setToolTip("Background file names loading");
	pbFileNamesLoading->hide();
	statusBar()->addPermanentWidget(pbFileNamesLoading);

	QVector<QSplitter*> v(1, treeSplitter);
	QGit::restoreGeometrySetting(QGit::MAIN_GEOM_KEY, this, &v);
	treeView->hide();

	// set-up menu for recent visited repositories
	connect(File, SIGNAL(triggered(QAction*)), this, SLOT(openRecent_triggered(QAction*)));
	doUpdateRecentRepoMenu("");

	// set-up menu for custom actions
	connect(Actions, SIGNAL(triggered(QAction*)), this, SLOT(customAction_triggered(QAction*)));
	doUpdateCustomActionMenu(settings.value(ACT_LIST_KEY).toStringList());

	// manual adjust lineEditSHA width
	QString tmp(41, '8');
	int wd = lineEditSHA->fontMetrics().boundingRect(tmp).width();
	lineEditSHA->setMinimumWidth(wd);

	// disable all actions
	updateGlobalActions(false);

	connect(git, SIGNAL(fileNamesLoad(int, int)), this, SLOT(fileNamesLoad(int, int)));

	connect(git, SIGNAL(newRevsAdded(const FileHistory*, const QVector<ShaString>&)),
	        this, SLOT(newRevsAdded(const FileHistory*, const QVector<ShaString>&)));

	connect(this, SIGNAL(typeWriterFontChanged()), this, SIGNAL(updateRevDesc()));

	connect(this, SIGNAL(changeFont(const QFont&)), git, SIGNAL(changeFont(const QFont&)));

	// connect cross-domain update signals
	connect(rv->tab()->listViewLog, SIGNAL(doubleClicked(const QModelIndex&)),
	        this, SLOT(listViewLog_doubleClicked(const QModelIndex&)));

	connect(rv->tab()->fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
	        this, SLOT(fileList_itemDoubleClicked(QListWidgetItem*)));

	connect(treeView, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
	        this, SLOT(treeView_doubleClicked(QTreeWidgetItem*, int)));

	// use most recent repo as startup dir if it exists and user opted to do so
	QStringList recents(settings.value(REC_REP_KEY).toStringList());
	QDir checkRepo;
	if (	recents.size() >= 1
		 && testFlag(REOPEN_REPO_F, FLAGS_KEY)
		 && checkRepo.exists(recents.at(0)))
	{
		startUpDir = recents.at(0);
	}
	else {
		startUpDir = (cd.isEmpty() ? QDir::current().absolutePath() : cd);
	}

	// MainImpl c'tor is called before to enter event loop,
	// but some stuff requires event loop to init properly
	QTimer::singleShot(10, this, SLOT(initWithEventLoopActive()));
}

void MainImpl::initWithEventLoopActive() {

	git->checkEnvironment();
	setRepository(startUpDir);
	startUpDir = ""; // one shot
}

void MainImpl::saveCurrentGeometry() {

	QVector<QSplitter*> v(1, treeSplitter);
	QGit::saveGeometrySetting(QGit::MAIN_GEOM_KEY, this, &v);
}

void MainImpl::highlightAbbrevSha(SCRef abbrevSha) {
	// reset any previous highlight
	if (ActSearchAndHighlight->isChecked())
		ActSearchAndHighlight->toggle();

	// set to highlight on SHA matching
	cmbSearch->setCurrentIndex(CS_SHA1);

	// set substring to search for
	lineEditFilter->setText(abbrevSha);

	// go with highlighting
	ActSearchAndHighlight->toggle();
}

void MainImpl::lineEditSHA_returnPressed() {

	QString sha = git->getRefSha(lineEditSHA->text());
	if (!sha.isEmpty()) // good, we can resolve to an unique sha
	{
		rv->st.setSha(sha);
		UPDATE_DOMAIN(rv);
	} else { // try a multiple match search
		highlightAbbrevSha(lineEditSHA->text());
		goMatch(0);
	}
}

void MainImpl::ActBack_activated() {

	lineEditSHA->undo(); // first for insert(text)
	if (lineEditSHA->text().isEmpty())
		lineEditSHA->undo(); // double undo, see RevsView::updateLineEditSHA()

	lineEditSHA_returnPressed();
}

void MainImpl::ActForward_activated() {

	lineEditSHA->redo();
	if (lineEditSHA->text().isEmpty())
		lineEditSHA->redo();

	lineEditSHA_returnPressed();
}

// *************************** ExternalDiffViewer ***************************

void MainImpl::ActExternalDiff_activated() {

	QStringList args;
	QStringList filenames;
	getExternalDiffArgs(&args, &filenames);
	ExternalDiffProc* externalDiff = new ExternalDiffProc(filenames, this);
	externalDiff->setWorkingDirectory(curDir);

	if (!QGit::startProcess(externalDiff, args)) {
		QString text("Cannot start external viewer: ");
		text.append(args[0]);
		QMessageBox::warning(this, "Error - QGit", text);
		delete externalDiff;
	}
}

void MainImpl::getExternalDiffArgs(QStringList* args, QStringList* filenames) {

	// save files to diff in working directory,
	// will be removed by ExternalDiffProc on exit
	QFileInfo f(rv->st.fileName());
	QString prevRevSha(rv->st.diffToSha());
	if (prevRevSha.isEmpty()) { // default to first parent
		const Rev* r = git->revLookup(rv->st.sha());
		prevRevSha = (r && r->parentsCount() > 0 ? r->parent(0) : rv->st.sha());
	}
	QFileInfo fi(f);
	QString fName1(curDir + "/" + rv->st.sha().left(6) + "_" + fi.fileName());
	QString fName2(curDir + "/" + prevRevSha.left(6) + "_" + fi.fileName());

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	QByteArray fileContent;
	QString fileSha(git->getFileSha(rv->st.fileName(), rv->st.sha()));
	git->getFile(fileSha, NULL, &fileContent, rv->st.fileName());
	if (!writeToFile(fName1, QString(fileContent)))
		statusBar()->showMessage("Unable to save " + fName1);

	fileSha = git->getFileSha(rv->st.fileName(), prevRevSha);
	git->getFile(fileSha, NULL, &fileContent, rv->st.fileName());
	if (!writeToFile(fName2, QString(fileContent)))
		statusBar()->showMessage("Unable to save " + fName2);

	// get external diff viewer command
	QSettings settings;
	QString extDiff(settings.value(EXT_DIFF_KEY, EXT_DIFF_DEF).toString());

	QApplication::restoreOverrideCursor();

	// if command doesn't have %1 and %2 to denote filenames, add them to end
	if (!extDiff.contains("%1")) {
		extDiff.append(" %1");
	}
	if (!extDiff.contains("%2")) {
		extDiff.append(" %2");
	}

	// set process arguments
	QStringList extDiffArgs = extDiff.split(' ');
	QString curArg;
	for (int i = 0; i < extDiffArgs.count(); i++) {
		curArg = extDiffArgs.value(i);

		// perform any filename replacements that are necessary
		// (done inside the loop to handle whitespace in paths properly)
		curArg.replace("%1", fName2);
		curArg.replace("%2", fName1);

		args->append(curArg);
	}

	// set filenames so that they can be deleted when the process completes
	filenames->append(fName1);
	filenames->append(fName2);
}

// ********************** Repository open or changed *************************

void MainImpl::setRepository(SCRef newDir, bool refresh, bool keepSelection,
                             const QStringList* passedArgs, bool overwriteArgs) {

	/*
	   Because Git::init calls processEvents(), if setRepository() is called in
	   a tight loop (as example keeping pressed F5 refresh button) then a lot
	   of pending init() calls would be stacked.
	   On returning from processEvents() an exception is trown and init is exited,
	   so we end up with a long list of 'exception thrown' messages.
	   But the worst thing is that we have to wait for _all_ the init call to exit
           and this could take a long time as example in case of working directory refreshing
	   'git update-index' of a big tree.
	   So we use a guard flag to guarantee we have only one init() call 'in flight'
	*/
	if (setRepositoryBusy)
		return;

	setRepositoryBusy = true;

	// check for a refresh or open of a new repository while in filtered view
	if (ActFilterTree->isChecked() && passedArgs == NULL)
		// toggle() triggers a refresh and a following setRepository()
		// call that is filtered out by setRepositoryBusy guard flag
		ActFilterTree->toggle(); // triggers ActFilterTree_toggled()

	try {
		EM_REGISTER(exExiting);

		bool archiveChanged;
		curDir = git->getBaseDir(&archiveChanged, newDir);

		git->stop(archiveChanged); // stop all pending processes, non blocking

		if (archiveChanged && refresh)
			dbs("ASSERT in setRepository: different dir with no range select");

		// now we can clear all our data
		setWindowTitle(curDir + " - QGit");
		bool complete = !refresh || !keepSelection;
		rv->clear(complete);
		if (archiveChanged)
			emit closeAllTabs();

		// disable all actions
		updateGlobalActions(false);
		updateContextActions("", "", false, false);
		ActCommit_setEnabled(false);

		if (ActFilterTree->isChecked())
			setWindowTitle(windowTitle() + " - FILTER ON < " +
			               passedArgs->join(" ") + " >");

		// tree name should be set before init because in case of
		// StGIT archives the first revs are sent before init returns
		QString n(curDir);
		treeView->setTreeName(n.prepend('/').section('/', -1, -1));

		bool quit;
		bool ok = git->init(curDir, !refresh, passedArgs, overwriteArgs, &quit); // blocking call
		if (quit)
			goto exit;

		updateCommitMenu(ok && git->isStGITStack());
		ActCheckWorkDir->setChecked(testFlag(DIFF_INDEX_F)); // could be changed in Git::init()

		if (ok) {
			updateGlobalActions(true);
			if (archiveChanged)
				updateRecentRepoMenu(curDir);
		} else
			statusBar()->showMessage("Not a git archive");

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
	ActBranch->setEnabled(found && (newRevSha != ZERO_SHA) && !isUnApplied);
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

	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		break;
	case TAB_PATCH:
		t->deleteWhenDone();
		ActViewDiffNewTab->setEnabled(ActViewDiff->isEnabled() && firstTab<PatchView>());
		break;
	case TAB_FILE:
		t->deleteWhenDone();
		ActViewFileNewTab->setEnabled(ActViewFile->isEnabled() && firstTab<FileView>());
		break;
	default:
		dbs("ASSERT in pushButtonCloseTab_clicked: unknown current page");
		break;
	}
}

void MainImpl::ActRangeDlg_activated() {

	QString args;
	RangeSelectImpl rs(this, &args, false, git);
	bool quit = (rs.exec() == QDialog::Rejected); // modal execution
	if (!quit) {
		const QStringList l(args.split(" "));
		setRepository(curDir, true, true, &l, true);
	}
}

void MainImpl::ActViewRev_activated() {

	Domain* t;
	if (currentTabType(&t) == TAB_FILE) {
		rv->st = t->st;
		UPDATE_DOMAIN(rv);
	}
	tabWdg->setCurrentWidget(rv->tabPage());
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
		tabWdg->addTab(fv->tabPage(), "File");

		connect(fv->tab()->histListView, SIGNAL(doubleClicked(const QModelIndex&)),
		        this, SLOT(histListView_doubleClicked(const QModelIndex&)));

		connect(this, SIGNAL(closeAllTabs()), fv, SLOT(on_closeAllTabs()));

		ActViewFileNewTab->setEnabled(ActViewFile->isEnabled());
	}
	tabWdg->setCurrentWidget(fv->tabPage());
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

	if (ActSearchAndFilter->isChecked() || ActSearchAndHighlight->isChecked()) {
		bool isRegExp = (cmbSearch->currentIndex() == CS_PATCH_REGEXP);
		emit highlightPatch(lineEditFilter->text(), isRegExp);
	}
}

void MainImpl::ActViewDiffNewTab_activated() {

	rv->viewPatch(true);
}

bool MainImpl::eventFilter(QObject* obj, QEvent* ev) {

	if (ev->type() == QEvent::Wheel) {

		QWheelEvent* e = static_cast<QWheelEvent*>(ev);
		if (e->modifiers() == Qt::AltModifier) {

			int idx = tabWdg->currentIndex();
			if (e->delta() < 0)
				idx = (++idx == tabWdg->count() ? 0 : idx);
			else
				idx = (--idx < 0 ? tabWdg->count() - 1 : idx);

			tabWdg->setCurrentIndex(idx);
			return true;
		}
	}
	return QWidget::eventFilter(obj, ev);
}

void MainImpl::revisionsDragged(SCList selRevs) {

	const QString h(QString::fromLatin1("@") + curDir + '\n');
	const QString dragRevs = selRevs.join(h).append(h).trimmed();
	QDrag* drag = new QDrag(this);
	QMimeData* mimeData = new QMimeData;
	mimeData->setText(dragRevs);
	drag->setMimeData(mimeData);
	drag->start(); // blocking until drop event
}

void MainImpl::revisionsDropped(SCList remoteRevs) {
// remoteRevs is already sanity checked to contain some possible valid data

	if (rv->isDropping()) // avoid reentrancy
		return;

	QDir dr(curDir + QGit::PATCHES_DIR);
	if (dr.exists()) {
		const QString tmp("Please remove stale import directory " + dr.absolutePath());
		statusBar()->showMessage(tmp);
		return;
	}
	bool workDirOnly, fold;
	if (!askApplyPatchParameters(&workDirOnly, &fold))
		return;

	// ok, let's go
	rv->setDropping(true);
	dr.setFilter(QDir::Files);
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	raise();
	EM_PROCESS_EVENTS;

	uint revNum = 0;
	QStringList::const_iterator it(remoteRevs.constEnd());
	do {
		--it;

		QString tmp("Importing revision %1 of %2");
		statusBar()->showMessage(tmp.arg(++revNum).arg(remoteRevs.count()));

		SCRef sha((*it).section('@', 0, 0));
		SCRef remoteRepo((*it).section('@', 1));

		if (!dr.exists(remoteRepo))
			break;

		// we create patches one by one
		if (!git->formatPatch(QStringList(sha), dr.absolutePath(), remoteRepo))
			break;

		dr.refresh();
		if (dr.count() != 1) {
			qDebug("ASSERT in on_droppedRevisions: found %i files "
			       "in %s", dr.count(), QGit::PATCHES_DIR.toLatin1().constData());
			break;
		}
		SCRef fn(dr.absoluteFilePath(dr[0]));
		bool is_applied = git->applyPatchFile(fn, fold, Git::optDragDrop);
		dr.remove(fn);
		if (!is_applied)
			break;

	} while (it != remoteRevs.constBegin());

	if (it == remoteRevs.constBegin())
		statusBar()->clearMessage();
	else
		statusBar()->showMessage("Failed to import revision " + QString::number(revNum--));

	if (workDirOnly && (revNum > 0))
		git->resetCommits(revNum);

	dr.rmdir(dr.absolutePath()); // 'dr' must be already empty
	QApplication::restoreOverrideCursor();
	rv->setDropping(false);
	refreshRepo();
}

// ******************************* Filter ******************************

void MainImpl::newRevsAdded(const FileHistory* fh, const QVector<ShaString>&) {

	if (!git->isMainHistory(fh))
		return;

	if (ActSearchAndFilter->isChecked())
		ActSearchAndFilter_toggled(true); // filter again on new arrived data

	if (ActSearchAndHighlight->isChecked())
		ActSearchAndHighlight_toggled(true); // filter again on new arrived data

	// first rev could be a StGIT unapplied patch so check more then once
	if (   !ActCommit->isEnabled()
	    && (!git->isNothingToCommit() || git->isUnknownFiles())
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

	ShaSet shaSet;
	bool patchNeedsUpdate, isRegExp;
	patchNeedsUpdate = isRegExp = false;
	int idx = cmbSearch->currentIndex(), colNum = 0;
	if (isOn) {
		switch (idx) {
		case CS_SHORT_LOG:
			colNum = LOG_COL;
			shortLogRE.setPattern(filter);
			break;
		case CS_LOG_MSG:
			colNum = LOG_MSG_COL;
			longLogRE.setPattern(filter);
			break;
		case CS_AUTHOR:
			colNum = AUTH_COL;
			break;
		case CS_SHA1:
			colNum = COMMIT_COL;
			break;
		case CS_FILE:
		case CS_PATCH:
		case CS_PATCH_REGEXP:
			colNum = SHA_MAP_COL;
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			EM_PROCESS_EVENTS; // to paint wait cursor
			if (idx == CS_FILE)
				git->getFileFilter(filter, shaSet);
			else {
				isRegExp = (idx == CS_PATCH_REGEXP);
				if (!git->getPatchFilter(filter, isRegExp, shaSet)) {
					QApplication::restoreOverrideCursor();
					ActSearchAndFilter->toggle();
					return;
				}
				patchNeedsUpdate = (shaSet.count() > 0);
			}
			QApplication::restoreOverrideCursor();
			break;
		}
	} else {
		patchNeedsUpdate = (idx == CS_PATCH || idx == CS_PATCH_REGEXP);
		shortLogRE.setPattern("");
		longLogRE.setPattern("");
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	ListView* lv = rv->tab()->listViewLog;
	int matchedCnt = lv->filterRows(isOn, onlyHighlight, filter, colNum, &shaSet);

	QApplication::restoreOverrideCursor();

	emit updateRevDesc(); // could be highlighted
	if (patchNeedsUpdate)
		emit highlightPatch(isOn ? filter : "", isRegExp);

	QString msg;
	if (isOn && !onlyHighlight)
		msg = QString("Found %1 matches. Toggle filter/highlight "
		              "button to remove the filter").arg(matchedCnt);
	QApplication::postEvent(rv, new MessageEvent(msg)); // deferred message, after update
}

bool MainImpl::event(QEvent* e) {

	BaseEvent* de = dynamic_cast<BaseEvent*>(e);
	if (!de)
		return QWidget::event(e);

	SCRef data = de->myData();
	bool ret = true;

        switch ((EventType)e->type()) {
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
		statusBar()->showMessage(data);
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
	QWidget* curPage = tabWdg->currentWidget();
	if (curPage == rv->tabPage()) {
		*t = rv;
		return TAB_REV;
	}
	QList<PatchView*>* l = getTabs<PatchView>(curPage);
	if (l->count() > 0) {
		*t = l->first();
		delete l;
		return TAB_PATCH;
	}
	delete l;
	QList<FileView*>* l2 = getTabs<FileView>(curPage);
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

template<class X> QList<X*>* MainImpl::getTabs(QWidget* tabPage) {

	QList<X*> l = this->findChildren<X*>();
	QList<X*>* ret = new QList<X*>;

	for (int i = 0; i < l.size(); ++i) {
		if (!tabPage || l.at(i)->tabPage() == tabPage)
			ret->append(l.at(i));
	}
	return ret; // 'ret' must be deleted by caller
}

template<class X> X* MainImpl::firstTab(QWidget* startPage) {

	int minVal = 99, firstVal = 99;
	int startPos = tabWdg->indexOf(startPage);
	X* min = NULL;
	X* first = NULL;
	QList<X*>* l = getTabs<X>();
	for (int i = 0; i < l->size(); ++i) {

		X* d = l->at(i);
		int idx = tabWdg->indexOf(d->tabPage());
		if (idx < minVal) {
			minVal = idx;
			min = d;
		}
		if (idx < firstVal && idx > startPos) {
			firstVal = idx;
			first = d;
		}
	}
	delete l;
	return (first ? first : min);
}

void MainImpl::tabWdg_currentChanged(int w) {

	if (w == -1)
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

void MainImpl::setupShortcuts() {

	new QShortcut(Qt::Key_I,     this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_K,     this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_N,     this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_Left,  this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_Right, this, SLOT(shortCutActivated()));

	new QShortcut(Qt::Key_Delete,    this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_Backspace, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_Space,     this, SLOT(shortCutActivated()));

	new QShortcut(Qt::Key_B, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_D, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_F, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_P, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_R, this, SLOT(shortCutActivated()));
	new QShortcut(Qt::Key_U, this, SLOT(shortCutActivated()));

	new QShortcut(Qt::SHIFT | Qt::Key_Up,    this, SLOT(shortCutActivated()));
	new QShortcut(Qt::SHIFT | Qt::Key_Down,  this, SLOT(shortCutActivated()));
	new QShortcut(Qt::CTRL  | Qt::Key_Plus,  this, SLOT(shortCutActivated()));
	new QShortcut(Qt::CTRL  | Qt::Key_Minus, this, SLOT(shortCutActivated()));
}

void MainImpl::shortCutActivated() {

	QShortcut* se = dynamic_cast<QShortcut*>(sender());
	if (!se)
		return;

	bool isKey_P = false;

	switch (se->key()) {

	case Qt::Key_I:
		rv->tab()->listViewLog->on_keyUp();
		break;
	case Qt::Key_K:
	case Qt::Key_N:
		rv->tab()->listViewLog->on_keyDown();
		break;
	case Qt::SHIFT | Qt::Key_Up:
		goMatch(-1);
		break;
	case Qt::SHIFT | Qt::Key_Down:
		goMatch(1);
		break;
	case Qt::Key_Left:
		ActBack_activated();
		break;
	case Qt::Key_Right:
		ActForward_activated();
		break;
	case Qt::CTRL | Qt::Key_Plus:
		adjustFontSize(1);
		break;
	case Qt::CTRL | Qt::Key_Minus:
		adjustFontSize(-1);
		break;
	case Qt::Key_U:
		scrollTextEdit(-18);
		break;
	case Qt::Key_D:
		scrollTextEdit(18);
		break;
	case Qt::Key_Delete:
	case Qt::Key_B:
	case Qt::Key_Backspace:
		scrollTextEdit(-1);
		break;
	case Qt::Key_Space:
		scrollTextEdit(1);
		break;
	case Qt::Key_R:
		tabWdg->setCurrentWidget(rv->tabPage());
		break;
	case Qt::Key_P:
		isKey_P = true;
	case Qt::Key_F: {
		QWidget* cp = tabWdg->currentWidget();
		Domain* d = isKey_P ? static_cast<Domain*>(firstTab<PatchView>(cp)) :
		                      static_cast<Domain*>(firstTab<FileView>(cp));
		if (d)
			tabWdg->setCurrentWidget(d->tabPage()); }
		break;
	}
}

void MainImpl::goMatch(int delta) {

	if (ActSearchAndHighlight->isChecked())
		rv->tab()->listViewLog->scrollToNextHighlighted(delta);
}

QTextEdit* MainImpl::getCurrentTextEdit() {

	QTextEdit* te = NULL;
	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV:
		te = static_cast<RevsView*>(t)->tab()->textBrowserDesc;
		if (!te->isVisible())
			te = static_cast<RevsView*>(t)->tab()->textEditDiff;
		break;
	case TAB_PATCH:
		te = static_cast<PatchView*>(t)->tab()->textEditDiff;
		break;
	case TAB_FILE:
		te = static_cast<FileView*>(t)->tab()->textEditFile;
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

void MainImpl::adjustFontSize(int delta) {
// font size is changed on a 'per instance' base and only on list views

	int ps = QGit::STD_FONT.pointSize() + delta;
	if (ps < 2)
		return;

	QGit::STD_FONT.setPointSize(ps);

	QSettings settings;
	settings.setValue(QGit::STD_FNT_KEY, QGit::STD_FONT.toString());
	emit changeFont(QGit::STD_FONT);
}

void MainImpl::fileNamesLoad(int status, int value) {

	switch (status) {
	case 1: // stop
		pbFileNamesLoading->hide();
		break;
	case 2: // update
		pbFileNamesLoading->setValue(value);
		break;
	case 3: // start
		if (value > 200) { // don't show for few revisions
			pbFileNamesLoading->reset();
			pbFileNamesLoading->setMaximum(value);
			pbFileNamesLoading->show();
		}
		break;
	}
}

// ****************************** Menu *********************************

void MainImpl::updateCommitMenu(bool isStGITStack) {

	ActCommit->setText(isStGITStack ? "Commit St&GIT patch..." : "&Commit...");
	ActAmend->setText(isStGITStack ? "Refresh St&GIT patch..." : "&Amend commit...");
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

	QList<QAction*> al(File->actions());
	FOREACH (QList<QAction*>, it, al) {
		SCRef txt = (*it)->text();
		if (!txt.isEmpty() && txt.at(0).isDigit())
			File->removeAction(*it);
	}
	QSettings settings;
	QStringList recents(settings.value(REC_REP_KEY).toStringList());
	int idx = recents.indexOf(newEntry);
	if (idx != -1)
		recents.removeAt(idx);

	if (!newEntry.isEmpty())
		recents.prepend(newEntry);

	idx = 1;
	QStringList newRecents;
	FOREACH_SL (it, recents) {
		File->addAction(QString::number(idx++) + " " + *it);
		newRecents << *it;
		if (idx > MAX_RECENT_REPOS)
			break;
	}
	settings.setValue(REC_REP_KEY, newRecents);
}

static int cntMenuEntries(const QMenu& menu) {

	int cnt = 0;
	QList<QAction*> al(menu.actions());
	FOREACH (QList<QAction*>, it, al) {
		if (!(*it)->isSeparator())
			cnt++;
	}
	return cnt;
}

void MainImpl::doContexPopup(SCRef sha) {

	QMenu contextMenu(this);
	QMenu contextBrnMenu("More branches...", this);
	QMenu contextTagMenu("More tags...", this);
	QMenu contextRmtMenu("Remote branches", this);

	connect(&contextMenu, SIGNAL(triggered(QAction*)), this, SLOT(goRef_triggered(QAction*)));

	Domain* t;
	int tt = currentTabType(&t);
	bool isRevPage = (tt == TAB_REV);
	bool isPatchPage = (tt == TAB_PATCH);
	bool isFilePage = (tt == TAB_FILE);

	if (!isFilePage && ActCheckWorkDir->isEnabled()) {
		contextMenu.addAction(ActCheckWorkDir);
		contextMenu.addSeparator();
	}
	if (isFilePage && ActViewRev->isEnabled())
		contextMenu.addAction(ActViewRev);

	if (!isPatchPage && ActViewDiff->isEnabled())
		contextMenu.addAction(ActViewDiff);

	if (isRevPage && ActViewDiffNewTab->isEnabled())
		contextMenu.addAction(ActViewDiffNewTab);

	if (!isFilePage && ActExternalDiff->isEnabled())
		contextMenu.addAction(ActExternalDiff);

	if (isRevPage) {
		if (ActCommit->isEnabled() && (sha == ZERO_SHA))
			contextMenu.addAction(ActCommit);
		if (ActBranch->isEnabled())
			contextMenu.addAction(ActBranch);
		if (ActTag->isEnabled())
			contextMenu.addAction(ActTag);
		if (ActTagDelete->isEnabled())
			contextMenu.addAction(ActTagDelete);
		if (ActMailFormatPatch->isEnabled())
			contextMenu.addAction(ActMailFormatPatch);
		if (ActPush->isEnabled())
			contextMenu.addAction(ActPush);
		if (ActPop->isEnabled())
			contextMenu.addAction(ActPop);

		const QStringList& bn(git->getAllRefNames(Git::BRANCH, Git::optOnlyLoaded));
		const QStringList& rbn(git->getAllRefNames(Git::RMT_BRANCH, Git::optOnlyLoaded));
		const QStringList& tn(git->getAllRefNames(Git::TAG, Git::optOnlyLoaded));
		QAction* act = NULL;

		FOREACH_SL (it, rbn) {
			act = contextRmtMenu.addAction(*it);
			act->setData("Ref");
		}
		if (!contextRmtMenu.isEmpty())
			contextMenu.addMenu(&contextRmtMenu);

		// halve the possible remaining entries for branches and tags
		int remainingEntries = (MAX_MENU_ENTRIES - cntMenuEntries(contextMenu));
		int tagEntries = remainingEntries / 2;
		int brnEntries = remainingEntries - tagEntries;

		// display more branches, if there are few tags
		if (tagEntries > tn.count())
			tagEntries = tn.count();

		// one branch less because of the "More branches..." submenu
		if ((bn.count() > brnEntries) && tagEntries)
			tagEntries++;

		if (!bn.empty())
			contextMenu.addSeparator();

		FOREACH_SL (it, bn) {
			if (   cntMenuEntries(contextMenu) < MAX_MENU_ENTRIES - tagEntries
			    || (*it == bn.last() && contextBrnMenu.isEmpty()))
				act = contextMenu.addAction(*it);
			else
				act = contextBrnMenu.addAction(*it);

			act->setData("Ref");
		}
		if (!contextBrnMenu.isEmpty())
			contextMenu.addMenu(&contextBrnMenu);

		if (!tn.empty())
			contextMenu.addSeparator();

		FOREACH_SL (it, tn) {
			if (   cntMenuEntries(contextMenu) < MAX_MENU_ENTRIES
			    || (*it == tn.last() && contextTagMenu.isEmpty()))
				act = contextMenu.addAction(*it);
			else
				act = contextTagMenu.addAction(*it);

			act->setData("Ref");
		}
		if (!contextTagMenu.isEmpty())
			contextMenu.addMenu(&contextTagMenu);
	}
	QPoint p = QCursor::pos();
	p += QPoint(10, 10);
	contextMenu.exec(p);
}

void MainImpl::doFileContexPopup(SCRef fileName, int type) {

	QMenu contextMenu(this);

	Domain* t;
	int tt = currentTabType(&t);
	bool isRevPage = (tt == TAB_REV);
	bool isPatchPage = (tt == TAB_PATCH);
	bool isDir = treeView->isDir(fileName);

	if (type == POPUP_FILE_EV)
		if (!isPatchPage && ActViewDiff->isEnabled())
			contextMenu.addAction(ActViewDiff);

	if (!isDir && ActViewFile->isEnabled())
		contextMenu.addAction(ActViewFile);

	if (!isDir && ActViewFileNewTab->isEnabled())
		contextMenu.addAction(ActViewFileNewTab);

	if (!isRevPage && (type == POPUP_FILE_EV) && ActViewRev->isEnabled())
		contextMenu.addAction(ActViewRev);

	if (ActFilterTree->isEnabled())
		contextMenu.addAction(ActFilterTree);

	if (!isDir) {
		if (ActSaveFile->isEnabled())
			contextMenu.addAction(ActSaveFile);
		if ((type == POPUP_FILE_EV) && ActExternalDiff->isEnabled())
			contextMenu.addAction(ActExternalDiff);
	}
	contextMenu.exec(QCursor::pos());
}

void MainImpl::goRef_triggered(QAction* act) {

	if (!act || act->data() != "Ref")
		return;

	SCRef refSha(git->getRefSha(act->text()));
	rv->st.setSha(refSha);
	UPDATE_DOMAIN(rv);
}

void MainImpl::ActSplitView_activated() {

	Domain* t;
	switch (currentTabType(&t)) {
	case TAB_REV: {
		RevsView* rv = static_cast<RevsView*>(t);
		QWidget* w = rv->tab()->fileList;
		QSplitter* sp = static_cast<QSplitter*>(w->parent());
		sp->setHidden(w->isVisible()); }
		break;
	case TAB_PATCH: {
		PatchView* pv = static_cast<PatchView*>(t);
		QWidget* w = pv->tab()->textBrowserDesc;
		w->setHidden(w->isVisible()); }
		break;
	case TAB_FILE: {
		FileView* fv = static_cast<FileView*>(t);
		QWidget* w = fv->tab()->histListView;
		w->setHidden(w->isVisible()); }
		break;
	default:
		dbs("ASSERT in ActSplitView_activated: unknown current page");
		break;
	}
}

void MainImpl::ActToggleLogsDiff_activated() {

	Domain* t;
	if (currentTabType(&t) == TAB_REV) {
		RevsView* rv = static_cast<RevsView*>(t);
		rv->toggleDiffView();
	}
}

const QString MainImpl::getRevisionDesc(SCRef sha) {

	bool showHeader = ActShowDescHeader->isChecked();
	return git->getDesc(sha, shortLogRE, longLogRE, showHeader, NULL);
}

void MainImpl::ActShowDescHeader_activated() {

	// each open tab get his description,
	// could be different for each tab
	emit updateRevDesc();
}

void MainImpl::ActShowTree_toggled(bool b) {

	if (b) {
		treeView->show();
		UPDATE_DOMAIN(rv);
	} else {
		saveCurrentGeometry();
		treeView->hide();
	}
}

void MainImpl::ActSaveFile_activated() {

	QFileInfo f(rv->st.fileName());
	const QString fileName(QFileDialog::getSaveFileName(this, "Save file as", f.fileName()));
	if (fileName.isEmpty())
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	QString fileSha(git->getFileSha(rv->st.fileName(), rv->st.sha()));
	if (!git->saveFile(fileSha, rv->st.fileName(), fileName))
		statusBar()->showMessage("Unable to save " + fileName);

	QApplication::restoreOverrideCursor();
}

void MainImpl::openRecent_triggered(QAction* act) {

	bool ok;
	act->text().left(1).toInt(&ok);
	if (!ok) // only recent repos entries have a number in first char
		return;

	const QString workDir(act->text().section(' ', 1));
	if (!workDir.isEmpty()) {
		QDir d(workDir);
		if (d.exists())
			setRepository(workDir);
		else
			statusBar()->showMessage("Directory '" + workDir +
			                         "' does not seem to exsist anymore");
	}
}

void MainImpl::ActOpenRepo_activated() {

	const QString dirName(QFileDialog::getExistingDirectory(this, "Choose a directory", curDir));
	if (!dirName.isEmpty()) {
		QDir d(dirName);
		setRepository(d.absolutePath());
	}
}

void MainImpl::ActOpenRepoNewWindow_activated() {

	const QString dirName(QFileDialog::getExistingDirectory(this, "Choose a directory", curDir));
	if (!dirName.isEmpty()) {
		QDir d(dirName);
		MainImpl* newWin = new MainImpl(d.absolutePath());
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
		statusBar()->showMessage("At least one selected revision needed");
		return;
	}
	if (selectedItems.contains(ZERO_SHA)) {
		statusBar()->showMessage("Unable to save a patch for not committed content");
		return;
	}
	QSettings settings;
	QString outDir(settings.value(PATCH_DIR_KEY, curDir).toString());
	QString dirPath(QFileDialog::getExistingDirectory(this,
	                "Choose destination directory - Save Patch", outDir));
	if (dirPath.isEmpty())
		return;

	QDir d(dirPath);
	settings.setValue(PATCH_DIR_KEY, d.absolutePath());
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	git->formatPatch(selectedItems, d.absolutePath());
	QApplication::restoreOverrideCursor();
}

bool MainImpl::askApplyPatchParameters(bool* workDirOnly, bool* fold) {

	int ret = 0;
	if (!git->isStGITStack()) {
		ret = QMessageBox::question(this, "Apply Patch",
		      "Do you want to commit or just to apply changes to "
                      "working directory?", "&Cancel", "&Working directory", "&Commit", 0, 0);
		*workDirOnly = (ret == 1);
		*fold = false;
	} else {
		ret = QMessageBox::question(this, "Apply Patch", "Do you want to "
		      "import or fold the patch?", "&Cancel", "&Fold", "&Import", 0, 0);
		*workDirOnly = false;
		*fold = (ret == 1);
	}
	return (ret != 0);
}

void MainImpl::ActMailApplyPatch_activated() {

	QSettings settings;
	QString outDir(settings.value(PATCH_DIR_KEY, curDir).toString());
	QString patchName(QFileDialog::getOpenFileName(this,
	                  "Choose the patch file - Apply Patch", outDir,
	                  "Patches (*.patch *.diff *.eml)\nAll Files (*.*)"));
	if (patchName.isEmpty())
		return;

	QFileInfo f(patchName);
	settings.setValue(PATCH_DIR_KEY, f.absolutePath());

	bool workDirOnly, fold;
	if (!askApplyPatchParameters(&workDirOnly, &fold))
		return;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	bool ok = git->applyPatchFile(f.absoluteFilePath(), fold, !Git::optDragDrop);
	if (workDirOnly && ok)
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
	connect(&setView, SIGNAL(typeWriterFontChanged()),
	        this, SIGNAL(typeWriterFontChanged()));

	connect(&setView, SIGNAL(flagChanged(uint)),
	        this, SIGNAL(flagChanged(uint)));

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

	QAction* setupAct = Actions->actions().first(); // is never empty
	Actions->removeAction(setupAct);
	Actions->clear();
	Actions->addAction(setupAct);

	if (list.isEmpty())
		return;

	Actions->addSeparator();
	FOREACH_SL (it, list)
		Actions->addAction(*it);
}

void MainImpl::customAction_triggered(QAction* act) {

	SCRef actionName = act->text();
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
		cmdArgs = QInputDialog::getText(this, "Run action - QGit", "Enter command line "
		          "arguments for '" + actionName + "'", QLineEdit::Normal, "", &ok);
		cmdArgs.prepend(' ');
		if (!ok)
			return;
	}
	SCRef cmd = set.value(ACT_GROUP_KEY + actionName + ACT_TEXT_KEY).toString();
	if (cmd.isEmpty())
		return;

	ConsoleImpl* c = new ConsoleImpl(actionName, git); // has Qt::WA_DeleteOnClose attribute

	connect(this, SIGNAL(typeWriterFontChanged()),
	        c, SLOT(typeWriterFontChanged()));

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

	CommitImpl* c = new CommitImpl(git, false); // has Qt::WA_DeleteOnClose attribute
	connect(this, SIGNAL(closeAllWindows()), c, SLOT(close()));
	connect(c, SIGNAL(changesCommitted(bool)), this, SLOT(changesCommitted(bool)));
	c->show();
}

void MainImpl::ActAmend_activated() {

	CommitImpl* c = new CommitImpl(git, true); // has Qt::WA_DeleteOnClose attribute
	connect(this, SIGNAL(closeAllWindows()), c, SLOT(close()));
	connect(c, SIGNAL(changesCommitted(bool)), this, SLOT(changesCommitted(bool)));
	c->show();
}

void MainImpl::changesCommitted(bool ok) {

	if (ok)
		refreshRepo(false);
	else
		statusBar()->showMessage("Failed to commit changes");
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

void MainImpl::ActBranch_activated() {

    doBranchOrTag(false);
}

void MainImpl::ActTag_activated() {

    doBranchOrTag(true);
}

void MainImpl::doBranchOrTag(bool isTag) {

	QString refDesc = isTag ? "tag" : "branch";
	QString boxDesc = "Make " + refDesc + " - QGit";
	QString revDesc(rv->tab()->listViewLog->currentText(LOG_COL));
	bool ok;
	QString ref = QInputDialog::getText(this, boxDesc, "Enter " + refDesc
								+ " name:", QLineEdit::Normal, "", &ok);
	if (!ok || ref.isEmpty())
		return;

	QString tmp(ref.trimmed());
	if (ref != tmp.remove(' ')) {
		QMessageBox::warning(this, boxDesc,
		             "Sorry, control characters or spaces\n"
		             "are not allowed in " + refDesc + " name.");
		return;
	}
	if (!git->getRefSha(ref, isTag ? Git::TAG : Git::BRANCH, false).isEmpty()) {
		QMessageBox::warning(this, boxDesc,
		             "Sorry, " + refDesc + " name already exists.\n"
					 "Please choose a different name.");
		return;
	}
	QString msg;
	if (isTag) {
	    msg = QInputDialog::getText(this, boxDesc, "Enter tag message, if any:",
									QLineEdit::Normal, revDesc, &ok);
		if (!ok) return;
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	if (isTag)
	    ok = git->makeTag(lineEditSHA->text(), ref, msg);
	else
	    ok = git->makeBranch(lineEditSHA->text(), ref);

	QApplication::restoreOverrideCursor();
	if (ok)
		refreshRepo(true);
	else
		statusBar()->showMessage("Sorry, unable to tag the revision");
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
		statusBar()->showMessage("Sorry, unable to un-tag the revision");
}

void MainImpl::ActPush_activated() {

	QStringList selectedItems;
	rv->tab()->listViewLog->getSelectedItems(selectedItems);
	for (int i = 0; i < selectedItems.count(); i++) {
		if (!git->checkRef(selectedItems[i], Git::UN_APPLIED)) {
			statusBar()->showMessage("Please, select only unapplied patches");
			return;
		}
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	bool ok = true;
	for (int i = 0; i < selectedItems.count(); i++) {
		const QString tmp(QString("Pushing patch %1 of %2")
		                  .arg(i+1).arg(selectedItems.count()));
		statusBar()->showMessage(tmp);
		SCRef sha = selectedItems[selectedItems.count() - i - 1];
		if (!git->stgPush(sha)) {
			statusBar()->showMessage("Failed to push patch " + sha);
			ok = false;
			break;
		}
	}
	if (ok)
		statusBar()->clearMessage();

	QApplication::restoreOverrideCursor();
	refreshRepo(false);
}

void MainImpl::ActPop_activated() {

	QStringList selectedItems;
	rv->tab()->listViewLog->getSelectedItems(selectedItems);
	if (selectedItems.count() > 1) {
		statusBar()->showMessage("Please, select one revision only");
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
			treeView->updateTree(); // force tree updating

		treeView->getTreeSelectedItems(selectedItems);
		if (selectedItems.count() == 0) {
			dbs("ASSERT tree filter action activated with no selected items");
			return;
		}
		statusBar()->showMessage("Filter view on " + selectedItems.join(" "));
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
		if (te->find(textToFind))
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
	QString str(QInputDialog::getText(this, "Find text - QGit", "Text to find:",
	                                  QLineEdit::Normal, def, &ok));
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
	"<p><b>QGit version " PACKAGE_VERSION "</b></p>"
	"<p>Copyright (c) 2005, 2007, 2008 Marco Costalba</p>"
	"<p>Use and redistribute under the terms of the<br>"
	"<a href=\"http://www.gnu.org/licenses/old-licenses/gpl-2.0.html\">GNU General Public License Version 2</a></p>"
	"<p>Contributors:<br>"
	"Copyright (c) 2007 Andy Parkins<br>"
	"Copyright (c) 2007 Pavel Roskin<br>"
	"Copyright (c) 2007 Peter Oberndorfer<br>"
	"Copyright (c) 2007 Yaacov Akiba<br>"
	"Copyright (c) 2007 James McKaskill<br>"
	"Copyright (c) 2008 Jan Hudec<br>"
	"Copyright (c) 2008 Paul Gideon Dann<br>"
	"Copyright (c) 2008 Oliver Bock<br>"
	"Copyright (c) 2010 Cyp &lt;cyp561@gmail.com&gt;<br>"
	"Copyright (c) 2011 Jean-François Dagenais &lt;dagenaisj@sonatest.com&gt;<br>"
	"Copyright (c) 2011 Pavel Tikhomirov &lt;pavtih@gmail.com&gt;<br>"
	"Copyright (c) 2011 Cristian Tibirna &lt;tibirna@kde.org&gt;<br>"
	"Copyright (c) 2011 Tim Blechmann &lt;tim@klingt.org&gt;"
    "</p>"

	"<p>This version was compiled against Qt " QT_VERSION_STR "</p>";
	QMessageBox::about(this, "About QGit", QString::fromLatin1(aboutMsg));
}

void MainImpl::closeEvent(QCloseEvent* ce) {

	saveCurrentGeometry();

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

	git->stop(Git::optSaveCache);

	if (!git->findChildren<QProcess*>().isEmpty()) {
		// if not all processes have been deleted, there is
		// still some run() call not returned somewhere, it is
		// not safe to delete run() callers objects now
		QTimer::singleShot(100, this, SLOT(ActClose_activated()));
		ce->ignore();
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
