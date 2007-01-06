/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QPainter>
#include <QHeaderView>
#include <QApplication>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPixmap>
#include "common.h"
#include "domain.h"
#include "git.h"
#include "listview.h"

#include "mainimpl.h" // TODO remove, connect(d->m(), SIGNAL(highlightedRowsChanged

using namespace QGit;

ListView::ListView(QWidget* parent) : QTreeView(parent), d(NULL), git(NULL), fh(NULL) {}

void ListView::setup(Domain* dm, Git* g, FileHistory* f) {

	d = dm;
	git = g;
	fh = f;
	st = &(d->st);
	filterNextContextMenuRequest = false;

	setAcceptDrops(git->isMainHistory(fh));
	viewport()->setAcceptDrops(git->isMainHistory(fh));
	viewport()->installEventFilter(this); // filter out some right clicks

	setModel(fh);

	ListViewDelegate* lvd = new ListViewDelegate(git, fh, this);
	lvd->setCellHeight(fontMetrics().height());
	setItemDelegate(lvd);

	setupGeometry(); // after setting delegate

	connect(lvd, SIGNAL(updateView()), viewport(), SLOT(update()));

	connect(this, SIGNAL(diffTargetChanged(int)), lvd, SLOT(diffTargetChanged(int)));

	connect(d->m(), SIGNAL(highlightedRowsChanged(const QSet<int>&)),
	        lvd, SLOT(highlightedRowsChanged(const QSet<int>&)));

	connect(selectionModel(), SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
	        this, SLOT(on_currentChanged(const QModelIndex&, const QModelIndex&)));

	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(on_customContextMenuRequested(const QPoint&)));
}

ListView::~ListView() {

	git->cancelDataLoading(fh); // non blocking
}

void ListView::setupGeometry() {

	QPalette pl = palette();
	pl.setColor(QPalette::Base, ODD_LINE_COL);
	pl.setColor(QPalette::AlternateBase, EVEN_LINE_COL);
	setPalette(pl); // does not seem to inherit application paletteAnnotate

	if (git->isMainHistory(fh))
		hideColumn(ANN_ID_COL);

	QHeaderView* hv = header();
	hv->resizeSection(GRAPH_COL, DEF_GRAPH_COL_WIDTH);
	hv->resizeSection(LOG_COL, DEF_LOG_COL_WIDTH);
	hv->resizeSection(AUTH_COL, DEF_AUTH_COL_WIDTH);
	hv->resizeSection(TIME_COL, DEF_TIME_COL_WIDTH);
}

void ListView::on_repaintListViews(const QFont& f) {

	setFont(f);
	scrollTo(currentIndex());
}

void ListView::clear() {

	git->cancelDataLoading(fh);
	fh->clear(); // reset the model
}

QString ListView::currentText(int column) { // TODO try to remove

	QModelIndex idx = currentIndex().sibling (0, column);
	return (idx.isValid() ? idx.data().toString() : "");
}

int ListView::getLaneType(SCRef sha, int pos) const {

	const Rev* r = git->revLookup(sha, fh);
	return (r && pos < r->lanes.count() && pos >= 0 ? r->lanes.at(pos) : -1);
}

void ListView::showIdValues() {

	if (git->isMainHistory(fh))
		return;

	fh->setAnnIdValid();
	viewport()->update();
}

void ListView::getSelectedItems(QStringList& selectedItems) {

	selectedItems.clear();
	QModelIndexList ml = selectionModel()->selectedRows();
	FOREACH (QModelIndexList, it, ml)
		selectedItems.append(fh->sha((*it).row()));
}

const QString ListView::getSha(uint id) {

	if (git->isMainHistory(fh))
		return "";

	return fh->sha(fh->rowCount() - id);
}

bool ListView::update() {

	int stRow = fh->row(st->sha());
	if (stRow == -1) {
		dbp("ASSERT in ListView::update() st->sha() is <%1>", st->sha());
		return false;
	}
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
		emit diffTargetChanged(fh->row(st->diffToSha()));

	return currentIndex().isValid();
}

// ************************************ SLOTS ********************************

void ListView::on_currentChanged(const QModelIndex& index, const QModelIndex&) {

	SCRef selRev = fh->sha(index.row());
	if (st->sha() != selRev) { // to avoid looping
		st->setSha(selRev);
		st->setSelectItem(true);
		UPDATE_DOMAIN(d);
	}
}

