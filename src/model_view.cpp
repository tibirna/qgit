/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#include <QTime>
#include <QPainter>
#include <QHeaderView>
#include "common.h"
#include "git.h"
#include "model_view.h"

using namespace QGit;

MVC::MVC(Git* git, FileHistory* fh, QWidget* par) : QMainWindow(par) {

	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);

	QPalette pl = treeViewRevs->palette();
	pl.setColor(QPalette::Base, ODD_LINE_COL);
	pl.setColor(QPalette::AlternateBase, EVEN_LINE_COL);
	treeViewRevs->setPalette(pl); // does not seem to inherit application palette

	treeViewRevs->setModel(fh);

	d = new MVCDelegate(git, fh, this);
	d->setCellHeight(treeViewRevs->fontMetrics().height());
	treeViewRevs->setItemDelegate(d);
	connect(d, SIGNAL(updateView()), treeViewRevs->viewport(), SLOT(update()));

	if (git->isMainHistory(fh))
		treeViewRevs->hideColumn(ANN_ID_COL);

	treeViewRevs->header()->setStretchLastSection(true);
	int w = treeViewRevs->columnWidth(LOG_COL);
	treeViewRevs->setColumnWidth(LOG_COL, w * 4);
	treeViewRevs->setColumnWidth(AUTH_COL, w * 2);
}

// ******************************************************************************

MVCDelegate::MVCDelegate(Git* g, FileHistory* f, QObject* p) : QItemDelegate(p) {

	git = g;
	fh = f;
	_cellHeight = _cellWidth = 0;
	_diffTargetRow = -1;
}

void MVCDelegate::setCellHeight(int h) {

	_cellHeight = h;
	_cellWidth = 3 * _cellHeight / 4;
}

QSize MVCDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {

	return QSize(_cellWidth, _cellHeight);
}

void MVCDelegate::diffTargetChanged(int row) {

	if (_diffTargetRow != row) {
		_diffTargetRow = row;
		emit updateView();
	}
}

void MVCDelegate::highlightedRowsChanged(const QSet<int>& rows) {

	_hlRows.clear();
	_hlRows.unite(rows);
	emit updateView();
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

void MVCDelegate::paintGraph(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	static const QColor colors[COLORS_NUM] = { Qt::black, Qt::red, DARK_GREEN, Qt::blue,
	                                           Qt::darkGray, BROWN, Qt::magenta, ORANGE };

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

void MVCDelegate::paintLog(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	int row = i.row();
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
		QItemDelegate::paint(p, opt, i);
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

	QItemDelegate::paint(p, newOpt, i);
}

void MVCDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	if (i.column() == GRAPH_COL)
		return paintGraph(p, opt, i);

	if (i.column() == LOG_COL)
		return paintLog(p, opt, i);

	return QItemDelegate::paint(p, opt, i);
}

bool MVCDelegate::changedFiles(SCRef c) const {

	const RevFile* f = git->getFiles(c);
	if (f)
		for (int i = 0; i < f->names.count(); i++)
			if (!f->statusCmp(i, UNKNOWN))
				return true;
	return false;
}

QPixmap* MVCDelegate::getTagMarks(SCRef sha) const {

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

void MVCDelegate::addBranchPixmap(QPixmap** pp, SCRef sha) const {

	QString curBranch;
	SCList refs = git->getRefName(sha, Git::BRANCH, &curBranch);
	FOREACH_SL (it, refs) {
		bool isCur = (curBranch == *it);
		QColor color(isCur ? Qt::green : DARK_GREEN);
		addTextPixmap(pp, *it, color, isCur);
	}
}

void MVCDelegate::addRefPixmap(QPixmap** pp, SCList refs, const QColor& color) const {

	FOREACH_SL (it, refs)
		addTextPixmap(pp, *it, color, false);
}

void MVCDelegate::addTextPixmap(QPixmap** pp, SCRef text, const QColor& color, bool bold) const {

	QFont fnt; //QFont fnt(myListView()->font()); FIXME
	if (bold)
		fnt.setBold(true);

	QFontMetrics fm(fnt);
	QPixmap* pm = *pp;
	int ofs = pm->isNull() ? 0 : pm->width() + 2;
	int spacing = 2;
	int pw = fm.boundingRect(text).width() + 2 * (spacing + int(bold));
	int ph = fm.height() - 1; // leave vertical space between two consecutive tags

	QPixmap* newPm = new QPixmap(ofs + pw, ph);

	QPainter p;
	p.begin(newPm);
	if (!pm->isNull()) {
		newPm->fill(QApplication::palette().base());
		p.drawPixmap(0, 0, *pm);
	}
	p.setPen(Qt::black);
	p.setBrush(color);
	p.setFont(fnt);
	p.drawRect(ofs, 0, pw - 1, ph - 1);
	p.drawText(ofs + spacing, fm.ascent(), text);
	p.end();

	delete pm;
	*pp = newPm;
}
