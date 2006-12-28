/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef LISTVIEW_H
#define LISTVIEW_H

#include <qobject.h>
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

class ListViewItem: public Q3ListViewItem {
public:
	ListViewItem(Q3ListView* p, ListViewItem* a, Git* g, SCRef sha,
	             bool e, unsigned long t, FileHistory* f);

	SCRef sha() const { return _sha; }
	int getLaneType(int pos) const;
	void setDiffTarget(bool b);
	virtual void paintCell(QPainter* p, const QColorGroup& cg, int c, int w, int a);
	void setEven(bool b) { isEvenLine = b; }
	void setHighlighted(bool b) { isHighlighted = b; }
	bool highlighted() const { return isHighlighted; }
	int laneWidth() const { return 3 * myListView()->fontMetrics().height() / 4; }

private:
	void setupData(const Rev& c);
	void paintGraphLane(QPainter* p, int t, int x1, int x2, const QColor& c, const QBrush& b);
	void paintGraph(const Rev& c, QPainter *p, const QColorGroup& cg, int width);
	void paintTagMarks(int col);
	void addBranchPixmap(QPixmap** pp);
	void addRefPixmap(QPixmap** pp, SCList refs, const QColor& color);
	void addTextPixmap(QPixmap** pp, SCRef text, const QColor& color, bool bold);
	const QString timeDiff(unsigned long secs) const;
	bool changedFiles(SCRef c);
	Q3ListView* myListView() const { return listView_; } // QListViewItem::listView() traverses the
	                                                    // items to the root to find the listview
	Q3ListView* listView_;
	Git* git;
	FileHistory* fh;
	const QString _sha;
	unsigned long secs;
	bool populated, isEvenLine, isHighlighted, isDiffTarget;
};

class ListView: public QObject {
Q_OBJECT
public:
	ListView(Domain* d, Git* g, Q3ListView* l, FileHistory* f, const QFont& fnt);
	~ListView();
	void clear();
	const QString getSha(int id);
	void updateIdValues();
	void getSelectedItems(QStringList& selectedItems);
	bool update();
	void addNewRevs(const QVector<QString>& shaVec);

	bool filterNextContextMenuRequest;

signals:
	void lanesContextMenuRequested(const QStringList&, const QStringList&);
	void droppedRevisions(const QStringList&);
	void contextMenu(const QString&, int);

public slots:
	void on_newRevsAdded(const FileHistory* fh, const QVector<QString>& shaVec);
	void on_repaintListViews(const QFont& f);

protected:
	virtual bool eventFilter(QObject* obj, QEvent* ev);

private slots:
	void on_contextMenuRequested(Q3ListViewItem*);
	void on_currentChanged(Q3ListViewItem* item);
	void on_mouseButtonPressed(int, Q3ListViewItem*, const QPoint&, int);
	void on_clicked(Q3ListViewItem*);
	void on_onItem(Q3ListViewItem*);

private:
	void setupListView(const QFont& fnt);
	bool filterRightButtonPressed(QMouseEvent* e);
	bool filterDropEvent(QDropEvent* e);
	bool getLaneParentsChilds(ListViewItem* item, int x, SList p, SList c);
	const QString getRefs(Q3ListViewItem* item);
	void setHighlight(SCRef diffToSha);
	ListViewItem* findItemSha(SCRef sha) const;

	Domain* d;
	Git* git;
	Q3ListView* lv;
	StateInfo* st;
	FileHistory* fh;
	ListViewItem* lastItem;   // QListView::lastItem() is slow
	ListViewItem* diffTarget; // cannot use QGuardedPtr, not QObject inherited
	unsigned long secs;
};

#endif
