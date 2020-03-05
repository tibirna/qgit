/*
	Description: qgit main view

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QCloseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QWheelEvent>
#include <QTextCodec>
#include <assert.h>
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
#include "inputdialog.h"
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
	if (font.isEmpty()) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,2,0))
		font = QFontDatabase::systemFont(QFontDatabase::GeneralFont).toString();
#else
		font = QApplication::font().toString();
#endif
	}
	QGit::STD_FONT.fromString(font);

	// set-up typewriter (fixed width) font
	font = settings.value(TYPWRT_FNT_KEY).toString();
	if (font.isEmpty()) { // choose a sensible default
#if (QT_VERSION >= QT_VERSION_CHECK(5,2,0))
		QFont fnt = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#else
		QFont fnt = QApplication::font();
		fnt.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
		fnt.setFixedPitch(true);
		fnt.setFamily(fnt.defaultFamily()); // the family corresponding
#endif
		font = fnt.toString();              // to current style hint
	}
	QGit::TYPE_WRITER_FONT.fromString(font);

	// set-up tab view
	delete tabWdg->currentWidget(); // cannot be done in Qt Designer
	rv = new RevsView(this, git, true); // set has main domain
	tabWdg->addTab(rv->tabPage(), "&Rev list");

	// hide close button for rev list tab
	QTabBar* const tabBar = tabWdg->tabBar();
	tabBar->setTabButton(0, QTabBar::RightSide, NULL);
	tabBar->setTabButton(0, QTabBar::LeftSide, NULL);
	connect(tabWdg, SIGNAL(tabCloseRequested(int)), SLOT(tabBar_tabCloseRequested(int)));

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
	connect(rv->tab()->listViewLog, SIGNAL(showStatusMessage(QString,int)),
	        statusBar(), SLOT(showMessage(QString,int)));

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

	// handle --view-file=* or --view-file * argument
	QStringList arglist = qApp->arguments();
	// Remove first argument which is the path of the current executable
	arglist.removeFirst();
	bool retainNext = false;
	foreach (QString arg, arglist) {
		if (retainNext) {
			retainNext = false;
			startUpFile = arg;
		} else if (arg == "--view-file")
			retainNext = true;
		else if (arg.startsWith("--view-file="))
			startUpFile = arg.mid(12);
	}

	// MainImpl c'tor is called before to enter event loop,
	// but some stuff requires event loop to init properly
	QTimer::singleShot(10, this, SLOT(initWithEventLoopActive()));
}

void MainImpl::initWithEventLoopActive() {

	git->checkEnvironment();
	setRepository(startUpDir);
	startUpDir = ""; // one shot

	// handle --view-file=* or --view-file * argument
	if (!startUpFile.isEmpty()) {
		rv->st.setSha("HEAD");
		rv->st.setFileName(startUpFile);
		openFileTab();
		startUpFile = QString(); // one shot
	}
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

const QRegExp MainImpl::emptySha("0*");

QString MainImpl::copyFileToDiffIfNeeded(QStringList* filenames, QString sha) {
	if (emptySha.exactMatch(sha))
	{
		return QString(curDir + "/" + rv->st.fileName());
	}

	QFileInfo f(rv->st.fileName());
	QFileInfo fi(f);

	QString fName(curDir + "/" + sha.left(6) + "_" + fi.fileName());

	QByteArray fileContent;
	QTextCodec* tc = QTextCodec::codecForLocale();

	QString fileSha(git->getFileSha(rv->st.fileName(), sha));
	git->getFile(fileSha, NULL, &fileContent, rv->st.fileName());
	if (!writeToFile(fName, tc->toUnicode(fileContent)))
	{
		statusBar()->showMessage("Unable to save " + fName);
	}

	filenames->append(fName);

	return fName;

}

void MainImpl::getExternalDiffArgs(QStringList* args, QStringList* filenames) {

	QString prevRevSha(rv->st.diffToSha());
	if (prevRevSha.isEmpty()) { // default to first parent
		const Rev* r = git->revLookup(rv->st.sha());
		prevRevSha = (r && r->parentsCount() > 0 ? r->parent(0) : rv->st.sha());
	}
	// save files to diff in working directory,
	// will be removed by ExternalDiffProc on exit
	QString fName1 = copyFileToDiffIfNeeded(filenames, rv->st.sha());
	QString fName2 = copyFileToDiffIfNeeded(filenames, prevRevSha);

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

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

}

// *************************** ExternalEditor ***************************

void MainImpl::ActExternalEditor_activated() {

	const QStringList &args = getExternalEditorArgs();
	ExternalEditorProc* externalEditor = new ExternalEditorProc(this);
	externalEditor->setWorkingDirectory(curDir);

	if (!QGit::startProcess(externalEditor, args)) {
		QString text("Cannot start external editor: ");
		text.append(args[0]);
		QMessageBox::warning(this, "Error - QGit", text);
		delete externalEditor;
	}
}

QStringList MainImpl::getExternalEditorArgs() {

	QString fName1(curDir + "/" + rv->st.fileName());

	// get external diff viewer command
	QSettings settings;
	QString extEditor(settings.value(EXT_EDITOR_KEY, EXT_EDITOR_DEF).toString());

	// if command doesn't have %1 to denote filename, add to end
	if (!extEditor.contains("%1")) extEditor.append(" %1");

	// set process arguments
	QStringList args = extEditor.split(' ');
	for (int i = 0; i < args.count(); i++) {
		QString &curArg = args[i];

		// perform any filename replacements that are necessary
		// (done inside the loop to handle whitespace in paths properly)
		curArg.replace("%1", fName1);
	}
	return args;
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
                git->getBaseDir(newDir, curDir, archiveChanged);

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

const QString REV_LOCAL_BRANCHES("REV_LOCAL_BRANCHES");
const QString REV_REMOTE_BRANCHES("REV_REMOTE_BRANCHES");
const QString REV_TAGS("REV_TAGS");
const QString CURRENT_BRANCH("CURRENT_BRANCH");
const QString SELECTED_NAME("SELECTED_NAME");

void MainImpl::updateRevVariables(SCRef sha) {
	QMap<QString, QVariant> &v = revision_variables;
	v.clear();

	const QStringList &remote_branches = git->getRefNames(sha, Git::RMT_BRANCH);
	QString curBranch;
	v.insert(REV_LOCAL_BRANCHES, git->getRefNames(sha, Git::BRANCH));
	v.insert(CURRENT_BRANCH, git->getCurrentBranchName());
	v.insert(REV_REMOTE_BRANCHES, remote_branches);
	v.insert(REV_TAGS, git->getRefNames(sha, Git::TAG));
	v.insert("SHA", sha);

	// determine which name the user clicked on
	ListView* lv = rv->tab()->listViewLog;
	v.insert(SELECTED_NAME, lv->selectedRefName());
}

void MainImpl::updateContextActions(SCRef newRevSha, SCRef newFileName,
                                    bool isDir, bool found) {

	bool pathActionsEnabled = !newFileName.isEmpty();
	bool fileActionsEnabled = (pathActionsEnabled && !isDir);

	ActViewFile->setEnabled(fileActionsEnabled);
	ActViewFileNewTab->setEnabled(fileActionsEnabled && firstTab<FileView>());
	ActExternalDiff->setEnabled(fileActionsEnabled);
	ActExternalEditor->setEnabled(fileActionsEnabled);
	ActSaveFile->setEnabled(fileActionsEnabled);
	ActFilterTree->setEnabled(pathActionsEnabled || ActFilterTree->isChecked());

//	bool isTag       = false;
	bool isUnApplied = false;
	bool isApplied   = false;

	uint ref_type = 0;

	if (found) {
		const Rev* r = git->revLookup(newRevSha);
		ref_type = git->checkRef(newRevSha, Git::ANY_REF);
//		isTag = ref_type & Git::TAG;
		isUnApplied = r->isUnApplied;
		isApplied = r->isApplied;
	}
	ActMarkDiffToSha->setEnabled(newRevSha != ZERO_SHA);
	ActCheckout->setEnabled(found && (newRevSha != ZERO_SHA) && !isUnApplied);
	ActBranch->setEnabled(found && (newRevSha != ZERO_SHA) && !isUnApplied);
	ActTag->setEnabled(found && (newRevSha != ZERO_SHA) && !isUnApplied);
	ActDelete->setEnabled(ref_type != 0);
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

	if (testFlag(OPEN_IN_EDITOR_F, FLAGS_KEY)) {
		if (item && ActExternalEditor->isEnabled())
			ActExternalEditor->activate(QAction::Trigger);
	} else {
		bool isMainView = (item && item->listWidget() == rv->tab()->fileList);
		if (isMainView && ActViewDiff->isEnabled())
			ActViewDiff->activate(QAction::Trigger);

		if (item && !isMainView && ActViewFile->isEnabled())
			ActViewFile->activate(QAction::Trigger);
	}
}

void MainImpl::treeView_doubleClicked(QTreeWidgetItem* item, int) {
	if (testFlag(OPEN_IN_EDITOR_F, FLAGS_KEY)) {
		if (item && ActExternalEditor->isEnabled())
			ActExternalEditor->activate(QAction::Trigger);
	} else {
		if (item && ActViewFile->isEnabled())
			ActViewFile->activate(QAction::Trigger);
	}
}

void MainImpl::tabBar_tabCloseRequested(int index) {

	Domain* t;
	switch (tabType(&t, index)) {
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
		dbs("ASSERT in tabBar_tabCloseRequested: unknown current page");
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

void MainImpl::applyRevisions(SCList remoteRevs, SCRef remoteRepo) {
	// remoteRevs is already sanity checked to contain some possible valid data

	QDir dr(curDir + QGit::PATCHES_DIR);
	dr.setFilter(QDir::Files);
	if (!dr.exists(remoteRepo)) {
		statusBar()->showMessage("Remote repository missing: " + remoteRepo);
		return;
	}
	if (dr.exists() && dr.count()) {
		statusBar()->showMessage(QString("Please remove stale import directory " + dr.absolutePath()));
		return;
	}
	bool workDirOnly, fold;
	if (!askApplyPatchParameters(&workDirOnly, &fold))
		return;

	// ok, let's go
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	raise();
	EM_PROCESS_EVENTS;

	uint revNum = 0;
	QStringList::const_iterator it(remoteRevs.constEnd());
	do {
		--it;
		SCRef sha = *it;
		statusBar()->showMessage(QString("Importing revision %1 of %2: %3")
		                         .arg(++revNum).arg(remoteRevs.count()).arg(sha));

		// we create patches one by one
		if (!git->formatPatch(QStringList(sha), dr.absolutePath(), remoteRepo))
			break;

		dr.refresh();
		if (dr.count() != 1) {
			qDebug("ASSERT in on_droppedRevisions: found %i files "
			       "in %s", dr.count(), qPrintable(dr.absolutePath()));
			break;
		}
		SCRef fn(dr.absoluteFilePath(dr[0]));
		bool is_applied = git->applyPatchFile(fn, fold, Git::optDragDrop);
		dr.remove(fn);
		if (!is_applied) {
			statusBar()->showMessage(QString("Failed to import revision %1 of %2: %3")
			                         .arg(revNum).arg(remoteRevs.count()).arg(sha));
			break;
		}

	} while (it != remoteRevs.constBegin());

	if (it == remoteRevs.constBegin())
		statusBar()->clearMessage();

	if (workDirOnly && (revNum > 0))
		git->resetCommits(revNum);

	dr.rmdir(dr.absolutePath()); // 'dr' must be already empty
	QApplication::restoreOverrideCursor();
	refreshRepo();
}

bool MainImpl::applyPatches(const QStringList &files) {
	bool workDirOnly, fold;
	if (!askApplyPatchParameters(&workDirOnly, &fold))
		return false;

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	QStringList::const_iterator it=files.begin(), end=files.end();
	for(; it!=end; ++it) {
		statusBar()->showMessage("Applying " + *it);
		if (!git->applyPatchFile(*it, fold, Git::optDragDrop))
			statusBar()->showMessage("Failed to apply " + *it);
	}
	if (it == end) statusBar()->clearMessage();

	if (workDirOnly && (files.count() > 0))
		git->resetCommits(files.count());

	QApplication::restoreOverrideCursor();
	refreshRepo();
	return true;
}

void MainImpl::rebase(const QString &from, const QString &to, const QString &onto)
{
	bool success = false;
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	if (from.isEmpty()) {
		success = git->run(QString("git checkout -q %1").arg(to)) &&
		          git->run(QString("git rebase %1").arg(onto));
	} else {
		success = git->run(QString("git rebase --onto %3 %1^ %2").arg(from, to, onto));
	}
    if (!success) {
        // TODO say something about rebase failure
    }
	refreshRepo(true);
	QApplication::restoreOverrideCursor();
}

void MainImpl::merge(const QStringList &shas, const QString &into)
{
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	QString output;
	if (git->merge(into, shas, &output)) {
		refreshRepo(true);
		statusBar()->showMessage(QString("Successfully merged into %1").arg(into));
		ActCommit_activated();
	} else if (!output.isEmpty()) {
		QMessageBox::warning(this, "git merge failed",
		                     QString("\n\nGit says: \n\n" + output));
	}
	refreshRepo(true);
	QApplication::restoreOverrideCursor();
}

void MainImpl::moveRef(const QString &target, const QString &toSHA)
{
	QString cmd;
	if (target.startsWith("remotes/")) {
		QString remote = target.section("/", 1, 1);
		QString name = target.section("/", 2);
		cmd = QString("git push -q %1 %2:%3").arg(remote, toSHA, name);
	} else if (target.startsWith("tags/")) {
		cmd = QString("git tag -f %1 %2").arg(target.section("/",1), toSHA);
	} else if (!target.isEmpty()) {
		const QString &sha = git->getRefSha(target, Git::BRANCH, false);
		if (sha.isEmpty()) return;
		const QStringList &children = git->getChildren(sha);
		if ((children.count() == 0 || (children.count() == 1 && children.front() == ZERO_SHA)) && // no children
		    git->getRefNames(sha, Git::ANY_REF).count() == 1 && // last ref name
		    QMessageBox::question(this, "move branch",
		                          QString("This is the last reference to this branch.\n"
		                                  "Do you really want to move '%1'?").arg(target))
		    == QMessageBox::No)
			return;

		if (target == git->getCurrentBranchName()) // move current branch
			cmd = QString("git checkout -q -B %1 %2").arg(target, toSHA);
		else // move any other local branch
			cmd = QString("git branch -f %1 %2").arg(target, toSHA);
	}
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	if (git->run(cmd)) refreshRepo(true);
	QApplication::restoreOverrideCursor();
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
	    && (!git->isNothingToCommit() || git->isUnknownFiles()))
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

	return tabType(t, tabWdg->currentIndex());
}

int MainImpl::tabType(Domain** t, int index) {

	*t = NULL;
	QWidget* curPage = tabWdg->widget(index);
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
		break;
	case TAB_PATCH:
		static_cast<PatchView*>(t)->tab()->textEditDiff->setFocus();
		break;
	case TAB_FILE:
		static_cast<FileView*>(t)->tab()->histListView->setFocus();
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

	if (se) {
#if QT_VERSION >= 0x050000
		const QKeySequence& key = se->key();
#else
		const int key = se->key();
#endif

		if (key == Qt::Key_I) {
			rv->tab()->listViewLog->on_keyUp();
		}
		else if ((key == Qt::Key_K) || (key == Qt::Key_N)) {
			rv->tab()->listViewLog->on_keyDown();
		}
		else if (key == (Qt::SHIFT | Qt::Key_Up)) {
			goMatch(-1);
		}
		else if (key == (Qt::SHIFT | Qt::Key_Down)) {
			goMatch(1);
		}
		else if (key == Qt::Key_Left) {
			ActBack_activated();
		}
		else if (key == Qt::Key_Right) {
			ActForward_activated();
		}
		else if (key == (Qt::CTRL | Qt::Key_Plus)) {
			adjustFontSize(1); //TODO replace magic constant
		}
		else if (key == (Qt::CTRL | Qt::Key_Minus)) {
			adjustFontSize(-1); //TODO replace magic constant
		}
		else if (key == Qt::Key_U) {
			scrollTextEdit(-18); //TODO replace magic constant
		}
		else if (key == Qt::Key_D) {
			scrollTextEdit(18); //TODO replace magic constant
		}
		else if (key == Qt::Key_Delete || key == Qt::Key_B || key == Qt::Key_Backspace) {
			scrollTextEdit(-1); //TODO replace magic constant
		}
		else if (key == Qt::Key_Space) {
			scrollTextEdit(1);
		}
		else if (key == Qt::Key_R) {
			tabWdg->setCurrentWidget(rv->tabPage());
		}
		else if (key == Qt::Key_P || key == Qt::Key_F) {
			QWidget* cp = tabWdg->currentWidget();
			Domain* d = (key == Qt::Key_P)
						? static_cast<Domain*>(firstTab<PatchView>(cp))
						: static_cast<Domain*>(firstTab<FileView>(cp));
			if (d) tabWdg->setCurrentWidget(d->tabPage());
		}
	}
}

void MainImpl::goMatch(int delta) {

	if (ActSearchAndHighlight->isChecked())
		rv->tab()->listViewLog->scrollToNextHighlighted(delta);
	else
		rv->tab()->listViewLog->scrollToNext(delta);
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
		if ((*it)->data().toString().startsWith("RECENT"))
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
		QAction* newAction = File->addAction(QString::number(idx++) + " " + *it);
		newAction->setData(QString("RECENT ") + *it);
		newRecents << *it;
		if (idx > MAX_RECENT_REPOS)
			break;
	}
	settings.setValue(REC_REP_KEY, newRecents);
}

static void prepareRefSubmenu(QMenu* menu, const QStringList& refs, const QChar sep = '/') {

	FOREACH_SL (it, refs) {
		const QStringList& parts(it->split(sep, QString::SkipEmptyParts));
		QMenu* add_here = menu;
		FOREACH_SL (pit, parts) {
			if (pit == parts.end() - 1) break;
			QMenu* found = add_here->findChild<QMenu*>(*pit, Qt::FindDirectChildrenOnly);
			if(!found) {
				found = add_here->addMenu(*pit);
				found->setObjectName(*pit);
			}
			add_here = found;
		}
		QAction* act = add_here->addAction(*it);
		act->setData("Ref");
	}
}

void MainImpl::doContexPopup(SCRef sha) {

	QMenu contextMenu(this);
	QMenu contextBrnMenu("Branches...", this);
	QMenu contextRmtMenu("Remote branches...", this);
	QMenu contextTagMenu("Tags...", this);

	connect(&contextMenu, SIGNAL(triggered(QAction*)), this, SLOT(goRef_triggered(QAction*)));

	Domain* t;
	int tt = currentTabType(&t);
	bool isRevPage = (tt == TAB_REV);
	bool isPatchPage = (tt == TAB_PATCH);
	bool isFilePage = (tt == TAB_FILE);

	if (isFilePage && ActViewRev->isEnabled())
		contextMenu.addAction(ActViewRev);

	if (!isPatchPage && ActViewDiff->isEnabled())
		contextMenu.addAction(ActViewDiff);

	if (isRevPage && ActViewDiffNewTab->isEnabled())
		contextMenu.addAction(ActViewDiffNewTab);

	if (!isFilePage && ActExternalDiff->isEnabled())
		contextMenu.addAction(ActExternalDiff);

	if (isFilePage && ActExternalEditor->isEnabled())
		contextMenu.addAction(ActExternalEditor);

	if (isRevPage) {
		updateRevVariables(sha);

		if (ActCommit->isEnabled() && (sha == ZERO_SHA))
			contextMenu.addAction(ActCommit);
		if (ActCheckout->isEnabled())
			contextMenu.addAction(ActCheckout);
		if (ActBranch->isEnabled())
			contextMenu.addAction(ActBranch);
		if (ActTag->isEnabled())
			contextMenu.addAction(ActTag);
		if (ActDelete->isEnabled())
			contextMenu.addAction(ActDelete);
		if (ActMailFormatPatch->isEnabled())
			contextMenu.addAction(ActMailFormatPatch);
		if (ActPush->isEnabled())
			contextMenu.addAction(ActPush);
		if (ActPop->isEnabled())
			contextMenu.addAction(ActPop);

		contextMenu.addSeparator();

		QStringList bn(git->getAllRefNames(Git::BRANCH, Git::optOnlyLoaded));
		bn.sort();
		prepareRefSubmenu(&contextBrnMenu, bn);
		contextMenu.addMenu(&contextBrnMenu);
		contextBrnMenu.setEnabled(bn.size() > 0);

		QStringList rbn(git->getAllRefNames(Git::RMT_BRANCH, Git::optOnlyLoaded));
		rbn.sort();
		prepareRefSubmenu(&contextRmtMenu, rbn);
		contextMenu.addMenu(&contextRmtMenu);
		contextRmtMenu.setEnabled(rbn.size() > 0);

		QStringList tn(git->getAllRefNames(Git::TAG, Git::optOnlyLoaded));
		tn.sort();
		prepareRefSubmenu(&contextTagMenu, tn);
		contextMenu.addSeparator();
		contextMenu.addMenu(&contextTagMenu);
		contextTagMenu.setEnabled(tn.size() > 0);

	}

	QPoint p = QCursor::pos();
	p += QPoint(10, 10);
	contextMenu.exec(p);

	// remove selected ref name after showing the popup
	revision_variables.remove(SELECTED_NAME);
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
		if ((type == POPUP_FILE_EV) && ActExternalEditor->isEnabled())
			contextMenu.addAction(ActExternalEditor);
		if (ActExternalEditor->isEnabled())
			contextMenu.addAction(ActExternalEditor);
	}
	contextMenu.exec(QCursor::pos());
}

void MainImpl::goRef_triggered(QAction* act) {

	if (!act || act->data() != "Ref")
		return;

	SCRef refSha(git->getRefSha(act->iconText()));
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

	const QString dataString = act->data().toString();
	if (!dataString.startsWith("RECENT"))
		// only recent repos entries have "RECENT" in data field
		return;

	const QString workDir = dataString.mid(7);
	if (!workDir.isEmpty()) {
		QDir d(workDir);
		if (d.exists())
			setRepository(workDir);
		else
			statusBar()->showMessage("Directory '" + workDir +
			                         "' does not seem to exist anymore");
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

	QString actionName = act->text();
	if (actionName == "Setup actions...")
		return;

	QSettings set;
	QStringList actionsList = set.value(ACT_LIST_KEY).toStringList();
	if (!(actionsList.contains(actionName) || actionsList.contains(actionName.remove(QChar('&'))))) {
		dbp("ASSERT in customAction_activated, action %1 not found", actionName);
		return;
	}
	QString cmd = set.value(ACT_GROUP_KEY + actionName + ACT_TEXT_KEY).toString().trimmed();
	if (testFlag(ACT_CMD_LINE_F, ACT_GROUP_KEY + actionName + ACT_FLAGS_KEY)) {
		// for backwards compatibility: if ACT_CMD_LINE_F is set, insert a dialog token in first line
		int pos = cmd.indexOf('\n');
		if (pos < 0) pos = cmd.length();
		cmd.insert(pos, " %lineedit:cmdline args%");
	}
	updateRevVariables(lineEditSHA->text());
	InputDialog dlg(cmd, revision_variables, "Run custom action: " + actionName, this);
	if (!dlg.empty() && dlg.exec() != QDialog::Accepted) return;
	try {
		cmd = dlg.replace(revision_variables); // replace variables
	} catch (const std::exception &e) {
		QMessageBox::warning(this, "Custom action command", e.what());
		return;
	}

	if (cmd.isEmpty())
		return;

	ConsoleImpl* c = new ConsoleImpl(actionName, git); // has Qt::WA_DeleteOnClose attribute

	connect(this, SIGNAL(typeWriterFontChanged()),
	        c, SLOT(typeWriterFontChanged()));

	connect(this, SIGNAL(closeAllWindows()), c, SLOT(close()));
	connect(c, SIGNAL(customAction_exited(const QString&)),
	        this, SLOT(customAction_exited(const QString&)));

	if (c->start(cmd))
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

/** Checkout supports various operation modes:
 *  - switching to an existing branch (standard use case)
 *  - create and checkout a new branch
 *  - resetting an existing branch to a new sha
 */
