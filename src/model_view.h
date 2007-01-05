/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef MODEL_VIEW_H
#define MODEL_VIEW_H

#include <QItemDelegate>
#include <QSet>
#include "ui_model_view.h"
#include "common.h"

class MVCDelegate;
class FileHistory;
class Git;

class MVC : public QMainWindow, public Ui_MainWindowsModelView {
public:
	MVC(Git* git, FileHistory* fh, QWidget* parent);

//private:
	MVCDelegate* d;
};

class MVCDelegate : public QItemDelegate {
Q_OBJECT

public:
	MVCDelegate(Git* git, FileHistory* fh, QObject *parent);

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
