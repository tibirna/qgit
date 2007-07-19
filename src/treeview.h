/*
	Description: files tree view

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <QTreeWidget>
#include "common.h"
#include "git.h"

class DirItem;
class TreeView;
class Git;
class StateInfo;
class Domain;

class FileItem : public QTreeWidgetItem {
public:
	FileItem(FileItem* p, SCRef nm) : QTreeWidgetItem(p, QStringList(nm)) {}
	FileItem(QTreeWidget* p, SCRef nm) : QTreeWidgetItem(p, QStringList(nm)) {}

	virtual QString fullName() const;
	void setBold(bool b);
};

class DirItem : public FileItem {
public:
	DirItem(QTreeWidget* parent, SCRef ts, SCRef nm);
	DirItem(DirItem* parent, SCRef ts, SCRef nm);

protected:
	friend class TreeView;

	QString treeSha;
};

class TreeView : public QTreeWidget {
Q_OBJECT
public:
	TreeView(QWidget* par) : QTreeWidget(par), d(NULL), git(NULL), treeIsValid(false) {}
	void setup(Domain* d, Git* g);
	void setTreeName(SCRef treeName) { rootName = treeName; }
	void updateTree();
	const QString fullName(QTreeWidgetItem* item);
	bool isDir(SCRef fileName);
	bool isModified(SCRef path, bool isDir = false);
	void clear();
	void getTreeSelectedItems(QStringList& selectedItems);
	bool getTree(SCRef tSha, Git::TreeInfo& ti, bool wd, SCRef tPath);

	const QPixmap* folderClosed;
	const QPixmap* folderOpen;
	const QPixmap* fileDefault;

signals:
	void updateViews(const QString& newRevSha, const QString& newFileName);
	void contextMenu(const QString&, int type);

protected slots:
	void on_customContextMenuRequested(const QPoint&);
	void on_currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*);
	void on_itemExpanded(QTreeWidgetItem*);
	void on_itemCollapsed(QTreeWidgetItem*);

private:
	void setTree(SCRef treeSha);
	void setFile(SCRef fileName);
	void restoreStuff();

	Domain* d;
	Git* git;
	StateInfo* st;
	QString rootName;
	QStringList modifiedFiles; // no need a map, should not be a lot
	QStringList modifiedDirs;
	bool ignoreCurrentChanged;
	bool treeIsValid;
	bool isWorkingDir;
};

#endif
