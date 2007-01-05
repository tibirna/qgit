/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QHeaderView>
#include <qpainter.h>
#include <qapplication.h>
#include <q3header.h>
#include <q3dragobject.h>
#include <qdatetime.h>
//Added by qt3to4:
#include <QPixmap>
#include <QDropEvent>
#include <QMouseEvent>
#include <QEvent>
#include "common.h"
#include "domain.h"
#include "git.h"
#include "listview.h"

#include "mainimpl.h" // TODO remove

using namespace QGit;

ListView::ListView(Domain* dm, Git* g, QTreeView* l, FileHistory* f, const QFont& fnt) :
                   QObject(dm), d(dm), git(g), lv(l), fh(f) {

	st = &(d->st);
	filterNextContextMenuRequest = false;
	setupListView(fnt);
	clear(); // to init some stuff

	lv->setAcceptDrops(git->isMainHistory(fh));
	lv->viewport()->setAcceptDrops(git->isMainHistory(fh));
	lv->viewport()->installEventFilter(this); // filter out some right clicks

// FIXME
// 	connect(lv, SIGNAL(mouseButtonPressed(int,Q3ListViewItem*,const QPoint&,int)),
// 	        this, SLOT(on_mouseButtonPressed(int,Q3ListViewItem*,const QPoint&,int)));
//
// 	connect(lv, SIGNAL(clicked(Q3ListViewItem*)),
// 	        this, SLOT(on_clicked(Q3ListViewItem*)));
//
// 	connect(lv, SIGNAL(onItem(Q3ListViewItem*)),
// 	        this, SLOT(on_onItem(Q3ListViewItem*)));

	QTreeView* tv = lv;

	QPalette pl = tv->palette();
	pl.setColor(QPalette::Base, ODD_LINE_COL);
	pl.setColor(QPalette::AlternateBase, EVEN_LINE_COL);
	tv->setPalette(pl); // does not seem to inherit application palette

	tv->setModel(fh);

	ListViewDelegate* lvd = new ListViewDelegate(git, fh, this);
	lvd->setCellHeight(tv->fontMetrics().height());
	tv->setItemDelegate(lvd);

	if (git->isMainHistory(fh))
		tv->hideColumn(ANN_ID_COL);

	tv->header()->setStretchLastSection(true);
	int w = tv->columnWidth(LOG_COL);
	tv->setColumnWidth(LOG_COL, w * 4);
	tv->setColumnWidth(AUTH_COL, w * 2);

	connect(lvd, SIGNAL(updateView()), tv->viewport(), SLOT(update()));

	connect(this, SIGNAL(diffTargetChanged(int)), lvd, SLOT(diffTargetChanged(int)));

	connect(d->m(), SIGNAL(highlightedRowsChanged(const QSet<int>&)),
	        lvd, SLOT(highlightedRowsChanged(const QSet<int>&)));

	connect(lv->selectionModel(), SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
	        this, SLOT(on_currentChanged(const QModelIndex&, const QModelIndex&)));

	connect(lv, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(on_customContextMenuRequested(const QPoint&)));
}

ListView::~ListView() {

	git->cancelDataLoading(fh); // non blocking
}

void ListView::setupListView(const QFont& fnt) {

// 	lv->setItemMargin(0);
// 	lv->setSorting(-1);
// 	lv->setFont(fnt);
//
// 	int adj = !git->isMainHistory(fh) ? 0 : -1;
//
// 	lv->setColumnWidthMode(GRAPH_COL, Q3ListView::Manual);
// 	lv->setColumnWidthMode(LOG_COL + adj, Q3ListView::Manual);
// 	lv->setColumnWidthMode(AUTH_COL + adj, Q3ListView::Manual);
// 	lv->setColumnWidthMode(TIME_COL + adj, Q3ListView::Maximum); // width is almost constant
//
// 	lv->setColumnWidth(GRAPH_COL, DEF_GRAPH_COL_WIDTH);
// 	lv->setColumnWidth(LOG_COL + adj, DEF_LOG_COL_WIDTH);
// 	lv->setColumnWidth(AUTH_COL + adj, DEF_AUTH_COL_WIDTH);
// 	lv->setColumnWidth(TIME_COL + adj, DEF_TIME_COL_WIDTH);
//
// 	lv->header()->setStretchEnabled(false);
// 	lv->header()->setStretchEnabled(true, LOG_COL + adj);
}

void ListView::on_repaintListViews(const QFont& f) {

	lv->setFont(f);
// 	lv->ensureItemVisible(lv->currentItem()); FIXME
}

