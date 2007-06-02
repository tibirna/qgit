/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QScrollBar>
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

#define GO_UP   1
#define GO_DOWN 2
#define GO_LOG  3
#define GO_DIFF 4

void SmartLabel::contextMenuEvent(QContextMenuEvent* e) {

	if (text().count("href=") != 2)
		return;

	QMenu* menu = new QMenu(this);
	menu->addAction("Switch links", this, SLOT(switchLinks()));
	menu->exec(e->globalPos());
	delete menu;
}

void SmartLabel::switchLinks() {

	QString t(text());
	QString link1(t.section("<a href=", 1). section("</a>", 0, 0));
	QString link2(t.section("<a href=", 2). section("</a>", 0, 0));
	t.replace(link1, "%1").replace(link2, "%2");
	setText(t.arg(link2, link1));
	adjustSize();
}

SmartBrowse::SmartBrowse(RevsView* par, RevDesc* log, PatchContent* diff) : QObject(par) {

	wheelCnt = 0;

	textBrowserDesc = log;
	textEditDiff = diff;

	QString txt("<p><img src=\":/icons/resources/%1\"> %2 <small>%3</small></p>");
	QString link("<a href=\"%1\">%2</a>");
	QString linkUp(link.arg(QString::number(GO_UP), "Up"));
	QString linkDown(link.arg(QString::number(GO_DOWN), "Down"));
	QString linkLog(link.arg(QString::number(GO_LOG), "Log"));
	QString linkDiff(link.arg(QString::number(GO_DIFF), "Diff"));

	logTopLbl = new SmartLabel(txt.arg("1uparrow.png", linkUp, ""), log);
	logBottomLbl = new SmartLabel(txt.arg("1downarrow.png", linkDiff, linkDown), log);
	diffTopLbl = new SmartLabel(txt.arg("1uparrow.png", linkLog, linkUp), diff);
	diffBottomLbl = new SmartLabel(txt.arg("1downarrow.png", linkUp, linkDown), diff);

	logTopLbl->hide();
	logBottomLbl->hide();
	diffTopLbl->hide();
	diffBottomLbl->hide();

	log->installEventFilter(this);
	diff->installEventFilter(this);
	log->verticalScrollBar()->installEventFilter(this);
	diff->verticalScrollBar()->installEventFilter(this);

	connect(logTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(logBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));
}

QTextEdit* SmartBrowse::curTextEdit() {

	bool b = textEditDiff->isVisible();
	return (b ? static_cast<QTextEdit*>(textEditDiff)
	          : static_cast<QTextEdit*>(textBrowserDesc));
}

void SmartBrowse::linkActivated(const QString& text) {

	int key = text.toInt();
	RevsView* rv = static_cast<RevsView*>(parent());

	switch (key) {
	case GO_LOG:
	case GO_DIFF:
		rv->toggleDiffView();
		break;
	case GO_UP:
		rv->tab()->listViewLog->on_keyUp();
		break;
	case GO_DOWN:
		rv->tab()->listViewLog->on_keyDown();
		break;
	default:
		dbp("ASSERT in SmartBrowse::linkActivated, key %1 not known", text);
	}
}

bool SmartBrowse::eventFilter(QObject *obj, QEvent *event) {

	QTextEdit* te = dynamic_cast<QTextEdit*>(obj);
	QScrollBar* vsb = dynamic_cast<QScrollBar*>(obj);

	QEvent::Type t = event->type();
	if (te && t == QEvent::Resize)
		parentResized();

	if (te && t == QEvent::EnabledChange) {
		logTopLbl->setVisible(te->isEnabled());
		logBottomLbl->setVisible(te->isEnabled());
		diffTopLbl->setVisible(te->isEnabled());
		diffBottomLbl->setVisible(te->isEnabled());
	}
	if (vsb && t == QEvent::Wheel) {
		bool outOfRange = (   vsb->value() == vsb->minimum()
		                   || vsb->value() == vsb->maximum());

		QWheelEvent* we = static_cast<QWheelEvent*>(event);
		if (wheelRolled(we->delta(), outOfRange))
			return true; // filter event out
	}
	return QObject::eventFilter(obj, event);
}

void SmartBrowse::parentResized() {

	QTextEdit* te = curTextEdit();
	int w = te->width() - te->verticalScrollBar()->width();
	int h = te->height() - te->horizontalScrollBar()->height();

	logTopLbl->move(w - logTopLbl->width() - 10, 10);
	diffTopLbl->move(w - diffTopLbl->width() - 10, 10);
	logBottomLbl->move(w - logBottomLbl->width() - 10, h - logBottomLbl->height() - 10);
	diffBottomLbl->move(w - diffBottomLbl->width() - 10, h - diffBottomLbl->height() - 10);

	// we are called also when user toggle view manually,
	// so reset wheel counters to be sure we don't have alias
	scrollTimer.restart();
	wheelCnt = 0;
}

