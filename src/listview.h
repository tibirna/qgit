/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef LISTVIEW_H
#define LISTVIEW_H

#include <QTreeView>
#include <QItemDelegate>
#include <QSortFilterProxyModel>
#include <QRegExp>
#include "common.h"

class Git;
class StateInfo;
class Domain;
class FileHistory;
class ListViewProxy;

class ListView: public QTreeView {
Q_OBJECT
	struct DropInfo;
public:
	ListView(QWidget* parent);
	~ListView();
	void setup(Domain* d, Git* g);
	const QString shaFromAnnId(int id);
	void showIdValues();
	void scrollToCurrent(ScrollHint hint = EnsureVisible);
	void scrollToNextHighlighted(int direction);
	void scrollToNext(int direction);
	void getSelectedItems(QStringList& selectedItems);
	bool update();
	void addNewRevs(const QVector<QString>& shaVec);
	const QString currentText(int col);
	int filterRows(bool, bool, SCRef = QString(), int = -1, ShaSet* = NULL);
	const QString sha(int row) const;
	int row(SCRef sha) const;
	QString refNameAt(const QPoint &pos);
	const QString& selectedRefName() const {return lastRefName;}
	void markDiffToSha(SCRef sha);

signals:
	void lanesContextMenuRequested(const QStringList&, const QStringList&);
	void applyRevisions(const QStringList& shas, const QString& remoteRepo);
	void applyPatches(const QStringList &files);
	void rebase(const QString& from, const QString& to, const QString& onto);
	void merge(const QStringList& shas, const QString& into);
	void moveRef(const QString& refName, const QString& toSHA);
	void contextMenu(const QString&, int);
	void diffTargetChanged(int); // used by new model_view integration
	void showStatusMessage(const QString&, int timeout=0);

public slots:
	void on_changeFont(const QFont& f);
	void on_keyUp();
	void on_keyDown();

protected:
	virtual void mousePressEvent(QMouseEvent* e) override;
	virtual void mouseMoveEvent(QMouseEvent* e) override;
	virtual void mouseReleaseEvent(QMouseEvent* e) override;
	virtual void dragEnterEvent(QDragEnterEvent* e) override;
	virtual void dragMoveEvent(QDragMoveEvent* e) override;
	virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
	virtual void dropEvent(QDropEvent* e) override;
	void startDragging(QMouseEvent *e);
	QPixmap pixmapFromSelection(const QStringList &revs, const QString &ref) const;

private slots:
	void on_customContextMenuRequested(const QPoint&);
	virtual void currentChanged(const QModelIndex&, const QModelIndex&) override;

private:
	void setupGeometry();
	bool filterRightButtonPressed(QMouseEvent* e);
        bool getLaneParentsChildren(SCRef sha, int x, SList p, SList c);
	int getLaneType(SCRef sha, int pos) const;

	Domain* d;
	Git* git;
	StateInfo* st;
	FileHistory* fh;
	ListViewProxy* lp;
	unsigned long secs;
	bool filterNextContextMenuRequest;
	QString lastRefName; // last ref name clicked on
	DropInfo *dropInfo; // struct describing to-be-dropped content
};

class ListViewDelegate : public QItemDelegate {
	friend class ListView;
Q_OBJECT
public:
	ListViewDelegate(Git* git, ListViewProxy* lp, QObject* parent);

	virtual void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const override;
	virtual QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex &i) const override;
	int laneWidth() const { return 3 * laneHeight / 4; }
	void setLaneHeight(int h) { laneHeight = h; }

signals:
	void updateView();

public slots:
	void diffTargetChanged(int);

private:
	const Rev* revLookup(int row, FileHistory** fhPtr = NULL) const;
	void paintLog(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraph(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraphLane(QPainter* p, int type, int x1, int x2, const QColor& col, const QColor& activeCol, const QBrush& back) const;
	QPixmap* getTagMarks(SCRef sha, const QStyleOptionViewItem& opt) const;
	void addTextPixmap(QPixmap** pp, SCRef txt, const QStyleOptionViewItem& opt) const;
	bool changedFiles(SCRef sha) const;
	qreal dpr(void) const;

	Git* git;
	ListViewProxy* lp;
	int laneHeight;
	int diffTargetRow;
};

class ListViewProxy : public QSortFilterProxyModel {
Q_OBJECT
public:
	ListViewProxy(QObject* parent, Domain* d, Git* g);
	int setFilter(bool isOn, bool highlight, SCRef filter, int colNum, ShaSet* s);
	bool isHighlighted(int row) const;

protected:
	virtual bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	bool isMatch(int row) const;
	bool isMatch(SCRef sha) const;

	Domain* d;
	Git* git;
	bool isHighLight;
	QRegExp filter;
	int colNum;
	ShaSet shaSet;
};

#endif
