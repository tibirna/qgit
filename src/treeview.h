/*
	Description: files tree view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <q3listview.h>
#include <qpixmap.h>
#include <q3dict.h>
#include "common.h"

class DirItem;
class TreeView;
class Git;
class StateInfo;
class Domain;

class FileItem : public Q3ListViewItem {
public:
	FileItem(FileItem* p, SCRef nm) : Q3ListViewItem(p, nm), isModified(false) {}
	FileItem(Q3ListView* p, SCRef nm) : Q3ListViewItem(p, nm), isModified(false) {}

	virtual void setModified(bool b) { isModified = b; }
	virtual QString fullName() const;
	virtual void paintCell(QPainter* p, const QColorGroup& cg, int c, int w, int a);

protected:
	bool isModified;
};

class DirItem : public FileItem {
public:
	DirItem(Q3ListView* parent, SCRef ts, SCRef nm, TreeView* t);
	DirItem(DirItem* parent, SCRef ts, SCRef nm, TreeView* t);

	virtual void setOpen(bool b);
	virtual void setup();

protected:
	friend class TreeView;

	QString treeSha;
	TreeView* tv;
	bool isWorkingDir;
};

class TreeView : public QObject {
Q_OBJECT
public:
	TreeView(Domain* d, Git* g, Q3ListView* lv);
	void setTreeName(SCRef treeName) { rootName = treeName; }
	void update();
	const QString fullName(Q3ListViewItem* item);
	bool isDir(SCRef fileName);
	bool isModified(SCRef path, bool isDir = false);
	void clear();
	void getTreeSelectedItems(QStringList& selectedItems);
	bool getTree(SCRef tSha, SList nm, SList shas, SList types, bool wd, SCRef tPath);

	const QPixmap* folderClosed;
	const QPixmap* folderOpen;
	const QPixmap* fileDefault;

signals:
	void updateViews(const QString& newRevSha, const QString& newFileName);
	void contextMenu(const QString&, int type);

protected slots:
	void on_contextMenuRequested(Q3ListViewItem*,const QPoint&,int);
	void on_currentChanged(Q3ListViewItem*);

private:
	void setTree(SCRef treeSha);
	void setFile(SCRef fileName);
	void restoreStuff();

	Domain* d;
	Git* git;
	Q3ListView* listView;
	StateInfo* st;
	QString rootName;
	QStringList modifiedFiles; // no need a map, should not be a lot
	QStringList modifiedDirs;
	bool ignoreCurrentChanged;
	bool treeIsValid;
};

#endif
