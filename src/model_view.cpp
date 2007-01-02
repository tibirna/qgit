/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#include <QTime>
#include "model_view.h"

MVC::MVC(Git* g, FileHistory* f, QObject* p) : git(g), m(0), fh(f), par(p) {

	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);
}

MVC::~MVC() {

	delete m;
}

void MVC::populate() {

	dbStart;
	m = new MVCModel(git, fh, par);
	treeViewRevs->setModel(m);
	dbRestart;
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
