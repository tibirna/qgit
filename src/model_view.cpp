/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#include <QTime>
#include <QPainter>
#include "common.h"
#include "model_view.h"

using namespace QGit;

MVC::MVC(Git* g, FileHistory* f, QWidget* p) : QMainWindow(p), git(g), m(0), d(0), fh(f) {

	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);
}

MVC::~MVC() {}

void MVC::populate() {

	m = new MVCModel(git, fh, this);
	treeViewRevs->setModel(m);

	d = new MVCDelegate(git, fh, this);
	d->setCellHeight(treeViewRevs->fontMetrics().height());
// 	treeViewRevs->setItemDelegateForColumn(0, d);
	treeViewRevs->setItemDelegate(d);
}

// ***********************************************************************

MVCModel::MVCModel(Git* g, FileHistory* f, QObject* p) : QAbstractItemModel(p), git(g), fh(f) {

	lastRev = NULL;
	lastRow = -1;
	headerInfo << "Graph" << "Ann id" << "Short Log"
     		   << "Author" << "Author Date";
}

MVCModel::~MVCModel() {}

Qt::ItemFlags MVCModel::flags(const QModelIndex& index ) const {

	if (!index.isValid())
		return Qt::ItemIsEnabled;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable; // read only
}

QVariant MVCModel::headerData(int section, Qt::Orientation orientation, int role) const {

	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
		return headerInfo[section];

	return QVariant();
}

QModelIndex MVCModel::index(int row, int column, const QModelIndex&) const {

	if (!git || !fh)
		return QModelIndex();

	if (row > fh->revOrder.count() || row < 0)
		return QModelIndex();

	const Rev* r;
	if (row == lastRow)
		r = lastRev;
	else {
		lastRow = row;
		lastRev = git->revLookup(fh->revOrder.at(row));
		r = lastRev;
	}
	return (r ? createIndex(row, column, (void*)r) : QModelIndex());
}

QModelIndex MVCModel::parent(const QModelIndex&) const {

	return QModelIndex();
}

int MVCModel::rowCount(const QModelIndex&) const {

	return fh->revOrder.count();
}

int MVCModel::columnCount(const QModelIndex&) const {

	return 5;
}

QVariant MVCModel::data(const QModelIndex& index, int role) const {

	if (!index.isValid())
		return QVariant();

	if (role != Qt::DisplayRole)
		return QVariant();

	const Rev* r = static_cast<const Rev*>(index.internalPointer());
	int col = index.column();

	// calculate lanes
	if (r->lanes.count() == 0)
		git->setLane(r->sha(), fh);

	if (col == QGit::LOG_COL)
		return r->shortLog();

	if (col == QGit::AUTH_COL)
		return r->author();

	if (col == QGit::TIME_COL && r->sha() != QGit::ZERO_SHA)
		return git->getLocalDate(r->authorDate());

	return QVariant();
}

// void ListViewItem::setupData(const Rev& c) {
//
// 	// calculate lanes
// 	if (c.lanes.count() == 0)
// 		git->setLane(_sha, fh);
//
// 	// set time/date column
// 	int adj = !git->isMainHistory(fh) ? 0 : -1;
// 	if (_sha != ZERO_SHA) {
// 		if (secs != 0) { // secs is 0 for absolute date
// 			secs -= c.authorDate().toULong();
// 			setText(TIME_COL + adj, timeDiff(secs));
// 		} else
// 			setText(TIME_COL + adj, git->getLocalDate(c.authorDate()));
// 	}
// 	setText(LOG_COL + adj, c.shortLog());
// 	setText(AUTH_COL + adj, c.author());
// }
//
// const QString ListViewItem::timeDiff(unsigned long secs) const {
//
// 	uint days  =  secs / (3600 * 24);
// 	uint hours = (secs - days * 3600 * 24) / 3600;
// 	uint min   = (secs - days * 3600 * 24 - hours * 3600) / 60;
// 	uint sec   =  secs - days * 3600 * 24 - hours * 3600 - min * 60;
// 	QString tmp;
// 	if (days > 0)
// 		tmp.append(QString::number(days) + "d ");
//
// 	if (hours > 0 || !tmp.isEmpty())
// 		tmp.append(QString::number(hours) + "h ");
//
// 	if (min > 0 || !tmp.isEmpty())
// 		tmp.append(QString::number(min) + "m ");
//
// 	tmp.append(QString::number(sec) + "s");
// 	return tmp;
// }
//


MVCDelegate::MVCDelegate(Git* g, FileHistory* f, QObject* p) : QItemDelegate(p) {

	git = g;
	fh = f;
	_cellHeight = _cellWidth = 0;
}

void MVCDelegate::setCellHeight(int h) {

	_cellHeight = h;
	_cellWidth = 3 * _cellHeight / 4;
}

QSize MVCDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {

	return QSize(_cellWidth, _cellHeight);
}

void MVCDelegate::paintGraphLane(QPainter* p, int type, int x1, int x2,
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

void MVCDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	static const QColor colors[COLORS_NUM] = { Qt::black, Qt::red, DARK_GREEN,
	                                           Qt::blue,  Qt::darkGray, BROWN,
	                                           Qt::magenta, ORANGE };

	if (i.column() != 0)
		return QItemDelegate::paint(p, opt, i);

// 	dbg(i.row());dbg(p->viewport().top());dbg(p->hasClipping());
	if (opt.state & QStyle::State_Selected)
		p->fillRect(opt.rect, opt.palette.highlight());
	else
		p->fillRect(opt.rect, opt.palette.base());

	const Rev* r = static_cast<const Rev*>(i.internalPointer());
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