void ListView::mousePressEvent(QMouseEvent* e) {

	if (currentIndex().isValid() && e->button() == Qt::LeftButton)
		d->setReadyToDrag(true);

	QTreeView::mousePressEvent(e);
}

void ListView::mouseReleaseEvent(QMouseEvent* e) {

	d->setReadyToDrag(false); // in case of just click without moving
	QTreeView::mouseReleaseEvent(e);
}

void ListView::mouseMoveEvent(QMouseEvent* e) {

	if (d->isReadyToDrag()) {

		if (!d->setDragging(true))
			return;

		QStringList selRevs;
		getSelectedItems(selRevs);
		selRevs.remove(ZERO_SHA);
		if (!selRevs.empty()) {

			const QString h(d->dragHostName() + '\n');
			QString dragRevs = selRevs.join(h).append(h).stripWhiteSpace();
			QDrag* drag = new QDrag(this);
			QMimeData* mimeData = new QMimeData;
			mimeData->setText(dragRevs);
			drag->setMimeData(mimeData);
			drag->start(); // blocking until drop event
		}
		d->setDragging(false);
	}
	QTreeView::mouseMoveEvent(e);
}

void ListView::dragEnterEvent(QDragEnterEvent* e) {

	if (e->mimeData()->hasFormat("text/plain"))
		e->accept();
}

void ListView::dragMoveEvent(QDragMoveEvent* e) {

	if (e->mimeData()->hasFormat("text/plain"))
		e->accept();
}

void ListView::dropEvent(QDropEvent *e) {

	SCList remoteRevs(e->mimeData()->text().split('\n'));
	if (!remoteRevs.isEmpty()) {
		// some sanity check on dropped data
		SCRef sha(remoteRevs.first().section('@', 0, 0));
		SCRef remoteRepo(remoteRevs.first().section('@', 1));
		if (sha.length() == 40 && !remoteRepo.isEmpty())
			emit droppedRevisions(remoteRevs);
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
	emit contextMenu(fh->sha(index.row()), POPUP_LIST_EV);
}

bool ListView::eventFilter(QObject* obj, QEvent* ev) {
// we need this to filter out some right click mouse events

	if (obj == viewport() && ev->type() == QEvent::MouseButtonPress) {
		QMouseEvent* e = static_cast<QMouseEvent*>(ev);
		if (e->button() == Qt::RightButton)
			return filterRightButtonPressed(e);
	}
	return QObject::eventFilter(obj, ev);
}

bool ListView::filterRightButtonPressed(QMouseEvent* e) {

	QModelIndex index = indexAt(e->pos());
	SCRef sha = fh->sha(index.row());
	if (sha.isEmpty())
		return false;

	if (e->state() == Qt::ControlButton) { // check for 'diff to' function

		if (sha != ZERO_SHA && st->sha() != ZERO_SHA) {

			if (sha != st->diffToSha())
				st->setDiffToSha(sha);
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
		if (getLaneParentsChilds(sha, e->pos().x(), parents, children))
			emit lanesContextMenuRequested(parents, children);

		return true; // filter event out
	}
	return false;
}

bool ListView::getLaneParentsChilds(SCRef sha, int x, SList p, SList c) {

	uint lane = x / laneWidth();
	int t = getLaneType(sha, lane);
	if (t == EMPTY || t == -1)
		return false;

	// first find the parents
	p.clear();
	QString root;
	if (!isFreeLane(t)) {
		p = git->revLookup(sha)->parents(); // pointer cannot be NULL
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
		for (int i = 0; i < f->names.count(); i++)
			if (!f->statusCmp(i, UNKNOWN))
				return true;
	return false;
}

QPixmap* ListViewDelegate::getTagMarks(SCRef sha, const QStyleOptionViewItem& o) const {

	uint rt = git->checkRef(sha);
	if (rt == 0)
		return NULL; // common case

	QPixmap* pm = new QPixmap(); // must be deleted by caller
	QStyleOptionViewItem opt(o);

	if (rt & Git::BRANCH)
		addRefPixmap(&pm, sha, Git::BRANCH, opt);

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
		newPm->fill(opt.palette.base());
		p.drawPixmap(0, 0, *pm);
	}
	p.setPen(Qt::black);
	p.setBrush(opt.palette.color(QPalette::Window));
	p.setFont(opt.font);
	p.drawRect(ofs, 0, pw - 1, ph - 1);
	p.drawText(ofs + spacing, fm.ascent(), txt);
	p.end();

	delete pm;
	*pp = newPm;
}
