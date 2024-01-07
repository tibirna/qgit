/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef SMARTBROWSE_H
#define SMARTBROWSE_H

#include <QElapsedTimer>
#include <QLabel>
#include "revsview.h"

class SmartLabel : public QLabel {
Q_OBJECT
public:
	SmartLabel(const QString& text, QWidget* par);
	void paintEvent(QPaintEvent* event) override;

protected:
	virtual void contextMenuEvent(QContextMenuEvent* e) override;

private slots:
	void switchLinks();
};

class SmartBrowse : public QObject {
Q_OBJECT
public:
	SmartBrowse(RevsView* par);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
	void updateVisibility();
	void linkActivated(const QString&);
	void flagChanged(uint);

private:
	QTextEdit* curTextEdit(bool* isDiff = NULL);
	void setVisible(bool b);
	void updatePosition();
	int visibilityFlags(bool* isDiff = NULL);
	bool wheelRolled(int delta, int flags);

	RevsView* rv;
	SmartLabel* logTopLbl;
	SmartLabel* logBottomLbl;
	SmartLabel* diffTopLbl;
	SmartLabel* diffBottomLbl;
	QElapsedTimer scrollTimer, switchTimer, timeoutTimer;
	int wheelCnt;
	bool lablesEnabled;
};

#endif
