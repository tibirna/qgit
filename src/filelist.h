/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef FILELIST_H
#define FILELIST_H

#include <qobject.h>
#include <q3listbox.h>
#include "common.h"

class Domain;
class StateInfo;
class Git;

class ListBoxFileItem: public Q3ListBoxText {
public:
	ListBoxFileItem(Q3ListBox* lb, SCRef t, const QColor& c);
	virtual void paint(QPainter* p);

private:
	QColor myColor;
};

class ListBoxFiles: public QObject {
Q_OBJECT
public:
	ListBoxFiles(Domain* mi, Git* g, Q3ListBox* l);

	void clear();
	void update(const RevFile* files, bool newFiles);

signals:
	void contextMenu(const QString&, int);

private slots:
	void on_currentChanged(Q3ListBoxItem* item);
	void on_contextMenuRequested(Q3ListBoxItem* item);
	void on_mouseButtonPressed(int, Q3ListBoxItem*, const QPoint&);
	void on_clicked(Q3ListBoxItem*);
	void on_onItem(Q3ListBoxItem*);
	void on_onViewport();

private:
	void insertFiles(const RevFile* files);
	void mouseMoved();

	Domain* d;
	Git* git;
	Q3ListBox* lb;
	StateInfo* st;
	QString dragFileName;
};

#endif