bool SmartBrowse::wheelRolled(int delta, bool outOfRange) {

	bool justSwitched = (switchTimer.isValid() && switchTimer.elapsed() < 400);
	if (justSwitched)
		switchTimer.restart();

	bool scrolling = (scrollTimer.isValid() && scrollTimer.elapsed() < 400);
	bool directionChanged = (wheelCnt * delta < 0);

	// a scroll action have to start when in range
	// but can continue also when goes out of range
	if (!outOfRange || scrolling)
		scrollTimer.restart();

	if (!outOfRange || justSwitched)
		return justSwitched; // filter wheels events just after a switch

	// we want a quick rolling action to be considered valid
	bool tooSlow = (timeoutTimer.isValid() && timeoutTimer.elapsed() > 300);
	timeoutTimer.restart();

	if (directionChanged || scrolling || tooSlow)
		wheelCnt = 0;

	// ok, we would be ready to switch, but we want to add some inertia
	wheelCnt += (delta > 0 ? 1 : -1);
	if (wheelCnt * wheelCnt < 9)
		return false;

	QLabel* l;
	if (wheelCnt > 0)
		l = logTopLbl->isVisible() ? logTopLbl : diffTopLbl;
	else
		l = logBottomLbl->isVisible() ? logBottomLbl : diffBottomLbl;

	wheelCnt = 0;
	switchTimer.restart();
	linkActivated(l->text().section("href=", 1).section("\"", 1, 1));
	return false;
}

// ***************************  RevsView  ********************************

RevsView::RevsView(MainImpl* mi, Git* g, bool isMain) : Domain(mi, g, isMain) {

	revTab = new Ui_TabRev();
	revTab->setupUi(container);

	tab()->textEditDiff->hide();
	tab()->listViewLog->setup(this, g);
	tab()->textBrowserDesc->setup(this);
	tab()->textEditDiff->setup(this, git);
	tab()->fileList->setup(this, git);
	m()->treeView->setup(this, git);

	// restore geometry
	QVector<QSplitter*> v;
	v << tab()->horizontalSplitter << tab()->verticalSplitter;
	QGit::restoreGeometrySetting(QGit::REV_GEOM_KEY, NULL, &v);

	new SmartBrowse(this, tab()->textBrowserDesc, tab()->textEditDiff);

	connect(m(), SIGNAL(typeWriterFontChanged()),
	        tab()->textEditDiff, SLOT(typeWriterFontChanged()));

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

	QVector<QSplitter*> v;
	v << tab()->horizontalSplitter << tab()->verticalSplitter;
	QGit::saveGeometrySetting(QGit::REV_GEOM_KEY, NULL, &v);

	delete linkedPatchView;
	delete revTab;
}

void RevsView::clear(bool complete) {

	Domain::clear(complete);

	tab()->textBrowserDesc->clear();
	tab()->textEditDiff->clear();
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

void RevsView::toggleDiffView() {

	bool v = tab()->textEditDiff->isVisible();
	tab()->textEditDiff->setVisible(!v);
	tab()->textBrowserDesc->setVisible(v);
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
			pv->tab()->textEditDiff, SLOT(on_highlightPatch(const QString&, bool)));

		connect(pv->tab()->fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
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

			if (   testFlag(QGit::MSG_ON_NEW_F)
			    && tab()->textEditDiff->isVisible())
				toggleDiffView();
		}
		const RevFile* files = NULL;
		bool newFiles = false;

		if (st.isChanged(StateInfo::ANY & ~StateInfo::FILE_NAME) || force) {

			tab()->fileList->clear();

			if (linkedPatchView) // give some feedback while waiting
				linkedPatchView->clear();

			// blocking call, could be slow in case of all merge files
			files = git->getFiles(st.sha(), st.diffToSha(), st.allMergeFiles());
			newFiles = true;

			tab()->textEditDiff->update(st);
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
		if (st.isChanged() || force)
			tab()->textEditDiff->centerOnFileHeader(st);

		// at the end update diffs that is the slowest and must be
		// run after update of file list for 'diff to sha' to work
		if (linkedPatchView) {
			linkedPatchView->st = st;
			UPDATE_DM_MASTER(linkedPatchView, force); // async call
		}
	}
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