void ListView::clear() {

	git->cancelDataLoading(fh);
// 	lv->clear(); // FIXME SHOULD NOT BE USED, start from model instead
// 	diffTarget = NULL; // avoid a dangling pointer FIXME

// 	int adj = !git->isMainHistory(fh) ? 0 : -1;
//
// 	if (testFlag(REL_DATE_F)) {
// 		secs = QDateTime::currentDateTime().toTime_t();
// 		lv->setColumnText(TIME_COL + adj, "Last Change");
// 	} else {
// 		secs = 0;
// 		lv->setColumnText(TIME_COL + adj, "Author Date");
// 	}
}

QString ListView::currentText(int column) {

	QModelIndex idx = lv->currentIndex().sibling (0, column);
	return (idx.isValid() ? idx.data().toString() : "");
}

int ListView::getLaneType(int pos) const {

	QModelIndex idx = lv->currentIndex();
	if (!idx.isValid())
		return -1;

	SCRef sha = fh->revOrder.at(idx.row());
	const Rev* r = git->revLookup(sha, fh); // 'r' cannot be NULL
	return (pos < r->lanes.count() ? r->lanes[pos] : -1);
}

void ListView::updateIdValues() {

	if (git->isMainHistory(fh))
		return;

	uint id = fh->rowCount();
// 	Q3ListViewItem* item = lv->firstChild(); FIXME
// 	while (item) {
// 		item->setText(ANN_ID_COL, QString::number(id--) + "  ");
// 		item = item->itemBelow();
// 	}
}

void ListView::getSelectedItems(QStringList& selectedItems) {

// 	selectedItems.clear(); FIXME
// 	Q3ListViewItem* item = lv->firstChild();
// 	while (item) {
// 		if (item->isSelected())
// 			selectedItems.append(((ListViewItem*)item)->sha());
//
// 		item = item->itemBelow();
// 	}
}

const QString ListView::getSha(int id) {

	if (git->isMainHistory(fh) || !fh)
		return "";

	// id == rowCnt - row
	int row = fh->rowCount() - id;
	if (row < 0 || row >= fh->rowCount())
		return "";

	return fh->revOrder.at(row);

// 	// check to early skip common case of list mouse browsing
// 	Q3ListViewItem* item = lv->currentItem();
// 	if (item && item->text(ANN_ID_COL).toInt() == id)
// 		return ((ListViewItem*)item)->sha();
//
// 	item = lv->firstChild();
// 	while (item) {
// 		if (item->text(ANN_ID_COL).toInt() == id)
// 			return ((ListViewItem*)item)->sha();
//
// 		item = item->itemBelow();
// 	}
}

bool ListView::update() {

	QModelIndex index = lv->currentIndex();
	int newRow = fh->row(st->sha());
	QItemSelectionModel* sel = lv->selectionModel();

	if (index.isValid() && (index.row() == newRow)) {

		if (sel->isSelected(index) != st->selectItem())
			sel->setCurrentIndex(index, QItemSelectionModel::Toggle);

// 		lv->ensureItemVisible(item);
	} else {
		// setCurrentItem() does not clear previous
		// selections in a multi selection QListView
		lv->clearSelection();

		QModelIndex newIndex = lv->model()->index(newRow, 0);
		lv->setCurrentIndex(newIndex); // calls on_currentChanged()
		if (!st->selectItem())
			sel->setCurrentIndex(index, QItemSelectionModel::Toggle);

// 		lv->ensureItemVisible(item);
	}
	if (git->isMainHistory(fh)) {
// 		setHighlight(st->diffToSha());

		// NEW MODEL VIEW INTERFACE HOOK
		// we use orderIdx to get row number, it will be
		// got directly in final integration
		const Rev* r = git->revLookup(st->diffToSha(), fh);
		emit diffTargetChanged(r ? r->orderIdx : -1);
	}
	return lv->currentIndex().isValid();
}

// ************************************ SLOTS ********************************

void ListView::on_currentChanged(const QModelIndex& index, const QModelIndex&) {

	if (!index.isValid())
		return;

	SCRef selRev = fh->revOrder.at(index.row());
	if (st->sha() != selRev) { // to avoid looping
		st->setSha(selRev);
		st->setSelectItem(true);
		UPDATE_DOMAIN(d);
	}
}

void ListView::on_mouseButtonPressed(int b, Q3ListViewItem* item, const QPoint&, int) {

	if (item && b == Qt::LeftButton)
		d->setReadyToDrag(true);
}

