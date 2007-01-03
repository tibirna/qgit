/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#include <QTime>
#include <QPainter>
#include <QHeaderView>
#include "common.h"
#include "model_view.h"

using namespace QGit;

MVC::MVC(Git* g, FileHistory* f, QWidget* p) : QMainWindow(p), git(g), m(0), d(0), fh(f) {

	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);

	QPalette pl = treeViewRevs->palette();
	pl.setColor(QPalette::Base, ODD_LINE_COL);
	pl.setColor(QPalette::AlternateBase, EVEN_LINE_COL);
	treeViewRevs->setPalette(pl); // does not seem to inherit application palette

	m = new MVCModel(git, fh, this);
	treeViewRevs->setModel(m);

	d = new MVCDelegate(git, fh, this);
	d->setCellHeight(treeViewRevs->fontMetrics().height());
	treeViewRevs->setItemDelegate(d);

	if (git->isMainHistory(fh))
		treeViewRevs->hideColumn(ANN_ID_COL);

	treeViewRevs->header()->setStretchLastSection(true);
	int w = treeViewRevs->columnWidth(LOG_COL);
	treeViewRevs->setColumnWidth(LOG_COL, w * 4);
	treeViewRevs->setColumnWidth(AUTH_COL, w * 2);
}

// ***********************************************************************

MVCModel::MVCModel(Git* g, FileHistory* f, QObject* p) : QAbstractItemModel(p), git(g), fh(f) {

	_headerInfo << "Graph" << "Ann id" << "Short Log"
	            << "Author" << "Author Date";

	_lastRev = NULL;
	_lastRow = -1;
	dataCleared(); // after _headerInfo is set

	connect(fh, SIGNAL(cleared()), this, SLOT(dataCleared()));
	connect(git, SIGNAL(newRevsAdded(const FileHistory*, const QVector<QString>&)),
	        this, SLOT(on_newRevsAdded(const FileHistory*, const QVector<QString>&)));
}

MVCModel::~MVCModel() {}

void MVCModel::dataCleared() {

	if (testFlag(REL_DATE_F)) {
		_secs = QDateTime::currentDateTime().toTime_t();
		_headerInfo[3] = "Last Change";
	} else {
		_secs = 0;
		_headerInfo[3] = "Author Date";
	}
	_rowCnt = fh->revOrder.count();
	reset();
}

void MVCModel::on_newRevsAdded(const FileHistory* f, const QVector<QString>& shaVec) {

	if (f != fh) // signal newRevsAdded() is broadcast
		return;

	beginInsertRows(QModelIndex(), _rowCnt, shaVec.count());
	_rowCnt = shaVec.count();
	endInsertRows();
}

Qt::ItemFlags MVCModel::flags(const QModelIndex& index) const {

	if (!index.isValid())
		return Qt::ItemIsEnabled;

	return Qt::ItemIsEnabled | Qt::ItemIsSelectable; // read only
}

QVariant MVCModel::headerData(int section, Qt::Orientation orientation, int role) const {

	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
		return _headerInfo.at(section);

	return QVariant();
}

QModelIndex MVCModel::index(int row, int column, const QModelIndex&) const {

	if (!git || !fh)
		return QModelIndex();

	if (row < 0 || row >= _rowCnt)
		return QModelIndex();

	const Rev* r;
	if (row == _lastRow)
		r = _lastRev;
	else {
		_lastRow = row;
		_lastRev = git->revLookup(fh->revOrder.at(row));
		r = _lastRev;
	}
	return (r ? createIndex(row, column, (void*)r) : QModelIndex());
}

QModelIndex MVCModel::parent(const QModelIndex&) const {

	return QModelIndex();
}

const QString MVCModel::timeDiff(unsigned long secs) const {

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

	if (col == QGit::TIME_COL && r->sha() != QGit::ZERO_SHA) {

		if (_secs != 0) // secs is 0 for absolute date
			return timeDiff(_secs - r->authorDate().toULong());
		else
			return git->getLocalDate(r->authorDate());
	}
	return QVariant();
}

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

void MVCDelegate::paintGraph(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	static const QColor colors[COLORS_NUM] = { Qt::black, Qt::red, DARK_GREEN, Qt::blue,
	                                           Qt::darkGray, BROWN, Qt::magenta, ORANGE };

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

void MVCDelegate::paintLog(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const {

	const Rev* r = static_cast<const Rev*>(i.internalPointer());
	if (!r)
		return;

	if (r->isDiffCache)
		p->fillRect(opt.rect, changedFiles(ZERO_SHA) ? ORANGE : DARK_ORANGE);

/*	if (isHighlighted) {
		QFont f(p->font());
		f.setBold(true);
		p->save();
		p->setFont(f);
	}
	if (isDiffTarget)
		p->fillRect(opt.rect, LIGHT_BLUE);
*/
	QPixmap* pm = getTagMarks(r->sha());
	if (pm) {
		p->drawPixmap(opt.rect.x(), opt.rect.y(), *pm);
		QStyleOptionViewItem o(opt);
		o.rect.adjust(pm->width(), 0, 0, 0);
		delete pm;
		QItemDelegate::paint(p, o, i);
	} else
		QItemDelegate::paint(p, opt, i);

// 	if (isHighlighted)
// 		p->restore();
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
