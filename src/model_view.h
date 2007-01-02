/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef MODEL_VIEW_H
#define MODEL_VIEW_H

#include <QAbstractItemModel>
#include "ui_model_view.h"
#include "git.h"

class MVCModel;
class Rev;

class MVC : public QMainWindow, public Ui_MainWindowsModelView {
Q_OBJECT
public:
	MVC(Git* git, FileHistory* fh, QObject* parent);
	~MVC();
	void populate();

private:
	Git* git;
	MVCModel* m;
	FileHistory* fh;
	QObject* par;
};

class MVCModel : public QAbstractItemModel {
Q_OBJECT
public:
	MVCModel(Git* git, FileHistory* fh, QObject *parent = 0);
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


#endif
