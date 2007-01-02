/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef MODEL_VIEW_H
#define MODEL_VIEW_H

#include <QAbstractItemModel>
#include <QItemDelegate>
#include "ui_model_view.h"
#include "git.h"

class MVCModel;
class MVCDelegate;
class Rev;

class MVC : public QMainWindow, public Ui_MainWindowsModelView {
Q_OBJECT
public:
	MVC(Git* git, FileHistory* fh, QWidget* parent);

private:
	Git* git;
	MVCModel* m;
	MVCDelegate* d;
	FileHistory* fh;
};

class MVCModel : public QAbstractItemModel {
Q_OBJECT
public:
	MVCModel(Git* git, FileHistory* fh, QObject *parent);
	~MVCModel();

	QVariant data(const QModelIndex &index, int role) const;
	Qt::ItemFlags flags(const QModelIndex &index) const;
	QVariant headerData(int s, Qt::Orientation o, int role = Qt::DisplayRole) const;
	QModelIndex index(int r, int c, const QModelIndex& p = QModelIndex()) const;
	QModelIndex parent(const QModelIndex& index) const;
	int rowCount(const QModelIndex&) const { return _rowCnt; }
	int columnCount(const QModelIndex&) const { return 5; }

private slots:
	void dataCleared();
	void on_newRevsAdded(const FileHistory*, const QVector<QString>&);

private:
	const QString timeDiff(unsigned long secs) const;

	Git* git;
	FileHistory* fh;

	QList<QVariant> _headerInfo;
	int _rowCnt;
	unsigned long _secs;
	mutable int _lastRow;
	mutable const Rev* _lastRev;
};

class MVCDelegate : public QItemDelegate {
Q_OBJECT

public:
	MVCDelegate(Git* git, FileHistory* fh, QObject *parent);

	virtual void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	virtual QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &i) const;
	void setCellHeight(int h);

private:
	void paintGraph(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	void paintGraphLane(QPainter* p, int type, int x1, int x2,
                            const QColor& col, const QBrush& back) const;

        void paintTagMarks(int col, SCRef sha) const;
	void addBranchPixmap(QPixmap** pp, SCRef sha) const;
	void addRefPixmap(QPixmap** pp, SCList refs, const QColor& color) const;
	void addTextPixmap(QPixmap** pp, SCRef text, const QColor& color, bool bold) const;
	bool changedFiles(SCRef c) const;

	Git* git;
	FileHistory* fh;
	int _cellWidth;
	int _cellHeight;
};

#endif
