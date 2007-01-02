/*
	Description: qgit revision list view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
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

using namespace QGit;

ListView::ListView(Domain* dm, Git* g, Q3ListView* l, FileHistory* f, const QFont& fnt) :
                   QObject(dm), d(dm), git(g), lv(l), fh(f) {

	st = &(d->st);
	filterNextContextMenuRequest = false;
	setupListView(fnt);
	clear(); // to init some stuff

	lv->setAcceptDrops(git->isMainHistory(fh));
	lv->viewport()->setAcceptDrops(git->isMainHistory(fh));
	lv->viewport()->installEventFilter(this); // filter out some right clicks

	connect(lv, SIGNAL(currentChanged(Q3ListViewItem*)),
	        this, SLOT(on_currentChanged(Q3ListViewItem*)));

	connect(lv, SIGNAL(mouseButtonPressed(int,Q3ListViewItem*,const QPoint&,int)),
	        this, SLOT(on_mouseButtonPressed(int,Q3ListViewItem*,const QPoint&,int)));

	connect(lv, SIGNAL(clicked(Q3ListViewItem*)),
	        this, SLOT(on_clicked(Q3ListViewItem*)));

	connect(lv, SIGNAL(onItem(Q3ListViewItem*)),
	        this, SLOT(on_onItem(Q3ListViewItem*)));

	connect(lv, SIGNAL(contextMenuRequested(Q3ListViewItem*, const QPoint&,int)),
	        this, SLOT(on_contextMenuRequested(Q3ListViewItem*)));
}

ListView::~ListView() {

	git->cancelDataLoading(fh); // non blocking
}

void ListView::setupListView(const QFont& fnt) {

	lv->setItemMargin(0);
	lv->setSorting(-1);
	lv->setFont(fnt);

	int adj = !git->isMainHistory(fh) ? 0 : -1;

	lv->setColumnWidthMode(GRAPH_COL, Q3ListView::Manual);
	lv->setColumnWidthMode(LOG_COL + adj, Q3ListView::Manual);
	lv->setColumnWidthMode(AUTH_COL + adj, Q3ListView::Manual);
	lv->setColumnWidthMode(TIME_COL + adj, Q3ListView::Maximum); // width is almost constant

	lv->setColumnWidth(GRAPH_COL, DEF_GRAPH_COL_WIDTH);
	lv->setColumnWidth(LOG_COL + adj, DEF_LOG_COL_WIDTH);
	lv->setColumnWidth(AUTH_COL + adj, DEF_AUTH_COL_WIDTH);
	lv->setColumnWidth(TIME_COL + adj, DEF_TIME_COL_WIDTH);

	lv->header()->setStretchEnabled(false);
	lv->header()->setStretchEnabled(true, LOG_COL + adj);
}

void ListView::on_repaintListViews(const QFont& f) {

	lv->setFont(f);
	lv->ensureItemVisible(lv->currentItem());
}

void ListView::clear() {

	git->cancelDataLoading(fh);
	lv->clear();
	diffTarget = NULL; // avoid a dangling pointer

	int adj = !git->isMainHistory(fh) ? 0 : -1;

	if (testFlag(REL_DATE_F)) {
		secs = QDateTime::currentDateTime().toTime_t();
		lv->setColumnText(TIME_COL + adj, "Last Change");
	} else {
		secs = 0;
		lv->setColumnText(TIME_COL + adj, "Author Date");
	}
}

void ListView::updateIdValues() {

	if (git->isMainHistory(fh))
		return;

	uint id = lv->childCount();
	Q3ListViewItem* item = lv->firstChild();
	while (item) {
		item->setText(ANN_ID_COL, QString::number(id--) + "  ");
		item = item->itemBelow();
	}
}

void ListView::getSelectedItems(QStringList& selectedItems) {

	selectedItems.clear();
	Q3ListViewItem* item = lv->firstChild();
	while (item) {
		if (item->isSelected())
			selectedItems.append(((ListViewItem*)item)->sha());

		item = item->itemBelow();
	}
}

const QString ListView::getSha(int id) {

	if (git->isMainHistory(fh))
		return "";

	// check to early skip common case of list mouse browsing
	Q3ListViewItem* item = lv->currentItem();
	if (item && item->text(ANN_ID_COL).toInt() == id)
		return ((ListViewItem*)item)->sha();

	item = lv->firstChild();
	while (item) {
		if (item->text(ANN_ID_COL).toInt() == id)
			return ((ListViewItem*)item)->sha();

		item = item->itemBelow();
	}
	return "";
}

ListViewItem* ListView::findItemSha(SCRef sha) const {
// code taken from QListView::findItem() sources

	if (sha.isEmpty())
		return NULL;

	Q3ListViewItemIterator it(lv->currentItem() ? lv->currentItem() : lv->firstChild());
	Q3ListViewItem *sentinel = NULL;
	ListViewItem *item;

	for (int pass = 0; pass < 2; pass++) {
		while ((item = (ListViewItem*)it.current()) != sentinel) {
			if (sha == item->sha())
				return item;
			++it;
		}
		it = Q3ListViewItemIterator(lv->firstChild());
		sentinel = lv->currentItem() ? lv->currentItem() : lv->firstChild();
	}
	return NULL;
}

void ListView::setHighlight(SCRef diffToSha) {

	if (diffTarget && diffTarget->sha() == diffToSha)
		return;

	// remove highlight on any previous target
	if (diffTarget) {
		diffTarget->setDiffTarget(false);
		diffTarget = NULL;
	}
	if (diffToSha.isEmpty())
		return;

	diffTarget = findItemSha(diffToSha);
	if (diffTarget && (diffTarget->sha() != ZERO_SHA))
		diffTarget->setDiffTarget(true); // do highlight
}

bool ListView::update() {

	ListViewItem* item = static_cast<ListViewItem*>(lv->currentItem());

	if (item && (item->sha() == st->sha())) {
		lv->setSelected(item, st->selectItem()); // just a refresh
		lv->ensureItemVisible(item);
	} else {
		// setCurrentItem() does not clear previous
		// selections in a multi selection QListView
		lv->clearSelection();

		item = findItemSha(st->sha());
		if (item) {
			lv->setCurrentItem(item); // calls on_currentChanged()
			lv->setSelected(item, st->selectItem());
			lv->ensureItemVisible(item);
		}
	}
	if (git->isMainHistory(fh))
		setHighlight(st->diffToSha());

	return (item != NULL);
}

// ************************************ SLOTS ********************************

void ListView::on_newRevsAdded(const FileHistory* f, const QVector<QString>& shaVec) {

	if (f != fh) // signal newRevsAdded() is broadcast
		return;

	bool evenLine = !(lv->childCount() % 2);

	if (lv->childCount() == 0)
		lastItem = NULL;

	lv->setUpdatesEnabled(false);
	for (int i = lv->childCount(); i < shaVec.count(); i++) {
		lastItem = new ListViewItem(lv, lastItem, git, shaVec[i], evenLine, secs, fh);
		evenLine = !evenLine;
	}
	lv->setUpdatesEnabled(true);
}

void ListView::on_currentChanged(Q3ListViewItem* item) {

	SCRef selRev(item ? (static_cast<ListViewItem*>(item))->sha() : "");
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

void ListView::on_contextMenuRequested(Q3ListViewItem* item) {

	if (!item)
		return;

	if (filterNextContextMenuRequest) {
		// event filter does not work on them
		filterNextContextMenuRequest = false;
		return;
	}
	emit contextMenu(((ListViewItem*)item)->sha(), POPUP_LIST_EV);
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

	ListViewItem* item = static_cast<ListViewItem*>(lv->itemAt(e->pos()));
	if (!item)
		return false;

	if (e->state() == Qt::ControlButton) { // check for 'diff to' function

		SCRef diffToSha(item->sha());

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
	int column = lv->header()->sectionAt(e->pos().x());
	if (column == GRAPH_COL) {
		QStringList parents, children;
		if (getLaneParentsChilds(item, e->pos().x(), parents, children))
			emit lanesContextMenuRequested(parents, children);

		return true; // filter event out
	}
	return false;
}

bool ListView::getLaneParentsChilds(ListViewItem* item, int x, SList p, SList c) {

	uint lane = x / item->laneWidth();
	int t = item->getLaneType(lane);
	if (t == EMPTY || t == -1)
		return false;

	// first find the parents
	p.clear();
	QString root;
	SCRef sha(item->sha());
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

// ****************************** ListViewItem *****************************

ListViewItem::ListViewItem(Q3ListView* p, ListViewItem* a, Git* g, SCRef s,
              bool e, unsigned long t, FileHistory* f) : Q3ListViewItem(p, a),
              listView_(p), git(g), fh(f), _sha(s), secs(t), isEvenLine(e) {

	populated = isDiffTarget = isHighlighted = false;
}

int ListViewItem::getLaneType(int pos) const {

	const Rev* r = git->revLookup(_sha, fh); // 'r' cannot be NULL
	return (pos < r->lanes.count() ? r->lanes[pos] : -1);
}

void ListViewItem::setDiffTarget(bool b) {

	isDiffTarget = b;
	repaint();
}

/* Draw graph part for a lane
 */
