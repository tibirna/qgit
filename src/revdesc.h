/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#ifndef REVDESC_H
#define REVDESC_H

#include <q3textbrowser.h>
//Added by qt3to4:
#include <Q3PopupMenu>

class Q3PopupMenu;
class Domain;

/*
	this is placed by Qt Designer as a custom widget.
	we prefer QTextBrowser inheritance above
	composition to override createPopupMenu()
*/

class RevDesc: public Q3TextBrowser {
Q_OBJECT
public:
	RevDesc(QWidget* parent);
	void setDomain(Domain* dm) { d = dm; };

protected:
	virtual Q3PopupMenu* createPopupMenu(const QPoint& pos);

private slots:
	void on_linkClicked(const QString& link);
	void on_highlighted(const QString& link);
	void on_linkCopy();

private:
	Domain* d;
	QString highlightedLink;
};

#endif
