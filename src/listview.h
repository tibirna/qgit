/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef LISTVIEW_H
#define LISTVIEW_H

#include <QTreeView>
#include <QItemDelegate>
#include <QSet>
#include "common.h"

class Git;
class StateInfo;
class Domain;
class FileHistory;

class ListView: public QTreeView {
Q_OBJECT
public:
	typedef const QMap<QString, bool> ShaMap;

	ListView(QWidget* parent);
	~ListView();
	void setup(Domain* d, Git* g);
	const QString getSha(uint id);
	void showIdValues();
	void getSelectedItems(QStringList& selectedItems);
	bool update();
	void addNewRevs(const QVector<QString>& shaVec);
	const QString currentText(int col);
	int filterRows(bool, bool, SCRef = QString(), int = -1, ShaMap& = ShaMap());

signals:
	void lanesContextMenuRequested(const QStringList&, const QStringList&);
	void droppedRevisions(const QStringList&);
	void contextMenu(const QString&, int);
	void diffTargetChanged(int); // used by new model_view integration
	void matchedRowsChanged(const QSet<int>&);

public slots:
	void on_repaintListViews(const QFont& f);

protected:
	virtual void mousePressEvent(QMouseEvent* e);
	virtual void mouseMoveEvent(QMouseEvent* e);
	virtual void mouseReleaseEvent(QMouseEvent* e);
	virtual void dragEnterEvent(QDragEnterEvent* e);
	virtual void dragMoveEvent(QDragMoveEvent* e);
	virtual void dropEvent(QDropEvent* e);

private slots:
	void on_customContextMenuRequested(const QPoint&);
	void on_currentChanged(const QModelIndex&, const QModelIndex&);

private:
	void setupGeometry();
	bool filterRightButtonPressed(QMouseEvent* e);
	bool getLaneParentsChilds(SCRef sha, int x, SList p, SList c);
	int getLaneType(SCRef sha, int pos) const;
	bool isMatch(SCRef sha, SCRef filter, int colNum, ShaMap& shaMap);

	Domain* d;
	Git* git;
	StateInfo* st;
	FileHistory* fh;
	unsigned long secs;
	bool filterNextContextMenuRequest;
};

class ListViewDelegate : public QItemDelegate {
Q_OBJECT
public:
	ListViewDelegate(Git* git, FileHistory* fh, QObject *parent);

	virtual void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	virtual QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex &i) const;
	int laneWidth() const { return 3 * _laneHeight / 4; }
	void setLaneHeight(int h) { _laneHeight = h; }

signals:
	void updateView();

public slots:
	void diffTargetChanged(int);
	void matchedRowsChanged(const QSet<int>&);

private:
	void paintLog(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraph(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraphLane(QPainter* p, int type, int x1, int x2, const QColor& col, const QBrush& back) const;
	QPixmap* getTagMarks(SCRef sha, const QStyleOptionViewItem& opt) const;
	void addRefPixmap(QPixmap** pp, SCRef sha, int type, QStyleOptionViewItem opt) const;
	void addTextPixmap(QPixmap** pp, SCRef txt, const QStyleOptionViewItem& opt) const;
	bool changedFiles(SCRef sha) const;

	Git* git;
	FileHistory* fh;
	int _laneHeight;
	int _diffTargetRow;
	QSet<int> _matchedRows;
};

#endif