void ListViewItem::paintGraphLane(QPainter* p, int type, int x1, int x2,
                                  const QColor& col, const QBrush& back) {

	int h =  height() / 2;
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

void ListViewItem::paintGraph(const Rev& c, QPainter* p, const QColorGroup& cg, int width) {

	static const QColor colors[COLORS_NUM] = { Qt::black, Qt::red, DARK_GREEN,
	                                           Qt::blue,  Qt::darkGray, BROWN,
	                                           Qt::magenta, ORANGE };
	Q3ListView* lv = myListView();
	if (!lv)
		return;

	QPalette::ColorRole cr = QPalette::Base;
	if (isSelected() && lv->allColumnsShowFocus())
		cr = QPalette::Highlight;

	QBrush back = cg.brush(cr);
	p->fillRect(0, 0, width, height(), back);

	const QVector<int>& lanes(c.lanes);
	uint laneNum = lanes.count();
	uint mergeLane = 0;
	for (uint i = 0; i < laneNum; i++)
		if (isMerge(lanes[i])) {
			mergeLane = i;
			break;
		}

	int x1 = 0, x2 = 0;
	int lw = laneWidth();
	for (uint i = 0; i < laneNum && x1 < width; i++) {

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
}

void ListViewItem::paintCell(QPainter* p, const QColorGroup& cg,
                             int column, int width, int alignment) {
	QColorGroup _cg(cg);
	const Rev& c = *git->revLookup(_sha, fh);

	// lazy setup, only once when visible
	if (!populated) {
		populated = true;
		setupData(c);
	}
	if (column == GRAPH_COL) {
		paintGraph(c, p, _cg, width);
		return;
	}

	// adjust for annotation id column presence
	int mycolumn = (!git->isMainHistory(fh) ? column : column + 1);

	// alternate background color
	if (isInfoCol(mycolumn))
		_cg.setColor(QPalette::Window, isEvenLine ? EVEN_LINE_COL : ODD_LINE_COL);

	// tags, heads, refs and working dir colouring
	if (mycolumn == LOG_COL) {

		paintTagMarks(column);

		if (isHighlighted) {
			QFont f(p->font());
			f.setBold(true);
			p->save();
			p->setFont(f);
		}
		if (c.isDiffCache) {
			if (changedFiles(ZERO_SHA))
				_cg.setColor(QPalette::Window, ORANGE);
			else
				_cg.setColor(QPalette::Window, DARK_ORANGE);
		}
	}
	// diff target colouring
	if (isDiffTarget && isInfoCol(mycolumn))
		_cg.setColor(QPalette::Window, LIGHT_BLUE);

	Q3ListViewItem::paintCell(p, _cg, column, width, alignment);

	if (isHighlighted && mycolumn == LOG_COL)
		p->restore();
}

void ListViewItem::paintTagMarks(int col) {

	uint rt = git->checkRef(_sha);

	if (!pixmap(col) && rt == 0)
		return; // common case

	QPixmap* newPm = new QPixmap();

	if (rt & Git::BRANCH)
		addBranchPixmap(&newPm);

	if (rt & Git::TAG)
		addRefPixmap(&newPm, git->getRefName(_sha, Git::TAG), Qt::yellow);

	if (rt & Git::REF)
		addRefPixmap(&newPm, git->getRefName(_sha, Git::REF), PURPLE);

	if (!pixmap(col) || (newPm->rect() != pixmap(col)->rect()))
		setPixmap(col, *newPm);

	delete newPm;
}

void ListViewItem::addBranchPixmap(QPixmap** pp) {

	QString curBranch;
	SCList refs = git->getRefName(_sha, Git::BRANCH, &curBranch);
	FOREACH_SL (it, refs) {
		bool isCur = (curBranch == *it);
		QColor color(isCur ? Qt::green : DARK_GREEN);
		addTextPixmap(pp, *it, color, isCur);
	}
}

void ListViewItem::addRefPixmap(QPixmap** pp, SCList refs, const QColor& color) {

	FOREACH_SL (it, refs)
		addTextPixmap(pp, *it, color, false);
}

void ListViewItem::addTextPixmap(QPixmap** pp, SCRef text, const QColor& color, bool bold) {

	QFont fnt(myListView()->font());
	if (bold)
		fnt.setBold(true);

	QFontMetrics fm(fnt);
	QPixmap* pm = *pp;
	int ofs = pm->isNull() ? 0 : pm->width() + 2;
	int spacing = 2;
	int pw = fm.boundingRect(text).width() + 2 * (spacing + int(bold));
	int ph = fm.height() + 1;

	QPixmap* newPm = new QPixmap(ofs + pw, ph);

	QPainter p;
	p.begin(newPm);
	if (!pm->isNull()) {
		newPm->fill(isEvenLine ? EVEN_LINE_COL : ODD_LINE_COL);
		p.drawPixmap(0, 0, *pm);
	}
	p.setPen(Qt::black);
	p.setBrush(color);
	p.setFont(fnt);
	p.drawRect(ofs, 0, pw, ph);
	p.drawText(ofs + spacing, fm.ascent(), text);
	p.end();

	delete pm;
	*pp = newPm;
}

bool ListViewItem::changedFiles(SCRef c) {

	const RevFile* f = git->getFiles(c);
	if (f)
		for (int i = 0; i < f->names.count(); i++)
			if (!f->statusCmp(i, UNKNOWN))
				return true;
	return false;
}

void ListViewItem::setupData(const Rev& c) {

	// calculate lanes
	if (c.lanes.count() == 0)
		git->setLane(_sha, fh);

	// set time/date column
	int adj = !git->isMainHistory(fh) ? 0 : -1;
	if (_sha != ZERO_SHA) {
		if (secs != 0) { // secs is 0 for absolute date
			secs -= c.authorDate().toULong();
			setText(TIME_COL + adj, timeDiff(secs));
		} else
			setText(TIME_COL + adj, git->getLocalDate(c.authorDate()));
	}
	setText(LOG_COL + adj, c.shortLog());
	setText(AUTH_COL + adj, c.author());
}

const QString ListViewItem::timeDiff(unsigned long secs) const {

	uint days  =  secs / (3600 * 24);
	uint hours = (secs - days * 3600 * 24) / 3600;
	uint min   = (secs - days * 3600 * 24 - hours * 3600) / 60;
	uint sec   =  secs - days * 3600 * 24 - hours * 3600 - min * 60;
	QString tmp;
	if (days > 0)
		tmp.append(QString::number(days) + "d ");

	if (hours > 0 || !tmp.isEmpty())
		tmp.append(QString::number(hours) + "h ");

	if (min > 0 || !tmp.isEmpty())
		tmp.append(QString::number(min) + "m ");

	tmp.append(QString::number(sec) + "s");
	return tmp;
}