void ListView::on_clicked(Q3ListViewItem*) {

	d->setReadyToDrag(false); // in case of just click without moving
}

void ListView::on_onItem(Q3ListViewItem*) {

	if (!d->isReadyToDrag() || !d->setDragging(true))
		return;

	QStringList selRevs;
	getSelectedItems(selRevs);
	selRevs.remove(ZERO_SHA);

	if (!selRevs.empty()) {
		const QString h(d->dragHostName() + '\n');
		QString dragRevs = selRevs.join(h).append(h).stripWhiteSpace();
		Q3DragObject* drObj = new Q3TextDrag(dragRevs, lv);
		drObj->dragCopy(); // do not delete drObj. Blocking until drop event
	}
	d->setDragging(false);
}

void ListView::on_customContextMenuRequested(const QPoint& pos) {

	QModelIndex index = lv->indexAt(pos);
	if (!index.isValid())
		return;

	if (filterNextContextMenuRequest) {
		// event filter does not work on them
		filterNextContextMenuRequest = false;
		return;
	}
	emit contextMenu(fh->revOrder.at(index.row()), POPUP_LIST_EV);
}


bool ListView::eventFilter(QObject* obj, QEvent* ev) {
// we need an event filter for:
//  - filter out some right click mouse events
//  - intercept drop events sent to listview

	if (obj == lv->viewport() && ev->type() == QEvent::MouseButtonPress) {
		QMouseEvent* e = static_cast<QMouseEvent*>(ev);
		if (e->button() == Qt::RightButton)
			return filterRightButtonPressed(e);
	}
	if (obj == lv->viewport() && ev->type() == QEvent::Drop) {
		QDropEvent* e = static_cast<QDropEvent*>(ev);
		return filterDropEvent(e);
	}
	return QObject::eventFilter(obj, ev);
}

bool ListView::filterRightButtonPressed(QMouseEvent* e) {

	QModelIndex index = lv->indexAt(e->pos());
// 	ListViewItem* item = static_cast<ListViewItem*>(lv->itemAt(e->pos()));
	if (!index.isValid())
		return false;

	if (e->state() == Qt::ControlButton) { // check for 'diff to' function

		SCRef diffToSha(fh->revOrder.at(index.row()));

		if (diffToSha != ZERO_SHA && st->sha() != ZERO_SHA) {

			if (diffToSha != st->diffToSha())
				st->setDiffToSha(diffToSha);
			else
				st->setDiffToSha(""); // restore std view

			UPDATE_DOMAIN(d);
			filterNextContextMenuRequest = true;
			return true; // filter event out
		}
	}
	// check for 'children & parents' function, i.e. if mouse is on the graph
	if (index.column() == GRAPH_COL) {
		QStringList parents, children;
// 		if (getLaneParentsChilds(item, e->pos().x(), parents, children)) FIXME
// 			emit lanesContextMenuRequested(parents, children);

		return true; // filter event out
	}
	return false;
}

bool ListView::getLaneParentsChilds(ListViewItem* item, int x, SList p, SList c) {

// 	uint lane = x / item->laneWidth(); FIXME
// 	int t = item->getLaneType(lane);
// 	if (t == EMPTY || t == -1)
// 		return false;
//
// 	// first find the parents
// 	p.clear();
// 	QString root;
// 	SCRef sha(item->sha());
// 	if (!isFreeLane(t)) {
// 		p = git->revLookup(sha)->parents(); // pointer cannot be NULL
// 		root = sha;
// 	} else {
// 		SCRef par(git->getLaneParent(sha, lane));
// 		if (par.isEmpty()) {
// 			dbs("ASSERT getLaneParentsChilds: parent not found");
// 			return false;
// 		}
// 		p.append(par);
// 		root = p.first();
// 	}
// 	// then find children
// 	c = git->getChilds(root);
	return true;
}

bool ListView::filterDropEvent(QDropEvent* e) {

	QString text;
	if (Q3TextDrag::decode(e, text) && !text.isEmpty()) {

		SCList remoteRevs(QStringList::split('\n', text));

		// some sanity check on dropped data
		SCRef sha(remoteRevs[0].section('@', 0, 0));
		SCRef remoteRepo(remoteRevs[0].section('@', 1));

		if (sha.length() == 40 && !remoteRepo.isEmpty())
			emit droppedRevisions(remoteRevs);
	}
	return true; // filter out
}


// *****************************************************************************

