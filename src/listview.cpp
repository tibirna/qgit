/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QDrag>
#include <QUrl>
#include <QSettings>
#include "FileHistory.h"
#include "domain.h"
#include "mainimpl.h"
#include "git.h"
#include "listview.h"

void getTagMarkParams(QString &name, QStyleOptionViewItem& o,
                      const int type, const bool isCurrent);
uint refTypeFromName(SCRef name);

using namespace QGit;

ListView::ListView(QWidget* parent) : QTreeView(parent), d(NULL), git(NULL), fh(NULL), lp(NULL), dropInfo(NULL) {}

void ListView::setup(Domain* dm, Git* g) {

	d = dm;
	git = g;
	fh = d->model();
	st = &(d->st);
	filterNextContextMenuRequest = false;

	setFont(QGit::STD_FONT);

	// create ListViewProxy unplugged, will be plug
	// to the model only when filtering is needed
	lp = new ListViewProxy(this, d, git);
	setModel(fh);

	ListViewDelegate* lvd = new ListViewDelegate(git, lp, this);
        lvd->setLaneHeight(fontMetrics().height() + 2);
	setItemDelegate(lvd);

	setupGeometry(); // after setting delegate

	// shortcuts are activated only if widget is visible, this is good
	new QShortcut(Qt::Key_Up,   this, SLOT(on_keyUp()));
	new QShortcut(Qt::Key_Down, this, SLOT(on_keyDown()));

	connect(lvd, SIGNAL(updateView()), viewport(), SLOT(update()));

	connect(this, SIGNAL(diffTargetChanged(int)), lvd, SLOT(diffTargetChanged(int)));

	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(on_customContextMenuRequested(const QPoint&)));
}

ListView::~ListView() {

	git->cancelDataLoading(fh); // non blocking

	const QString settingsKey = git->isMainHistory(fh) ? QGit::REV_COLS_KEY : QGit::FILE_COLS_KEY;
	QSettings settings;
	settings.setValue(settingsKey, header()->saveState());
}

const QString ListView::sha(int row) const {

	if (!lp->sourceModel()) // unplugged
		return fh->sha(row);

	QModelIndex idx = lp->mapToSource(lp->index(row, 0));
	return fh->sha(idx.row());
}

int ListView::row(SCRef sha) const {

	if (!lp->sourceModel()) // unplugged
		return fh->row(sha);

	int row = fh->row(sha);
	QModelIndex idx = fh->index(row, 0);
	return lp->mapFromSource(idx).row();
}

void ListView::setupGeometry() {

	QHeaderView* hv = header();
	hv->setStretchLastSection(true);
#if QT_VERSION >= 0x050000
	hv->setSectionResizeMode(LOG_COL, QHeaderView::Interactive);
	hv->setSectionResizeMode(TIME_COL, QHeaderView::Interactive);
	hv->setSectionResizeMode(ANN_ID_COL, QHeaderView::ResizeToContents);
#else
	hv->setResizeMode(LOG_COL, QHeaderView::Interactive);
	hv->setResizeMode(TIME_COL, QHeaderView::Interactive);
	hv->setResizeMode(ANN_ID_COL, QHeaderView::ResizeToContents);
#endif
	hv->resizeSection(GRAPH_COL, DEF_GRAPH_COL_WIDTH);
	hv->resizeSection(LOG_COL, DEF_LOG_COL_WIDTH);
	hv->resizeSection(HASH_COL, DEF_HASH_COL_WIDTH);
	hv->resizeSection(AUTH_COL, DEF_AUTH_COL_WIDTH);
	hv->resizeSection(TIME_COL, DEF_TIME_COL_WIDTH);

	if (git->isMainHistory(fh))
		hideColumn(ANN_ID_COL);

	const QString settingsKey = git->isMainHistory(fh) ? QGit::REV_COLS_KEY : QGit::FILE_COLS_KEY;
	QSettings settings;
	QVariant v = settings.value(settingsKey);
	if (v.isValid()) hv->restoreState(v.toByteArray());
}

void ListView::scrollToNextHighlighted(int direction) {

	// Depending on the value of direction, scroll to:
	// -1 = the next highlighted item above the current one (i.e. newer in history)
	//  1 = the next highlighted item below the current one (i.e. older in history)
	//  0 = the first highlighted item from the top of the list

	QModelIndex idx = currentIndex();

	if (!direction) {
		idx = idx.sibling(0,0);
		if (lp->isHighlighted(idx.row())) {
			setCurrentIndex(idx);
			return;
		}
	}

	do {
		idx = (direction >= 0 ? indexBelow(idx) : indexAbove(idx));
		if (!idx.isValid())
			return;

	} while (!lp->isHighlighted(idx.row()));

	setCurrentIndex(idx);
}

void ListView::scrollToNext(int direction) {
	// Depending on the value of direction, scroll to:
	// -1 = the next child in history
	//  1 = the previous parent in history
	SCRef s = sha(currentIndex().row());
	const Rev* r = git->revLookup(s);
	if (!r) return;
	const QStringList& next = direction < 0 ? git->getChildren(s) : r->parents();
	if (next.size() >= 1)
		setCurrentIndex(model()->index(row(next.first()), 0));
}

void ListView::scrollToCurrent(ScrollHint hint) {

	if (currentIndex().isValid())
		scrollTo(currentIndex(), hint);
}

void ListView::on_keyUp() {

	QModelIndex idx = indexAbove(currentIndex());
	if (idx.isValid())
		setCurrentIndex(idx);
}

