/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include "domain.h"
#include "git.h"
#include "listview.h"

using namespace QGit;

ListView::ListView(QWidget* parent) : QTreeView(parent), d(NULL), git(NULL), fh(NULL), lp(NULL) {}

void ListView::setup(Domain* dm, Git* g) {

	d = dm;
	git = g;
	fh = d->model();
	st = &(d->st);
	filterNextContextMenuRequest = false;

	// create ListViewProxy unplugged, will be plug
	// to the model only when filtering is needed
	lp = new ListViewProxy(this, d, git);
	setModel(fh);

	ListViewDelegate* lvd = new ListViewDelegate(git, lp, this);
	lvd->setLaneHeight(fontMetrics().height());
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

	QPalette pl = palette();
	pl.setColor(QPalette::Base, ODD_LINE_COL);
	pl.setColor(QPalette::AlternateBase, EVEN_LINE_COL);
	setPalette(pl); // does not seem to inherit application paletteAnnotate

	QHeaderView* hv = header();
	hv->setStretchLastSection(true);
	hv->setResizeMode(LOG_COL, QHeaderView::Interactive);
	hv->setResizeMode(TIME_COL, QHeaderView::Interactive);
	hv->setResizeMode(ANN_ID_COL, QHeaderView::ResizeToContents);
	hv->resizeSection(GRAPH_COL, DEF_GRAPH_COL_WIDTH);
	hv->resizeSection(LOG_COL, DEF_LOG_COL_WIDTH);
	hv->resizeSection(AUTH_COL, DEF_AUTH_COL_WIDTH);
	hv->resizeSection(TIME_COL, DEF_TIME_COL_WIDTH);

	if (git->isMainHistory(fh))
		hideColumn(ANN_ID_COL);
}

void ListView::scrollToNextHighlighted(int direction) {

	QModelIndex idx = currentIndex();
	do {
		idx = (direction > 0 ? indexBelow(idx) : indexAbove(idx));
		if (!idx.isValid())
			return;

	} while (!lp->isHighlighted(idx.row()));

	setCurrentIndex(idx);
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
}

