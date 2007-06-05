/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef SMARTBROWSE_H
#define SMARTBROWSE_H

#include <QLabel>
#include <QTime>
#include "revsview.h"

class SmartLabel : public QLabel {
Q_OBJECT
public:
	SmartLabel(const QString& text, QWidget* par) : QLabel(text, par) {}

protected:
	virtual void contextMenuEvent(QContextMenuEvent* e);

private slots:
	void switchLinks();
};

class SmartBrowse : public QObject {
Q_OBJECT
public:
	SmartBrowse(RevsView* par);

protected:
	bool eventFilter(QObject *obj, QEvent *event);

public slots:
	void linkActivated(const QString&);
	void flagChanged(uint);

private:
	QTextEdit* curTextEdit(bool* isDiff = NULL);
	void setVisible(bool b);
	int updateVisibility(int delta = 0);
	void updatePosition();
	bool wheelRolled(int delta, bool outOfRange);

	RevsView* rv;
	SmartLabel* logTopLbl;
	SmartLabel* logBottomLbl;
	SmartLabel* diffTopLbl;
	SmartLabel* diffBottomLbl;
	QTime scrollTimer, switchTimer, timeoutTimer;
	int wheelCnt;
	bool lablesEnabled;
};

#endif