void ListView::on_keyDown() {

	QModelIndex idx = indexBelow(currentIndex());
	if (idx.isValid())
		setCurrentIndex(idx);
}

void ListView::on_changeFont(const QFont& f) {

	setFont(f);
	ListViewDelegate* lvd = static_cast<ListViewDelegate*>(itemDelegate());
	lvd->setLaneHeight(fontMetrics().height());
	scrollToCurrent();
}

const QString ListView::currentText(int column) {

	QModelIndex idx = model()->index(currentIndex().row(), column);
	return (idx.isValid() ? idx.data().toString() : "");
}

int ListView::getLaneType(SCRef sha, int pos) const {

	const Rev* r = git->revLookup(sha, fh);
	return (r && pos < r->lanes.count() && pos >= 0 ? r->lanes.at(pos) : -1);
}

void ListView::showIdValues() {

	fh->setAnnIdValid();
	viewport()->update();
}

void ListView::getSelectedItems(QStringList& selectedItems) {

	selectedItems.clear();
	QModelIndexList ml = selectionModel()->selectedRows();
	FOREACH (QModelIndexList, it, ml)
		selectedItems.append(sha((*it).row()));

	// selectedRows() returns the items in an unspecified order,
	// so be sure rows are ordered from newest to oldest.
	selectedItems = git->sortShaListByIndex(selectedItems);
}

const QString ListView::shaFromAnnId(int id) {

	if (git->isMainHistory(fh))
		return "";

	return sha(model()->rowCount() - id);
}

int ListView::filterRows(bool isOn, bool highlight, SCRef filter, int colNum, ShaSet* set) {

	setUpdatesEnabled(false);
	int matchedNum = lp->setFilter(isOn, highlight, filter, colNum, set);
	viewport()->update();
	setUpdatesEnabled(true);
	UPDATE_DOMAIN(d);
	return matchedNum;
}

bool ListView::update() {

	int stRow = row(st->sha());
	if (stRow == -1)
		return false; // main/tree view asked us a sha not in history

	QModelIndex index = currentIndex();
	QItemSelectionModel* sel = selectionModel();

	if (index.isValid() && (index.row() == stRow)) {

		if (sel->isSelected(index) != st->selectItem())
			sel->select(index, QItemSelectionModel::Toggle);

		scrollTo(index);
	} else {
		// setCurrentIndex() does not clear previous
		// selections in a multi selection QListView
		clearSelection();

		QModelIndex newIndex = model()->index(stRow, 0);
		if (newIndex.isValid()) {

			// emits QItemSelectionModel::currentChanged()
			setCurrentIndex(newIndex);
			scrollTo(newIndex);
			if (!st->selectItem())
				sel->select(newIndex, QItemSelectionModel::Deselect);
		}
	}
	if (git->isMainHistory(fh))
		emit diffTargetChanged(row(st->diffToSha()));

	return currentIndex().isValid();
}

void ListView::currentChanged(const QModelIndex& index, const QModelIndex&) {

	SCRef selRev = sha(index.row());
	if (st->sha() != selRev) { // to avoid looping
		st->setSha(selRev);
		st->setSelectItem(true);
		UPDATE_DOMAIN(d);
	}
}

void ListView::markDiffToSha(SCRef sha) {
	if (sha != st->diffToSha()) {
		st->setDiffToSha(sha);
		emit showStatusMessage("Marked " +  sha + " for diff. (Ctrl-RightClick)");
	} else {
		st->setDiffToSha(""); // restore std view
		emit showStatusMessage("Unmarked diff reference.");
	}
	UPDATE_DOMAIN(d);
}

bool ListView::filterRightButtonPressed(QMouseEvent* e) {

	QModelIndex index = indexAt(e->pos());
	SCRef selSha = sha(index.row());
	if (selSha.isEmpty())
		return false;

	if (e->modifiers() == Qt::ControlModifier) { // check for 'diff to' function
		if (selSha != ZERO_SHA) {

			filterNextContextMenuRequest = true;
			markDiffToSha(selSha);
			return true; // filter event out
		}
	}
	// check for 'children & parents' function, i.e. if mouse is on the graph
	if (index.column() == GRAPH_COL) {

		filterNextContextMenuRequest = true;
		QStringList parents, children;
                if (getLaneParentsChildren(selSha, e->pos().x(), parents, children))
			emit lanesContextMenuRequested(parents, children);

		return true; // filter event out
	}
	return false;
}

void ListView::mousePressEvent(QMouseEvent* e) {
	lastRefName = refNameAt(e->pos());

	if (e->button() == Qt::RightButton && filterRightButtonPressed(e))
		return; // filtered out

	QTreeView::mousePressEvent(e);
}

void ListView::mouseReleaseEvent(QMouseEvent* e) {

	lastRefName = ""; // reset
	QTreeView::mouseReleaseEvent(e);
}