ListViewDelegate::ListViewDelegate(Git* g, FileHistory* f, QObject* p) : QItemDelegate(p) {

	git = g;
	fh = f;
	_cellHeight = _cellWidth = 0;
	_diffTargetRow = -1;
}

void ListViewDelegate::setCellHeight(int h) {

	_cellHeight = h;
	_cellWidth = 3 * _cellHeight / 4;
}

QSize ListViewDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {

	return QSize(_cellWidth, _cellHeight);
}

void ListViewDelegate::diffTargetChanged(int row) {

	if (_diffTargetRow != row) {
		_diffTargetRow = row;
		emit updateView();
	}
}

void ListViewDelegate::highlightedRowsChanged(const QSet<int>& rows) {

	_hlRows.clear();
	_hlRows.unite(rows);
	emit updateView();
}

void ListViewDelegate::paintGraphLane(QPainter* p, int type, int x1, int x2,
                                      const QColor& col, const QBrush& back) const {

	int h = _cellHeight / 2;
	int m = (x1 + x2) / 2;
	int r = (x2 - x1) / 3;
	int d =  2 * r;

	#define P_CENTER m , h
	#define P_0      x2, h
	#define P_90     m , 0
	#define P_180    x1, h
	#define P_270    m , 2 * h
	#define R_CENTER m - r, h - r, d, d

	p->setPen(QPen(col, 2));

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

	const Rev* r = git->revLookup(fh->revOrder.at(i.row()));
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
	for (uint i = 0; i < laneNum && x2 < maxWidth; i++) {

		x1 = x2;
		x2 += _cellWidth;

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
	const Rev* r = git->revLookup(fh->revOrder.at(row));
	if (!r)
		return;

	if (r->isDiffCache)
		p->fillRect(opt.rect, changedFiles(ZERO_SHA) ? ORANGE : DARK_ORANGE);

	if (_diffTargetRow == row)
		p->fillRect(opt.rect, LIGHT_BLUE);

	bool isHighlighted = (!_hlRows.isEmpty() && _hlRows.contains(row));
	QPixmap* pm = getTagMarks(r->sha());

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
		for (int i = 0; i < f->names.count(); i++)
			if (!f->statusCmp(i, UNKNOWN))
				return true;
	return false;
}

QPixmap* ListViewDelegate::getTagMarks(SCRef sha) const {

	uint rt = git->checkRef(sha);
	if (rt == 0)
		return NULL; // common case

	QPixmap* pm = new QPixmap(); // must be deleted by caller

	if (rt & Git::BRANCH)
		addBranchPixmap(&pm, sha);

	if (rt & Git::TAG)
		addRefPixmap(&pm, git->getRefName(sha, Git::TAG), Qt::yellow);

	if (rt & Git::REF)
		addRefPixmap(&pm, git->getRefName(sha, Git::REF), PURPLE);

	return pm;
}

void ListViewDelegate::addBranchPixmap(QPixmap** pp, SCRef sha) const {

	QString curBranch;
	SCList refs = git->getRefName(sha, Git::BRANCH, &curBranch);
	FOREACH_SL (it, refs) {
		bool isCur = (curBranch == *it);
		QColor color(isCur ? Qt::green : DARK_GREEN);
		addTextPixmap(pp, *it, color, isCur);
	}
}

void ListViewDelegate::addRefPixmap(QPixmap** pp, SCList refs, const QColor& clr) const {

	FOREACH_SL (it, refs)
		addTextPixmap(pp, *it, clr, false);
}

void ListViewDelegate::addTextPixmap(QPixmap** pp, SCRef txt, const QColor& clr, bool bld) const {

	QFont fnt; //QFont fnt(myListView()->font()); FIXME
	if (bld)
		fnt.setBold(true);

	QFontMetrics fm(fnt);
	QPixmap* pm = *pp;
	int ofs = pm->isNull() ? 0 : pm->width() + 2;
	int spacing = 2;
	int pw = fm.boundingRect(txt).width() + 2 * (spacing + int(bld));
	int ph = fm.height() - 1; // leave vertical space between two consecutive tags

	QPixmap* newPm = new QPixmap(ofs + pw, ph);

	QPainter p;
	p.begin(newPm);
	if (!pm->isNull()) {
		newPm->fill(QApplication::palette().base());
		p.drawPixmap(0, 0, *pm);
	}
	p.setPen(Qt::black);
	p.setBrush(clr);
	p.setFont(fnt);
	p.drawRect(ofs, 0, pw - 1, ph - 1);
	p.drawText(ofs + spacing, fm.ascent(), txt);
	p.end();

	delete pm;
	*pp = newPm;
}
