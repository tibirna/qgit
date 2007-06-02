/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef REVSVIEW_H
#define REVSVIEW_H

#include <QLabel>
#include <QPointer>
#include <QTime>
#include "ui_revsview.h" // needed by moc_* file to understand tab() function
#include "common.h"
#include "domain.h"

class MainImpl;
class Git;
class FileHistory;
class PatchView;

class RevsView : public Domain {
Q_OBJECT
public:
	RevsView(MainImpl* parent, Git* git, bool isMain = false);
	~RevsView();
	void clear(bool complete);
	void viewPatch(bool newTab);
	void setEnabled(bool b);
	Ui_TabRev* tab() { return revTab; }

public slots:
	void toggleDiffView();

private slots:
	void on_loadCompleted(const FileHistory*, const QString& stats);
	void on_lanesContextMenuRequested(const QStringList&, const QStringList&);
	void on_updateRevDesc();

protected:
	virtual bool doUpdate(bool force);

private:
	friend class MainImpl;

	void updateLineEditSHA(bool clear = false);

	Ui_TabRev* revTab;
	QPointer<PatchView> linkedPatchView;
};

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
	SmartBrowse(RevsView* par, RevDesc* msg, PatchContent* diff);

protected:
	bool eventFilter(QObject *obj, QEvent *event);

private slots:
	void linkActivated(const QString&);

private:
	QTextEdit* curTextEdit();
	void parentResized();
	bool wheelRolled(int delta, bool outOfRange);

	RevDesc* textBrowserDesc;
	PatchContent* textEditDiff;

	SmartLabel* logTopLbl;
	SmartLabel* logBottomLbl;
	SmartLabel* diffTopLbl;
	SmartLabel* diffBottomLbl;
	QTime filterTimer, switchTimer;
	int wheelCnt;
};

#endif