QPixmap ListView::pixmapFromSelection(const QStringList &revs, const QString &ref) const {
#if QT_VERSION >= 0x060000
	const qsizetype maxRows = 10;
#else
	const int maxRows = 10;
#endif
	const int dotdotRow = 5;
	QStyleOptionViewItem opt; opt.initFrom(this);
//	ListViewDelegate *lvd = dynamic_cast<ListViewDelegate*>(itemDelegate());

	QFontMetrics fm(opt.font);
	int height = fm.height()+2;
	int row = 0, rows = std::min(revs.count() + (ref.isEmpty() ? 0 : 1), maxRows);
	int spacing = 4;
	QPixmap pixmap (columnWidth(LOG_COL), rows*height);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	// render selected ref name
	if (!ref.isEmpty()) {
		QStyleOptionViewItem o(opt);
		QString dummy;
		getTagMarkParams(dummy, o, refTypeFromName(ref), false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
		painter.fillRect(0, 0, fm.horizontalAdvance(ref)+2*spacing, height, o.palette.window());
#else
		painter.fillRect(0, 0, fm.width(ref)+2*spacing, height, o.palette.window());
#endif
		painter.drawText(spacing, fm.ascent()+1, ref);
		row = 1;
	}

	painter.fillRect(0, row*height, pixmap.width(), (rows-row)*height, opt.palette.window());
	for (QStringList::const_iterator it = revs.begin(), end = revs.end(); it != end; ++it) {
		const Rev* r = git->revLookup(it->section(" ", 0, 0));
		if (!r) continue; // should not happen
		painter.drawText(spacing, row*height + fm.ascent()+1, r->shortLog()); ++row;
		// jump to last dotdotRows-1 items if necessary
		if (rows-row == dotdotRow && end-it > dotdotRow+1) {
			++row; // leave one line empty
			it += end-it - dotdotRow;
		}
	}
	painter.end();
	return pixmap;
}

void ListView::startDragging(QMouseEvent* /*e*/) {

	QStringList selRevs;
	getSelectedItems(selRevs);
	selRevs.removeAll(ZERO_SHA);

	QDrag* drag = new QDrag(this);
	QMimeData* mimeData = new QMimeData;
	if (!selRevs.empty()) {
		Qt::DropActions actions = Qt::CopyAction; // patch
		Qt::DropAction default_action = Qt::CopyAction;

		// compose mime data
		bool contiguous = git->isContiguous(selRevs);

		// standard text for range description
		if (contiguous) {
			QString text;
			if (selRevs.size() > 1) text += selRevs.back() + "..";
			text += lastRefName.isEmpty() ? selRevs.front() : lastRefName;
			mimeData->setText(text);
		}

		// revision range for patch/merge/rebase operations
		QString mime = QString("%1@%2\n").arg(contiguous ? "RANGE" : "LIST").arg(d->m()->currentDir());
		if (contiguous && !lastRefName.isEmpty())
			selRevs.front() += " " + lastRefName; // append ref name
		mime.append(selRevs.join("\n"));
		mimeData->setData("application/x-qgit-revs", mime.toUtf8());

		drag->setMimeData(mimeData);
		drag->setPixmap(pixmapFromSelection(selRevs, lastRefName));

		if (contiguous) { // rebase enabled
			actions |= Qt::MoveAction; // rebase (local branch) or push (remote branch)
		}
		// merging (of several shas) is also enabled
		actions |= Qt::LinkAction;

		drag->exec(actions, default_action);
	}
}

void ListView::mouseMoveEvent(QMouseEvent* e) {

	if (e->buttons() == Qt::LeftButton && QGit::testFlag(QGit::ENABLE_DRAGNDROP_F)) {
		startDragging(e);
		return;
	}

	QTreeView::mouseMoveEvent(e);
}

struct ListView::DropInfo {
	DropInfo (uint f) : flags(f) {}

	enum Flags {
		PATCHES  = 1 << 0,
		REV_LIST = 1 << 1,
		REV_RANGE = 1 << 2,
		SAME_REPO     = 1 << 3
	};
	enum Action {
		PatchAction = Qt::CopyAction,
		RebaseAction = Qt::MoveAction,
		MoveRefAction = (Qt::LinkAction << 1) | Qt::MoveAction,
		MergeAction = Qt::LinkAction
	};

	QString sourceRepo;
	QString sourceRef;
	uint    sourceRefType;
	Action  action;
	uint flags;
	QStringList shas;

	static QString actionName (ListView::DropInfo::Action a) {
		switch (a) {
		case PatchAction: return "patching";
		case RebaseAction: return "rebasing";
		case MoveRefAction: return "moving";
		case MergeAction: return "merging";
		default: return "This should not happen.";
		}
	}
};

uint refTypeFromName(SCRef name) {
	if (name.startsWith("tags/")) return Git::TAG;
	if (name.startsWith("remotes/")) return Git::RMT_BRANCH;
	if (!name.isEmpty()) return Git::BRANCH;
	return 0;
}

void ListView::dragEnterEvent(QDragEnterEvent* e) {

	if (dropInfo) delete dropInfo;
	dropInfo = NULL;
	bool bCleanWorkDir = git->isNothingToCommit();

	// accept local file urls for patching
	bool valid=true;
	const QList<QUrl>& urls = e->mimeData()->urls();
	for(QList<QUrl>::const_iterator it=urls.begin(), end=urls.end();
	    valid && it!=end; ++it)
		valid &= it->isLocalFile();
	if (urls.size() > 0 && valid && bCleanWorkDir) {
		dropInfo = new DropInfo(DropInfo::PATCHES);
		e->accept();
	}

	// parse internal mime format
	if (!e->mimeData()->hasFormat("application/x-qgit-revs"))
		return;

	dropInfo = new DropInfo(DropInfo::REV_LIST);

	SCRef revsText(e->mimeData()->data("application/x-qgit-revs"));
	QString header = revsText.section("\n", 0, 0);
	dropInfo->shas = revsText.section("\n", 1).split('\n', QGIT_SPLITBEHAVIOR(SkipEmptyParts));
	// extract refname and sha from first entry again
	dropInfo->sourceRef = dropInfo->shas.front().section(" ", 1);
	dropInfo->shas.front() = dropInfo->shas.front().section(" ", 0, 0);
	dropInfo->sourceRefType = refTypeFromName(dropInfo->sourceRef);

	// check source repo
	dropInfo->sourceRepo = header.section("@", 1);
	if (dropInfo->sourceRepo != d->m()->currentDir()) {
		if (!QDir().exists(dropInfo->sourceRepo)) {
			emit showStatusMessage("Remote repository missing: " + dropInfo->sourceRepo, 10000);
			e->ignore();
			return;
		}
	} else
		dropInfo->flags |= DropInfo::SAME_REPO;


	if (!bCleanWorkDir && // dirty work dir doesn't allow merging/rebasing
	    // only exception is moving ref names within same repo
	    !(dropInfo->sourceRefType != 0 && dropInfo->shas.count() == 1 && (dropInfo->flags & DropInfo::SAME_REPO))) {
		emit showStatusMessage("Drag-n-drop rejected: First clean your working dir!", 10000);
		e->ignore();
		return;
	}
	e->accept();

	if (header.startsWith("RANGE")) dropInfo->flags |= DropInfo::REV_RANGE;
}

void ListView::dragMoveEvent(QDragMoveEvent* e) {
	// When getting here, dragEnterEvent already accepted the drag in general

	SCRef targetRef = refNameAt(e->
#if QT_VERSION >= 0x060000
		position().toPoint()
#else
		pos()
#endif
	);
	uint targetRefType = refTypeFromName(targetRef);
	QModelIndex idx = indexAt(e->
#if QT_VERSION >= 0x060000
		position().toPoint()
#else
		pos()
#endif
	);
	SCRef targetSHA = sha(idx.row());
	uint   accepted_actions = DropInfo::PatchAction; // applying patches is always allowed
	DropInfo::Action  action, default_action = DropInfo::PatchAction;

	// extended drop actions (merging, rebasing, pushing) are only allowed
	if (dropInfo->flags & DropInfo::SAME_REPO && // on same repo
	    dropInfo->flags & DropInfo::REV_LIST && // qgit drags
	    idx.column() == LOG_COL) { // and when dropping onto LOG_COL
		// only accept drop when target has different sha than source shas
		if (dropInfo->shas.contains(targetSHA)) {
			e->ignore();
			emit showStatusMessage("Cannot drop onto current selection.");
			return;
		}
		// decide the preferred drop action based on context and

		// rebasing is allowed onto any sha (and workdir),
		// but only if source sha list is contiguous range
		if (dropInfo->flags & DropInfo::REV_RANGE) {
			accepted_actions |= DropInfo::RebaseAction;
			default_action = DropInfo::RebaseAction;
		}

		// merging is allowed onto any local branch
		if (targetRefType == Git::BRANCH) {
			accepted_actions |= DropInfo::MergeAction;
			default_action = DropInfo::MergeAction;
		}

		// pushing is allowed when sha list has 1 item and sourceRef is remote branch
		if (dropInfo->shas.count() == 1 &&
		    dropInfo->sourceRefType != 0 && targetSHA != ZERO_SHA) {
			accepted_actions |= DropInfo::MoveRefAction;
			default_action = DropInfo::MoveRefAction;
		}
	} else if (e->source() == this && idx.row() == currentIndex().row()) {
		// move at least by one line before enabling drag
		e->ignore();
		showStatusMessage("");
		return;
	}

	e->accept();
	// check whether modifier keys enforce an action
#if QT_VERSION >= 0x060000
	switch (e->modifiers()) {
#else
	switch (e->keyboardModifiers()) {
#endif
	case Qt::ControlModifier: action = DropInfo::PatchAction; break;
	case Qt::ShiftModifier: action = DropInfo::RebaseAction; break;
	case Qt::AltModifier: action = DropInfo::MergeAction; break;
	default: action = default_action; break;
	}
	QString statusMsg;
	if ((action & accepted_actions) == 0) {
		statusMsg = DropInfo::actionName(action) + " not allowed. ";
		statusMsg[0] = statusMsg[0].toUpper();
		action = default_action;
	}

	// inform user about the to-be-performed action in statusBar
	switch (action) {
	case DropInfo::PatchAction:
		statusMsg += "Applying patches";
		break;
	case DropInfo::RebaseAction:
		statusMsg += QString("Rebasing %1 onto %2")
		        .arg((dropInfo->sourceRefType == Git::BRANCH &&
		              dropInfo->shas.count() == 1) ? dropInfo->sourceRef : "selection")
		        .arg(targetRefType == Git::BRANCH ? targetRef : targetSHA);
		break;
	case DropInfo::MoveRefAction:
		statusMsg += "Moving " + dropInfo->sourceRef;
		break;
	case DropInfo::MergeAction:
		statusMsg += "Merging selected branches into " + targetRef;
		break;
	}
	emit showStatusMessage(statusMsg);
	dropInfo->action = action;
	e->setDropAction(static_cast<Qt::DropAction>(action & 0x7));
}

void ListView::dragLeaveEvent(QDragLeaveEvent* /*e*/)
{
    if (dropInfo) delete dropInfo;
    dropInfo = NULL;
	showStatusMessage("");
}

void ListView::dropEvent(QDropEvent *e) {

	if (dropInfo->flags & DropInfo::PATCHES) {
		QStringList files;
		QList<QUrl> urls = e->mimeData()->urls();
		FOREACH(QList<QUrl>, it, urls)
		    files << it->toLocalFile();

		emit applyPatches(files);
		return;
	}

	SCRef targetRef = refNameAt(e->
#if QT_VERSION >= 0x060000
		position().toPoint()
#else
		pos()
#endif
	);
//	uint  targetRefType = refTypeFromName(targetRef);
	SCRef targetSHA = sha(indexAt(e->
#if QT_VERSION >= 0x060000
		position().toPoint()
#else
		pos()
#endif
	).row());
	switch(dropInfo->action) {
	case DropInfo::PatchAction:
		emit applyRevisions(dropInfo->shas, dropInfo->sourceRepo);
		break;
	case DropInfo::RebaseAction:
		if (dropInfo->sourceRefType == Git::BRANCH && dropInfo->shas.count() == 1)
			emit rebase("", dropInfo->sourceRef, targetSHA);
		else
			emit rebase(dropInfo->shas.last(),
		                dropInfo->sourceRef.isEmpty() ? dropInfo->shas.first()
		                                              : dropInfo->sourceRef,
		                targetSHA);
		break;
	case DropInfo::MergeAction:
		emit merge(dropInfo->shas, targetRef);
		break;
	case DropInfo::MoveRefAction:
		emit moveRef(dropInfo->sourceRef, targetSHA);
		break;
	}
}

void ListView::on_customContextMenuRequested(const QPoint& pos) {

	QModelIndex index = indexAt(pos);
	if (!index.isValid())
		return;

	if (filterNextContextMenuRequest) {
		// event filter does not work on them
		filterNextContextMenuRequest = false;
		return;
	}
	emit contextMenu(sha(index.row()), POPUP_LIST_EV);
}

bool ListView::getLaneParentsChildren(SCRef sha, int x, SList p, SList c) {

	ListViewDelegate* lvd = static_cast<ListViewDelegate*>(itemDelegate());
	uint lane = x / lvd->laneWidth();
	int t = getLaneType(sha, lane);
	if (t == EMPTY || t == -1)
		return false;

	// first find the parents
	p.clear();
	QString root;
	if (!isFreeLane(t)) {
		p = git->revLookup(sha, fh)->parents(); // pointer cannot be NULL
		root = sha;
	} else {
		SCRef par(git->getLaneParent(sha, lane));
		if (par.isEmpty()) {
                        dbs("ASSERT getLaneParentsChildren: parent not found");
			return false;
		}
		p.append(par);
		root = p.first();
	}
	// then find children
        c = git->getChildren(root);
	return true;
}

/** Iterator over all refnames of a given sha.
 *  References are traversed in following order:
 *  detached (name empty), local branches, remote branches, tags, other refs
 */
class RefNameIterator {
	Git* git;
	const QString sha;
	uint ref_types; // all reference types associated with sha
	int  cur_state; // state indicating the currently processed ref type
	QStringList ref_names; // ref_names of current type
	QStringList::const_iterator cur_name;
	QString cur_branch;

public:
	RefNameIterator(const QString &sha, Git* git);
	bool valid() const {return cur_state != -1;}
	QString name() const {return *cur_name;}
	int type() {return cur_state;}
	bool isCurrentBranch() {return *cur_name == cur_branch;}

	void next();
};

RefNameIterator::RefNameIterator(const QString &sha, Git *git)
    : git(git), sha(sha), cur_state(0), cur_branch(git->getCurrentBranchName())
{
	ref_types = git->checkRef(sha);
	if (ref_types == 0) {
		cur_state = -1; // indicates end
		return;
	}

	// initialize dummy string list
	ref_names << "";
	cur_name = ref_names.begin();

	// detached ?
	if ((ref_types & Git::CUR_BRANCH) && cur_branch.isEmpty()) {
		// indicate detached state with type() == 0 and empty ref name
		cur_branch = *cur_name;
	} else { // advance to first real ref name
		next();
	}
}

void RefNameIterator::next()
{
	++cur_name;

	// switch to next ref type if required
	while (valid() && cur_name == ref_names.end()) {
		switch (cur_state) {
		case 0: cur_state = Git::BRANCH; break;
		case Git::BRANCH: cur_state = Git::RMT_BRANCH; break;
		case Git::RMT_BRANCH: cur_state = Git::TAG; break;
		case Git::TAG: cur_state = Git::REF; break;
		default: cur_state = -1; // indicate end
		}
		ref_names = git->getRefNames(sha, (Git::RefType)cur_state);
		cur_name = ref_names.begin();
	}
}

// *****************************************************************************

ListViewDelegate::ListViewDelegate(Git* g, ListViewProxy* px, QObject* p) : QItemDelegate(p) {

	git = g;
	lp = px;
	laneHeight = 0;
	diffTargetRow = -1;
}

QSize ListViewDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {

	return QSize(laneWidth(), laneHeight);
}

void ListViewDelegate::diffTargetChanged(int row) {

	if (diffTargetRow != row) {
		diffTargetRow = row;
		emit updateView();
	}
}

const Rev* ListViewDelegate::revLookup(int row, FileHistory** fhPtr) const {

	ListView* lv = static_cast<ListView*>(parent());
	FileHistory* fh = static_cast<FileHistory*>(lv->model());

	if (lp->sourceModel())
		fh = static_cast<FileHistory*>(lp->sourceModel());

	if (fhPtr)
		*fhPtr = fh;

	return git->revLookup(lv->sha(row), fh);
}

static QColor blend(const QColor& col1, const QColor& col2, int amount = 128) {

	// Returns ((256 - amount)*col1 + amount*col2) / 256;
	return QColor(((256 - amount)*col1.red()   + amount*col2.red()  ) / 256,
	              ((256 - amount)*col1.green() + amount*col2.green()) / 256,
	              ((256 - amount)*col1.blue()  + amount*col2.blue() ) / 256);
}

void ListViewDelegate::paintGraphLane(QPainter* p, int type, int x1, int x2,
                                      const QColor& col, const QColor& activeCol, const QBrush& back) const {

        const int padding = 2;
        x1 += padding;
        x2 += padding;

	int h = laneHeight / 2;
	int m = (x1 + x2) / 2;
        int r = (x2 - x1) * 1 / 3;
	int d =  2 * r;

	#define P_CENTER m , h
	#define P_0      x2, h      // >
	#define P_90     m , 0      // ^
	#define P_180    x1, h      // <
	#define P_270    m , 2 * h  // v
	#define DELTA_UR 2*(x1 - m), 2*h ,   0*16, 90*16  // -,
	#define DELTA_DR 2*(x1 - m), 2*-h, 270*16, 90*16  // -'
	#define DELTA_UL 2*(x2 - m), 2*h ,  90*16, 90*16  //  ,-
	#define DELTA_DL 2*(x2 - m), 2*-h, 180*16, 90*16  //  '-
	#define CENTER_UR x1, 2*h, 225
	#define CENTER_DR x1, 0  , 135
	#define CENTER_UL x2, 2*h, 315
	#define CENTER_DL x2, 0  ,  45
	#define R_CENTER m - r, h - r, d, d

	static QPen lanePen(QPalette().color(QPalette::WindowText), 2); // fast path here

	// arc
	switch (type) {
	case JOIN:
	case JOIN_R:
	case HEAD:
	case HEAD_R: {
		QConicalGradient gradient(CENTER_UR);
		gradient.setColorAt(0.375, col);
		gradient.setColorAt(0.625, activeCol);
                lanePen.setBrush(gradient);
                p->setPen(lanePen);
		p->drawArc(P_CENTER, DELTA_UR);
		break;
	}
	case JOIN_L: {
		QConicalGradient gradient(CENTER_UL);
		gradient.setColorAt(0.375, activeCol);
		gradient.setColorAt(0.625, col);
                lanePen.setBrush(gradient);
                p->setPen(lanePen);
		p->drawArc(P_CENTER, DELTA_UL);
		break;
	}
	case TAIL:
	case TAIL_R: {
		QConicalGradient gradient(CENTER_DR);
		gradient.setColorAt(0.375, activeCol);
		gradient.setColorAt(0.625, col);
                lanePen.setBrush(gradient);
                p->setPen(lanePen);
		p->drawArc(P_CENTER, DELTA_DR);
		break;
	}
	default:
		break;
	}

        lanePen.setColor(col);
        p->setPen(lanePen);

	// vertical line
	switch (type) {
	case ACTIVE:
	case NOT_ACTIVE:
	case MERGE_FORK:
	case MERGE_FORK_R:
	case MERGE_FORK_L:
	case JOIN:
	case JOIN_R:
	case JOIN_L:
	case CROSS:
		p->drawLine(P_90, P_270);
		break;
	case HEAD_L:
	case BRANCH:
		p->drawLine(P_CENTER, P_270);
		break;
	case TAIL_L:
	case INITIAL:
	case BOUNDARY:
	case BOUNDARY_C:
	case BOUNDARY_R:
	case BOUNDARY_L:
		p->drawLine(P_90, P_CENTER);
		break;
	default:
		break;
	}

        lanePen.setColor(activeCol);
        p->setPen(lanePen);

	// horizontal line
	switch (type) {
	case MERGE_FORK:
	case JOIN:
	case HEAD:
	case TAIL:
	case CROSS:
	case CROSS_EMPTY:
	case BOUNDARY_C:
		p->drawLine(P_180, P_0);
		break;
	case MERGE_FORK_R:
	case BOUNDARY_R:
		p->drawLine(P_180, P_CENTER);
		break;
	case MERGE_FORK_L:
	case HEAD_L:
	case TAIL_L:
	case BOUNDARY_L:
		p->drawLine(P_CENTER, P_0);
		break;
	default:
		break;
	}

	// center symbol, e.g. rect or ellipse
	switch (type) {
	case ACTIVE:
	case INITIAL:
	case BRANCH:
		p->setPen(Qt::black);
		p->setBrush(col);
		p->drawEllipse(R_CENTER);
		break;
	case MERGE_FORK:
	case MERGE_FORK_R:
	case MERGE_FORK_L:
		p->setPen(Qt::black);
		p->setBrush(col);
		p->drawRect(R_CENTER);
		break;
	case UNAPPLIED:
		// Red minus sign
		p->setPen(Qt::NoPen);
		p->setBrush(Qt::red);
		p->drawRect(m - r, h - 1, d, 2);
		break;
	case APPLIED:
		// Green plus sign
		p->setPen(Qt::NoPen);
		p->setBrush(DARK_GREEN);
		p->drawRect(m - r, h - 1, d, 2);
		p->drawRect(m - 1, h - r, 2, d);
		break;
	case BOUNDARY:
		p->setPen(Qt::black);
		p->setBrush(back);
		p->drawEllipse(R_CENTER);
		break;
	case BOUNDARY_C:
	case BOUNDARY_R:
	case BOUNDARY_L:
		p->setPen(Qt::black);
		p->setBrush(back);
		p->drawRect(R_CENTER);
		break;
	default:
		break;
	}
	#undef P_CENTER
	#undef P_0
	#undef P_90
	#undef P_180
	#undef P_270
	#undef DELTA_UR
	#undef DELTA_DR
	#undef DELTA_UL
	#undef DELTA_DL
	#undef CENTER_UR
	#undef CENTER_DR
	#undef CENTER_UL
	#undef CENTER_DL
	#undef R_CENTER
}

void ListViewDelegate::paintGraph(QPainter* p, const QStyleOptionViewItem& opt,
                                  const QModelIndex& i) const {
	// static const QColor & baseColor = QPalette().color(QPalette::WindowText);
	static const QColor colors[COLORS_NUM] = {
		QPalette().color(QPalette::WindowText),
		Qt::red, DARK_GREEN,
		Qt::blue, Qt::darkGray, BROWN,
		Qt::magenta, ORANGE
	};
	if (opt.state & QStyle::State_Selected)
		p->fillRect(opt.rect, opt.palette.highlight());
	else if (i.row() & 1)
		p->fillRect(opt.rect, opt.palette.alternateBase());
	else
		p->fillRect(opt.rect, opt.palette.base());

	FileHistory* fh;
	const Rev* r = revLookup(i.row(), &fh);
	if (!r)
		return;

	p->save();
	p->setClipRect(opt.rect, Qt::IntersectClip);
	p->translate(opt.rect.topLeft());

	// calculate lanes
	if (r->lanes.count() == 0)
		git->setLane(r->sha(), fh);

	QBrush back = opt.palette.base();
	const QVector<int>& lanes(r->lanes);
	uint laneNum = lanes.count();
	uint activeLane = 0;
	for (uint i = 0; i < laneNum; i++)
		if (isActive(lanes[i])) {
			activeLane = i;
			break;
		}

	int x1 = 0, x2 = 0;
	int maxWidth = opt.rect.width();
	int lw = laneWidth();
	QColor activeColor = colors[activeLane % COLORS_NUM];
	if (opt.state & QStyle::State_Selected)
		activeColor = blend(activeColor, opt.palette.highlightedText().color(), 208);
	for (uint i = 0; i < laneNum && x2 < maxWidth; i++) {

		x1 = x2;
		x2 += lw;

		int ln = lanes[i];
		if (ln == EMPTY)
			continue;

		QColor color = i == activeLane ? activeColor : colors[i % COLORS_NUM];
		paintGraphLane(p, ln, x1, x2, color, activeColor, back);
	}
	p->restore();
}

void ListViewDelegate::paintLog(QPainter* p, const QStyleOptionViewItem& opt,
                                const QModelIndex& index) const {

	int row = index.row();
	const Rev* r = revLookup(row);
	if (!r)
		return;

	if (r->isDiffCache)
		p->fillRect(opt.rect, changedFiles(ZERO_SHA) ? ORANGE : DARK_ORANGE);

	if (diffTargetRow == row)
		p->fillRect(opt.rect, LIGHT_BLUE);

	bool isHighlighted = lp->isHighlighted(row);
	QPixmap* pm = getTagMarks(r->sha(), opt);

	if (!pm && !isHighlighted) { // fast path in common case
		QItemDelegate::paint(p, opt, index);
		return;
	}
	QStyleOptionViewItem newOpt(opt); // we need a copy
	if (pm) {
		p->drawPixmap(newOpt.rect.x(), newOpt.rect.y() + 1, *pm); // +1 means leave a pixel spacing above the pixmap
		newOpt.rect.adjust(static_cast<int>(pm->width() / dpr()), 0, 0, 0);
		delete pm;
	}
	if (isHighlighted)
                newOpt.font.setBold(true);

	QItemDelegate::paint(p, newOpt, index);
}

void ListViewDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                             const QModelIndex& index) const {

  p->setRenderHints(QPainter::Antialiasing);

	if (index.column() == GRAPH_COL)
		return paintGraph(p, opt, index);

	if (index.column() == LOG_COL)
		return paintLog(p, opt, index);

	return QItemDelegate::paint(p, opt, index);
}

bool ListViewDelegate::changedFiles(SCRef sha) const {

	const RevFile* f = git->getFiles(sha);
	if (f)
		for (int i = 0; i < f->count(); i++)
			if (!f->statusCmp(i, RevFile::UNKNOWN))
				return true;
	return false;
}

// adapt style and name based on type
void getTagMarkParams(QString &name, QStyleOptionViewItem& o,
                      const int type, const bool isCurrent) {
	QColor clr;

	switch (type) {
	case 0: name = "detached"; clr = Qt::red; break;
	case Git::BRANCH: clr = isCurrent ? Qt::green : DARK_GREEN; break;
	case Git::RMT_BRANCH: clr = LIGHT_ORANGE; break;
	case Git::TAG: clr = Qt::yellow; break;
	case Git::REF: clr = PURPLE; break;
	}

	o.palette.setColor(QPalette::Window, clr);
	o.palette.setColor(QPalette::WindowText, QColor(Qt::black));
	o.font.setBold(isCurrent);
}

QPixmap* ListViewDelegate::getTagMarks(SCRef sha, const QStyleOptionViewItem& opt) const {

	uint rt = git->checkRef(sha);
	if (rt == 0)
		return NULL; // common case: no refs at all

	QPixmap* pm = new QPixmap(); // must be deleted by caller

	for (RefNameIterator it(sha, git); it.valid(); it.next()) {
		QStyleOptionViewItem o(opt);
		QString name = it.name();
		getTagMarkParams(name, o, it.type(), it.isCurrentBranch());
		addTextPixmap(&pm, name, o);
	}

	return pm;
}

QString ListView::refNameAt(const QPoint &pos)
{
	QModelIndex index = indexAt(pos);
	if (index.column() != LOG_COL) return QString();

	int spacing = 4; // inner spacing within pixmaps (cf. addTextPixmap)
	int ofs = visualRect(index).left();
	for (RefNameIterator it(sha(index.row()), git); it.valid(); it.next()) {
		QStyleOptionViewItem o;
		QString name = it.name();
		getTagMarkParams(name, o, it.type(), it.isCurrentBranch());

		QFontMetrics fm(o.font);
		ofs += fm.boundingRect(name).width() + 2*spacing;
		if (pos.x() <= ofs) {
			// name found: return fully-qualified ref name (cf. Git::getRefs() for names)
			switch (it.type()) {
			   case Git::BRANCH: return it.name(); break;
			   case Git::TAG: return "tags/" + it.name(); break;
			   case Git::RMT_BRANCH: return "remotes/" + it.name(); break;
			   case Git::REF: return "bases/" + it.name(); break;
			   default: return QString(); break;
			}
		}
		ofs += 2; // distance between pixmaps (cf. addTextPixmap)
	}
	return QString();
}

/*
 * Return the device pixel ratio
 */
qreal ListViewDelegate::dpr(void) const {
#if QT_VERSION >= QT_VERSION_CHECK(5,6,0)
    return qApp->devicePixelRatio();
#else
    return 1.0;
#endif
}

void ListViewDelegate::addTextPixmap(QPixmap** pp, SCRef txt, const QStyleOptionViewItem& opt) const {

	QPixmap* pm = *pp;

	const unsigned int mark_spacing = 2; // Space between markers in pixels
	unsigned int offset = pm->isNull() ? 0 : static_cast<unsigned int>(pm->width() / dpr()) + mark_spacing; // Marker's offset in the base pixmap

	QFontMetrics fm(opt.font);
	const unsigned int text_spacing = 4;
#if QT_VERSION >= QT_VERSION_CHECK(5,11,0)
			unsigned int fmw = fm.horizontalAdvance(txt);
#else
			unsigned int fmw = fm.width(txt)
#endif
	unsigned int text_width = fmw + 2 * text_spacing;
	unsigned int text_height = fm.height();

	// Define size of the new Pixmap
	QSize pixmapSize(static_cast<int>((offset + text_width) * dpr()),
			 static_cast<int>(text_height * dpr()));

	QPixmap* newPm = new QPixmap(pixmapSize);
#if QT_VERSION >= QT_VERSION_CHECK(5,6,0)
    newPm->setDevicePixelRatio(dpr());
#endif

	QPainter p;
	p.begin(newPm);

	newPm->fill(opt.palette.base().color());
	if (!pm->isNull()) p.drawPixmap(0, 0, *pm);

	p.setPen(opt.palette.color(QPalette::WindowText));
	p.setBrush(opt.palette.color(QPalette::Window));
	p.setFont(opt.font);

	p.drawRect(offset, 0, text_width - 1, text_height - 1);
	p.drawText(offset + text_spacing, fm.ascent(), txt);
	p.end();

	delete pm;
	*pp = newPm;
}

// *****************************************************************************

ListViewProxy::ListViewProxy(QObject* p, Domain * dm, Git * g) : QSortFilterProxyModel(p) {

	d = dm;
	git = g;
	colNum = 0;
	isHighLight = false;
	setDynamicSortFilter(false);
}

bool ListViewProxy::isMatch(SCRef sha) const {

	if (colNum == SHA_MAP_COL)
		// in this case shaMap contains all good sha to search for
		return shaSet.contains(sha);

	const Rev* r = git->revLookup(sha);
	if (!r) {
		dbp("ASSERT in ListViewFilter::isMatch, sha <%1> not found", sha);
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
	return (target.contains(filter));
}

bool ListViewProxy::isMatch(int source_row) const {

	FileHistory* fh = d->model();
	if (fh->rowCount() <= source_row) // FIXME required to avoid an ASSERT in d->isMatch()
		return false;

	bool extFilter = (colNum == -1);
	return ((!extFilter && isMatch(fh->sha(source_row)))
	      ||( extFilter && d->isMatch(fh->sha(source_row))));
}

bool ListViewProxy::isHighlighted(int row) const {

	// FIXME row == source_row only because when
	// higlights the rows are not hidden
	return (isHighLight && isMatch(row));
}

bool ListViewProxy::filterAcceptsRow(int source_row, const QModelIndex&) const {

	return (isHighLight || isMatch(source_row));
}

int ListViewProxy::setFilter(bool isOn, bool h, SCRef fl, int cn, ShaSet* s) {

#if QT_VERSION >= 0x060000
	filter = QRegularExpression::fromWildcard(fl, Qt::CaseInsensitive, QRegularExpression::UnanchoredWildcardConversion);
#else
	filter = QRegExp(fl, Qt::CaseInsensitive, QRegExp::Wildcard);
#endif
	colNum = cn;
	if (s)
		shaSet = *s;

	// isHighlighted() is called also when filter is off,
	// so reset 'isHighLight' flag in that case
	isHighLight = h && isOn;

	ListView* lv = static_cast<ListView*>(parent());
	FileHistory* fh = d->model();
	SCRef cur = lv->sha(lv->currentIndex().row());

	if (!isOn && sourceModel()){
		lv->setModel(fh);
		setSourceModel(NULL);

	} else if (isOn && !isHighLight) {
		setSourceModel(fh); // trigger a rows scanning
		lv->setModel(this);
	}
	lv->setCurrentIndex(lv->model()->index(lv->row(cur), 0));
	return (sourceModel() ? rowCount() : 0);
}
