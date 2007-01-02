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
	~MVC();
	void populate();

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
	QVariant headerData(int section, Qt::Orientation orientation,
				int role = Qt::DisplayRole) const;
	QModelIndex index(int row, int column,
			const QModelIndex &parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex &index) const;
	int rowCount(const QModelIndex &parent = QModelIndex()) const;
	int columnCount(const QModelIndex &parent = QModelIndex()) const;

private:
	void setupModelData(const QStringList &lines, MVCModel *parent);

	Git* git;
	FileHistory* fh;

	QList<QVariant> headerInfo;
	mutable int lastRow;
	mutable const Rev* lastRev;
};

class MVCDelegate : public QItemDelegate {
Q_OBJECT

public:
	MVCDelegate(Git* git, FileHistory* fh, QObject *parent);

	virtual void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex &i) const;
	virtual QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &i) const;
	void setCellHeight(int h);

private:
	void paintGraphLane(QPainter* p, int type, int x1, int x2,
                            const QColor& col, const QBrush& back) const;

	Git* git;
	FileHistory* fh;
	int _cellWidth;
	int _cellHeight;
};

#endif
