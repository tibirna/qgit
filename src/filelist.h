/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef FILELIST_H
#define FILELIST_H

#include <QListWidget>
#include "common.h"

class Domain;
class StateInfo;
class Git;

class FileList: public QListWidget {
Q_OBJECT
public:
	FileList(QWidget* parent);
	void setup(Domain* dm, Git* g);
	void update(const RevFile* files, bool newFiles);
	void addItem(const QString& label, const QColor& clr);
	QString currentText();

signals:
	void contextMenu(const QString&, int);

protected:
	virtual void focusInEvent(QFocusEvent*);
	virtual void mousePressEvent(QMouseEvent*);
	virtual void mouseMoveEvent(QMouseEvent*);
	virtual void mouseReleaseEvent(QMouseEvent*);

private slots:
	void on_currentItemChanged(QListWidgetItem*, QListWidgetItem*);
	void on_customContextMenuRequested(const QPoint&);

private:
	void insertFiles(const RevFile* files);

	Domain* d;
	Git* git;
	StateInfo* st;
	QString dragFileName;
};

#endif