void MainImpl::ActCheckout_activated()
{
	QString sha = lineEditSHA->text(), rev = sha;
	const QString branchKey("local branch name");
	QString cmd = "git checkout -q ";

	const QString &selected_name = revision_variables.value(SELECTED_NAME).toString();
	const QString &current_branch = revision_variables.value(CURRENT_BRANCH).toString();
	const QStringList &local_branches = revision_variables.value(REV_LOCAL_BRANCHES).toStringList();

	if (!selected_name.isEmpty() &&
	    local_branches.contains(selected_name) &&
	    selected_name != current_branch) {
		// standard branch switching: directly checkout selected branch
		rev = selected_name;
	} else {
		// ask for (new) local branch name
		QString title = QString("Checkout ");
		if (selected_name.isEmpty()) {
			title += QString("revision ") + sha.mid(0, 8);
		} else {
			title	+= QString("branch ") + selected_name;
			rev = selected_name;
		}
		// merge all reference names into a single list
		const QStringList &rmts = revision_variables.value(REV_REMOTE_BRANCHES).toStringList();
		QStringList all_names;
		all_names << revision_variables.value(REV_LOCAL_BRANCHES).toStringList();
		for(QStringList::const_iterator it=rmts.begin(), end=rmts.end(); it!=end; ++it) {
			// drop initial <origin>/ from name
			int pos = it->indexOf('/'); if (pos < 0) continue;
			all_names << it->mid(pos+1);
		}
		revision_variables.insert("ALL_NAMES", all_names);

		InputDialog dlg(QString("%combobox[editable,ref,empty]:%1=$ALL_NAMES%").arg(branchKey), revision_variables, title, this);
		if (dlg.exec() != QDialog::Accepted) return;

		QString branch = dlg.value(branchKey).toString();
		if (!branch.isEmpty()) {
			SCRef refsha = git->getRefSha(branch, Git::BRANCH, true);
			if (refsha == sha)
				rev = branch; // checkout existing branch, even if name wasn't directly selected
			else if (!refsha.isEmpty()) {
				if (QMessageBox::warning(this, "Checkout " + branch,
				                         QString("Branch %1 already exists. Reset?").arg(branch),
				                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
				    != QMessageBox::Yes)
					return;
				else
					cmd.append("-B ").append(branch); // reset an existing branch
			} else {
				cmd.append("-b ").append(branch); // create new local branch
			}
		} // if new branch name is empty, checkout detached
	}

	cmd.append(" ").append(rev);
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	if (!git->run(cmd)) statusBar()->showMessage("Failed to checkout " + rev);
	refreshRepo(true);
	QApplication::restoreOverrideCursor();
}

void MainImpl::ActBranch_activated() {

    doBranchOrTag(false);
}

void MainImpl::ActTag_activated() {

    doBranchOrTag(true);
}

const QStringList& stripNames(QStringList& names) {
	for(QStringList::iterator it=names.begin(), end=names.end(); it!=end; ++it)
		*it = it->section('/', -1);
	return names;
}

void MainImpl::doBranchOrTag(bool isTag) {
	const QString sha = lineEditSHA->text();
	QString refDesc = isTag ? "tag" : "branch";
	QString dlgTitle = "Create " + refDesc + " - QGit";

	QString dlgDesc = "%lineedit[ref]:name=$ALL_NAMES%";
	InputDialog::VariableMap dlgVars;
	QStringList allNames = git->getAllRefNames(Git::BRANCH | Git::RMT_BRANCH | Git::TAG, false);
	stripNames(allNames);
	allNames.removeDuplicates();
	allNames.sort();
	dlgVars.insert("ALL_NAMES", allNames);

	if (isTag) {
		QString revDesc(rv->tab()->listViewLog->currentText(LOG_COL));
		dlgDesc += "%textedit:message=$MESSAGE%";
		dlgVars.insert("MESSAGE", revDesc);
	}

	InputDialog dlg(dlgDesc, dlgVars, dlgTitle, this);
	if (dlg.exec() != QDialog::Accepted) return;
	const QString& ref = dlg.value("name").toString();

	bool force = false;
	if (!git->getRefSha(ref, isTag ? Git::TAG : Git::BRANCH, false).isEmpty()) {
		if (QMessageBox::warning(this, dlgTitle,
		                         refDesc + " name '" + ref + "' already exists.\n"
		                         "Force reset?", QMessageBox::Yes | QMessageBox::No,
		                         QMessageBox::No) != QMessageBox::Yes)
			return;
		force = true;
	}

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	QString cmd;
	if (isTag) {
		const QString& msg = dlg.value("message").toString();
		cmd = "git tag ";
		if (!msg.isEmpty()) cmd += "-m \"" + msg + "\" ";
	} else {
		cmd = "git branch ";
	}
	if (force) cmd += "-f ";
	cmd += ref + " " + sha;

	if (git->run(cmd))
		refreshRepo(true);
	else
		statusBar()->showMessage("Failed to create " + refDesc + " " + ref);

	QApplication::restoreOverrideCursor();
}

// put a ref name into a corresponding StringList for tags, remotes, and local branches
typedef QMap<QString, QStringList> RefGroupMap;
static void groupRef(const QString& ref, RefGroupMap& groups) {
	QString group, name;
	if (ref.startsWith("tags/")) { group = ref.left(5); name = ref.mid(5); }
	else if (ref.startsWith("remotes/")) { group = ref.section('/', 1, 1); name = ref.section('/', 2); }
	else { group = ""; name = ref; }
	if (!groups.contains(group))
		groups.insert(group, QStringList());
	QStringList &l = groups[group];
	l << name;
}

void MainImpl::ActDelete_activated() {

	const QString &selected_name = revision_variables.value(SELECTED_NAME).toString();
	const QStringList &tags = revision_variables.value(REV_TAGS).toStringList();
	const QStringList &rmts = revision_variables.value(REV_REMOTE_BRANCHES).toStringList();

	// merge all reference names into a single list
	QStringList all_names;
	all_names << revision_variables.value(REV_LOCAL_BRANCHES).toStringList();
	for (QStringList::const_iterator it=rmts.begin(), end=rmts.end(); it!=end; ++it)
		all_names << "remotes/" + *it;
	for (QStringList::const_iterator it=tags.begin(), end=tags.end(); it!=end; ++it)
		all_names << "tags/" + *it;

	// group selected names by origin and determine which ref names will remain
	QMap <QString, QStringList> groups;
	QStringList remaining = all_names;
	if (!selected_name.isEmpty()) {
		groupRef(selected_name, groups);
		remaining.removeOne(selected_name);
	} else if (all_names.size() == 1) {
		const QString &name = all_names.first();
		groupRef(name, groups);
		remaining.removeOne(name);
	} else {
		revision_variables.insert("ALL_NAMES", all_names);
		InputDialog dlg("%listbox:_refs=$ALL_NAMES%", revision_variables,
		                "Delete references - QGit", this);
		QListView *w = dynamic_cast<QListView*>(dlg.widget("_refs"));
		w->setSelectionMode(QAbstractItemView::ExtendedSelection);
		if (dlg.exec() != QDialog::Accepted) return;

		QModelIndexList selected = w->selectionModel()->selectedIndexes();
		for (QModelIndexList::const_iterator it=selected.begin(), end=selected.end(); it!=end; ++it) {
			const QString &name = it->data().toString();
			groupRef(name, groups);
			remaining.removeOne(name);
		}
	}
	if (groups.empty()) return;

	// check whether all refs will be removed
	const QString sha = revision_variables.value("SHA").toString();
	const QStringList &children = git->getChildren(sha);
	if ((children.count() == 0 || (children.count() == 1 && children.front() == ZERO_SHA)) && // no children
	    remaining.count() == 0 && // all refs will be removed
	    QMessageBox::warning(this, "remove references",
	                         "Do you really want to remove all\nremaining references to this branch?",
	                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
	    == QMessageBox::No)
		return;

	// group selected names by origin
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	bool ok = true;
	for (RefGroupMap::const_iterator g = groups.begin(), gend = groups.end(); g != gend; ++g) {
		QString cmd;
		if (g.key() == "") // local branches
			cmd = "git branch -D " + g.value().join(" ");
		else if (g.key() == "tags/") // tags
			cmd = "git tag -d " + g.value().join(" ");
		else // remote branches
			cmd = "git push -q " + g.key() + " :" + g.value().join(" :");
		ok &= git->run(cmd);
	}
	refreshRepo(true);
	QApplication::restoreOverrideCursor();
	if (!ok) statusBar()->showMessage("Failed, to remove some refs.");
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

void MainImpl::ActMarkDiffToSha_activated()
{
	ListView* lv = rv->tab()->listViewLog;
	lv->markDiffToSha(lineEditSHA->text());
}

void MainImpl::ActAbout_activated() {

	static const char* aboutMsg =
	"<p><b>QGit version " PACKAGE_VERSION "</b></p>"
	"<p>Copyright (c) 2005, 2007, 2008 Marco Costalba<br>"
	"Copyright (c) 2011-2019 <a href='mailto:tibirna@kde.org'>Cristian Tibirna</a></p>"
	"<p>Use and redistribute under the terms of the<br>"
	"<a href=\"http://www.gnu.org/licenses/old-licenses/gpl-2.0.html\">GNU General Public License Version 2</a></p>"
	"<p>Contributors:<br>"
	"Copyright (c) "
	"<nobr>2007 Andy Parkins,</nobr> "
	"<nobr>2007 Pavel Roskin,</nobr> "
	"<nobr>2007 Peter Oberndorfer,</nobr> "
	"<nobr>2007 Yaacov Akiba,</nobr> "
	"<nobr>2007 James McKaskill,</nobr> "
	"<nobr>2008 Jan Hudec,</nobr> "
	"<nobr>2008 Paul Gideon Dann,</nobr> "
	"<nobr>2008 Oliver Bock,</nobr> "
	"<nobr>2010 <a href='mailto:cyp561@gmail.com'>Cyp</a>,</nobr> "
	"<nobr>2011 <a href='dagenaisj@sonatest.com'>Jean-Fran&ccedil;ois Dagenais</a>,</nobr> "
	"<nobr>2011 <a href='mailto:pavtih@gmail.com'>Pavel Tikhomirov</a>,</nobr> "
	"<nobr>2011 <a href='mailto:tim@klingt.org'>Tim Blechmann</a>,</nobr> "
	"<nobr>2014 <a href='mailto:codestruct@posteo.org'>Gregor Mi</a>,</nobr> "
	"<nobr>2014 <a href='mailto:sbytnn@gmail.com'>Sbytov N.N</a>,</nobr> "
	"<nobr>2015 <a href='mailto:dendy.ua@gmail.com'>Daniel Levin</a>,</nobr> "
	"<nobr>2017 <a href='mailto:luigi.toscano@tiscali.it'>Luigi Toscano</a>,</nobr> "
	"<nobr>2016 <a href='mailto:hkarel@yandex.ru'>Pavel Karelin</a>,</nobr> "
	"<nobr>2016 <a href='mailto:zbitter@redhat.com'>Zane Bitter</a>,</nobr> "
	"<nobr>2017 <a href='mailto:wrar@wrar.name'>Andrey Rahmatullin</a>,</nobr> "
	"<nobr>2017 <a href='mailto:alex-github@wenlex.nl'>Alex Hermann</a>,</nobr> "
	"<nobr>2017 <a href='mailto:shalokshalom@protonmail.ch'>Matthias Schuster</a>,</nobr> "
	"<nobr>2017 <a href='mailto:u.joss@calltrade.ch'>Urs Joss</a>,</nobr> "
	"<nobr>2017 <a href='mailto:patrick.m.lacasse@gmail.com'>Patrick Lacasse</a>,</nobr> "
	"<nobr>2018 <a href='mailto:deveee@gmail.com'>Deve</a>,</nobr> "
	"<nobr>2018 <a href='mailto:asturm@gentoo.org'>Andreas Sturmlechner</a>,</nobr> "
	"<nobr>2018 <a href='mailto:kde@davidedmundson.co.uk'>David Edmundson</a>,</nobr> "
	"<nobr>2016-2018 <a href='mailto:rhaschke@techfak.uni-bielefeld.de'>Robert Haschke</a>,</nobr> "
	"<nobr>2018 <a href='mailto:filipe.rinaldi@gmail.com'>Filipe Rinaldi</a>,</nobr> "
	"<nobr>2018 <a href='mailto:balbusm@gmail.com'>Mateusz Balbus</a>,</nobr> "
	"<nobr>2019 <a href='mailto:sebastian@pipping.org'>Sebastian Pipping</a>,</nobr> "
	"<nobr>2019 <a href='mailto:mvf@gmx.eu'>Matthias von Faber</a>,</nobr> "
	"<nobr>2019 <a href='mailto:Kevin@tigcc.ticalc.org'>Kevin Kofler</a>,</nobr> "
	"<nobr>2020 <a href='mailto:initial.dann@gmail.com'>Daniel Kettle</a></nobr> "

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