const QString ListView::shaFromAnnId(uint id) {

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

bool ListView::filterRightButtonPressed(QMouseEvent* e) {

	QModelIndex index = indexAt(e->pos());
	SCRef selSha = sha(index.row());
	if (selSha.isEmpty())
		return false;

	if (e->modifiers() == Qt::ControlModifier) { // check for 'diff to' function

		if (selSha != ZERO_SHA && st->sha() != ZERO_SHA) {

			if (selSha != st->diffToSha())
				st->setDiffToSha(selSha);
			else
				st->setDiffToSha(""); // restore std view

			filterNextContextMenuRequest = true;
			UPDATE_DOMAIN(d);
			return true; // filter event out
		}
	}
	// check for 'children & parents' function, i.e. if mouse is on the graph
	if (index.column() == GRAPH_COL) {

		filterNextContextMenuRequest = true;
		QStringList parents, children;
		if (getLaneParentsChilds(selSha, e->pos().x(), parents, children))
			emit lanesContextMenuRequested(parents, children);

		return true; // filter event out
	}
	return false;
}

void ListView::mousePressEvent(QMouseEvent* e) {

	if (currentIndex().isValid() && e->button() == Qt::LeftButton)
		d->setReadyToDrag(true);

	if (e->button() == Qt::RightButton && filterRightButtonPressed(e))
		return; // filtered out

	QTreeView::mousePressEvent(e);
}

void ListView::mouseReleaseEvent(QMouseEvent* e) {

	d->setReadyToDrag(false); // in case of just click without moving
	QTreeView::mouseReleaseEvent(e);
}

void ListView::mouseMoveEvent(QMouseEvent* e) {

	if (d->isReadyToDrag()) {

		if (indexAt(e->pos()).row() == currentIndex().row())
			return; // move at least by one line to activate drag

		if (!d->setDragging(true))
			return;

		QStringList selRevs;
		getSelectedItems(selRevs);
		selRevs.removeAll(ZERO_SHA);
		if (!selRevs.empty())
			emit revisionsDragged(selRevs); // blocking until drop event

		d->setDragging(false);
	}
	QTreeView::mouseMoveEvent(e);
}

void ListView::dragEnterEvent(QDragEnterEvent* e) {

	if (e->mimeData()->hasFormat("text/plain"))
		e->accept();
}

void ListView::dragMoveEvent(QDragMoveEvent* e) {

	// already checked by dragEnterEvent()
	e->accept();
}

void ListView::dropEvent(QDropEvent *e) {

	SCList remoteRevs(e->mimeData()->text().split('\n', QString::SkipEmptyParts));
	if (!remoteRevs.isEmpty()) {
		// some sanity check on dropped data
		SCRef sha(remoteRevs.first().section('@', 0, 0));
		SCRef remoteRepo(remoteRevs.first().section('@', 1));
		if (sha.length() == 40 && !remoteRepo.isEmpty())
			emit revisionsDropped(remoteRevs);
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

bool ListView::getLaneParentsChilds(SCRef sha, int x, SList p, SList c) {

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
			dbs("ASSERT getLaneParentsChilds: parent not found");
			return false;
		}
		p.append(par);
		root = p.first();
	}
	// then find children
	c = git->getChilds(root);
	return true;
}

// *****************************************************************************

ListViewDelegate::ListViewDelegate(Git* g, ListViewProxy* px, QObject* p) : QItemDelegate(p) {

	git = g;
	lp = px;
	_laneHeight = 0;
	_diffTargetRow = -1;
}

QSize ListViewDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {

	return QSize(laneWidth(), _laneHeight);
}

void ListViewDelegate::diffTargetChanged(int row) {

	if (_diffTargetRow != row) {
		_diffTargetRow = row;
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

void ListViewDelegate::paintGraphLane(QPainter* p, int type, int x1, int x2,
                                      const QColor& col, const QBrush& back) const {

	int h = _laneHeight / 2;
	int m = (x1 + x2) / 2;
	int r = (x2 - x1) / 3;
	int d =  2 * r;

	#define P_CENTER m , h
	#define P_0      x2, h
	#define P_90     m , 0
	#define P_180    x1, h
	#define P_270    m , 2 * h
	#define R_CENTER m - r, h - r, d, d

	static QPen myPen(Qt::black, 2); // fast path here
	myPen.setColor(col);
	p->setPen(myPen);

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
		p->drawLine(P_90, P_270);
		break;
	case HEAD:
	case HEAD_R:
	case HEAD_L:
	case BRANCH:
		p->drawLine(P_CENTER, P_270);
		break;
	case TAIL:
	case TAIL_R:
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
	case JOIN_R:
	case HEAD_R:
	case TAIL_R:
	case BOUNDARY_R:
		p->drawLine(P_180, P_CENTER);
		break;
	case MERGE_FORK_L:
	case JOIN_L:
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
		p->setPen(Qt::NoPen);
		p->setBrush(col);
		p->drawEllipse(R_CENTER);
		break;
	case MERGE_FORK:
	case MERGE_FORK_R:
	case MERGE_FORK_L:
		p->setPen(Qt::NoPen);
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
		p->setBrush(back);
		p->drawEllipse(R_CENTER);
		break;
	case BOUNDARY_C:
	case BOUNDARY_R:
	case BOUNDARY_L:
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
	#undef R_CENTER
}

void ListViewDelegate::paintGraph(QPainter* p, const QStyleOptionViewItem& opt,
                                  const QModelIndex& i) const {

	static const QColor colors[COLORS_NUM] = { Qt::black, Qt::red, DARK_GREEN,
	                                           Qt::blue, Qt::darkGray, BROWN,
	                                           Qt::magenta, ORANGE };
	if (opt.state & QStyle::State_Selected)
		p->fillRect(opt.rect, opt.palette.highlight());
	else
		p->fillRect(opt.rect, opt.palette.base());

	FileHistory* fh;
	const Rev* r = revLookup(i.row(), &fh);
	if (!r)
		return;

	p->save();
	p->translate(QPoint(opt.rect.left(), opt.rect.top()));

	// calculate lanes
	if (r->lanes.count() == 0)
		git->setLane(r->sha(), fh);

	QBrush back = opt.palette.base();
	const QVector<int>& lanes(r->lanes);
	uint laneNum = lanes.count();
	uint mergeLane = 0;
	for (uint i = 0; i < laneNum; i++)
		if (isMerge(lanes[i])) {
			mergeLane = i;
			break;
		}

	int x1 = 0, x2 = 0;
	int maxWidth = opt.rect.right();
	int lw = laneWidth();
	for (uint i = 0; i < laneNum && x2 < maxWidth; i++) {

		x1 = x2;
		x2 += lw;

		int ln = lanes[i];
		if (ln == EMPTY)
			continue;

		uint col = (   isHead(ln) || isTail(ln) || isJoin(ln)
		            || ln == CROSS_EMPTY) ? mergeLane : i;

		if (ln == CROSS) {
			paintGraphLane(p, NOT_ACTIVE, x1, x2, colors[col % COLORS_NUM], back);
			paintGraphLane(p, CROSS, x1, x2, colors[mergeLane % COLORS_NUM], back);
		} else
			paintGraphLane(p, ln, x1, x2, colors[col % COLORS_NUM], back);
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

	if (_diffTargetRow == row)
		p->fillRect(opt.rect, LIGHT_BLUE);

	bool isHighlighted = lp->isHighlighted(row);
	QPixmap* pm = getTagMarks(r->sha(), opt);

	if (!pm && !isHighlighted) { // fast path in common case
		QItemDelegate::paint(p, opt, index);
		return;
	}
	QStyleOptionViewItem newOpt(opt); // we need a copy
	if (pm) {
		p->drawPixmap(newOpt.rect.x(), newOpt.rect.y(), *pm);
		newOpt.rect.adjust(pm->width(), 0, 0, 0);
		delete pm;
	}
	if (isHighlighted)
		newOpt.font.setBold(true);

	QItemDelegate::paint(p, newOpt, index);
}

void ListViewDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                             const QModelIndex& index) const {

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

QPixmap* ListViewDelegate::getTagMarks(SCRef sha, const QStyleOptionViewItem& opt) const {

	uint rt = git->checkRef(sha);
	if (rt == 0)
		return NULL; // common case

	QPixmap* pm = new QPixmap(); // must be deleted by caller

	if (rt & Git::BRANCH)
		addRefPixmap(&pm, sha, Git::BRANCH, opt);

	if (rt & Git::RMT_BRANCH)
		addRefPixmap(&pm, sha, Git::RMT_BRANCH, opt);

	if (rt & Git::TAG)
		addRefPixmap(&pm, sha, Git::TAG, opt);

	if (rt & Git::REF)
		addRefPixmap(&pm, sha, Git::REF, opt);

	return pm;
}

void ListViewDelegate::addRefPixmap(QPixmap** pp, SCRef sha, int type, QStyleOptionViewItem opt) const {

	QString curBranch;
	SCList refs = git->getRefName(sha, (Git::RefType)type, &curBranch);
	FOREACH_SL (it, refs) {

		bool isCur = (curBranch == *it);
		opt.font.setBold(isCur);

		QColor clr;
		if (type == Git::BRANCH)
			clr = (isCur ? Qt::green : DARK_GREEN);

		else if (type == Git::RMT_BRANCH)
			clr = LIGHT_ORANGE;

		else if (type == Git::TAG)
			clr = Qt::yellow;

		else if (type == Git::REF)
			clr = PURPLE;

		opt.palette.setColor(QPalette::Window, clr);
		addTextPixmap(pp, *it, opt);
	}
}

void ListViewDelegate::addTextPixmap(QPixmap** pp, SCRef txt, const QStyleOptionViewItem& opt) const {

	QPixmap* pm = *pp;
	int ofs = pm->isNull() ? 0 : pm->width() + 2;
	int spacing = 2;
	QFontMetrics fm(opt.font);
	int pw = fm.boundingRect(txt).width() + 2 * (spacing + int(opt.font.bold()));
	int ph = fm.height() - 1; // leave vertical space between two consecutive tags

	QPixmap* newPm = new QPixmap(ofs + pw, ph);
	QPainter p;
	p.begin(newPm);
	if (!pm->isNull()) {
		newPm->fill(opt.palette.base().color());
		p.drawPixmap(0, 0, *pm);
	}
	p.setPen(opt.palette.color(QPalette::WindowText));
	p.setBrush(opt.palette.color(QPalette::Window));
	p.setFont(opt.font);
	p.drawRect(ofs, 0, pw - 1, ph - 1);
	p.drawText(ofs + spacing, fm.ascent(), txt);
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

	filter = QRegExp(fl, Qt::CaseInsensitive, QRegExp::Wildcard);
	colNum = cn;
	if (s)
		shaSet = *s;

	// isHighlighted() is called also when filter is off,
	// so reset 'isHighLight' flag in that case
	isHighLight = h && isOn;

	ListView* lv = static_cast<ListView*>(parent());
	FileHistory* fh = d->model();

	if (!isOn && sourceModel()){
		lv->setModel(fh);
		setSourceModel(NULL);

	} else if (isOn && !isHighLight) {
		setSourceModel(fh); // trigger a rows scanning
		lv->setModel(this);
	}
	return (sourceModel() ? rowCount() : 0);
}
