/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef LISTVIEW_H
#define LISTVIEW_H

#include <QTreeView>
#include <QItemDelegate>
#include <QSet>
#include <q3listview.h>
//Added by qt3to4:
#include <QDropEvent>
#include <QPixmap>
#include <QMouseEvent>
#include <QEvent>
#include "common.h"

class Git;
class StateInfo;
class Domain;
class FileHistory;

class ListViewItem;

class ListView: public QObject {
Q_OBJECT
public:
	ListView(Domain* d, Git* g, QTreeView* l, FileHistory* f, const QFont& fnt);
	~ListView();
	void clear();
	const QString getSha(int id);
	void updateIdValues();
	void getSelectedItems(QStringList& selectedItems);
	bool update();
	void addNewRevs(const QVector<QString>& shaVec);
	QString currentText(int col);

	bool filterNextContextMenuRequest;


signals:
	void lanesContextMenuRequested(const QStringList&, const QStringList&);
	void droppedRevisions(const QStringList&);
	void contextMenu(const QString&, int);
	void diffTargetChanged(int); // used by new model_view integration

public slots:
	void on_repaintListViews(const QFont& f);

protected:
	virtual bool eventFilter(QObject* obj, QEvent* ev);

private slots:
	void on_customContextMenuRequested(const QPoint&);
	void on_currentChanged(const QModelIndex&, const QModelIndex&);

	void on_mouseButtonPressed(int, Q3ListViewItem*, const QPoint&, int);
	void on_clicked(Q3ListViewItem*);
	void on_onItem(Q3ListViewItem*);

private:
	void setupListView(const QFont& fnt);
	bool filterRightButtonPressed(QMouseEvent* e);
	bool filterDropEvent(QDropEvent* e);
	bool getLaneParentsChilds(ListViewItem* item, int x, SList p, SList c);
	const QString getRefs(Q3ListViewItem* item);
	int laneWidth() const { return 3 * lv->fontMetrics().height() / 4; }
	int getLaneType(int pos) const;

	Domain* d;
	Git* git;
	QTreeView* lv;
	StateInfo* st;
	FileHistory* fh;
	unsigned long secs;
};

class ListViewDelegate : public QItemDelegate {
Q_OBJECT

public:
	ListViewDelegate(Git* git, FileHistory* fh, QObject *parent);

	virtual void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	virtual QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &i) const;
	void setCellHeight(int h);

signals:
	void updateView();

public slots:
	void diffTargetChanged(int);
	void highlightedRowsChanged(const QSet<int>&);

private:
	void paintLog(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraph(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraphLane(QPainter* p, int type, int x1, int x2,
                            const QColor& col, const QBrush& back) const;

	QPixmap* getTagMarks(SCRef sha) const;
	void addBranchPixmap(QPixmap** pp, SCRef sha) const;
	void addRefPixmap(QPixmap** pp, SCList refs, const QColor& color) const;
	void addTextPixmap(QPixmap** pp, SCRef text, const QColor& color, bool bold) const;
	bool changedFiles(SCRef c) const;

	Git* git;
	FileHistory* fh;
	int _cellWidth;
	int _cellHeight;
	int _diffTargetRow;
	QSet<int> _hlRows;
};

#endif
