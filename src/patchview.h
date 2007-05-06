/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef PATCHVIEW_H
#define PATCHVIEW_H

#include <QPointer>
#include <QRegExp>
#include "ui_patchview.h"
#include "domain.h"

class DiffHighlighter;
class Git;
class MyProcess;

class PatchView :public Domain {
Q_OBJECT
public:
	PatchView() {}
	PatchView(MainImpl* mi, Git* g);
	~PatchView();
	void clear(bool complete = true);
	Ui_TabPatch* tab() { return patchTab; }

signals:
	void diffTo(const QString&);
	void diffViewerDocked();

public slots:

	void lineEditDiff_returnPressed();
	void button_clicked(int);
	void buttonFilterPatch_clicked();
	void procReadyRead(const QByteArray& data);
	void procFinished();
	void on_highlightPatch(const QString&, bool);
	void on_updateRevDesc();

protected slots:
	virtual void on_contextMenu(const QString&, int);

protected:
	virtual bool doUpdate(bool force);

private:
	friend class DiffHighlighter;

	void updatePatch();
	void scrollCursorToTop();
	void scrollLineToTop(int lineNum);
	int positionToLineNum(int pos);
	int topToLineNum();
	void centerOnFileHeader(const QString& fileName);
	void centerTarget();
	void saveRestoreSizes(bool startup = false);
	int doSearch(const QString& txt, int pos);
	bool computeMatches();
	bool getMatch(int para, int* indexFrom, int* indexTo);
	void centerMatch(int id = 0);
	void processData(const QByteArray& data, int* prevLineNum = NULL);

	Ui_TabPatch* patchTab;
	DiffHighlighter* diffHighlighter;
	QPointer<MyProcess> proc;
	QByteArray patchRowData;
	QString halfLine;
	QString target;
	QString normalizedSha;
	bool seekTarget;
	bool diffLoaded;
	bool isRegExp;
	QRegExp pickAxeRE;

	enum ButtonId {
		DIFF_TO_PARENT = 0,
		DIFF_TO_HEAD   = 1,
		DIFF_TO_SHA    = 2
	};

	enum PatchFilter {
		VIEW_ALL,
		VIEW_ADDED,
		VIEW_REMOVED
	};
	PatchFilter curFilter, prevFilter;

	struct MatchSelection {
		int paraFrom;
		int indexFrom;
		int paraTo;
		int indexTo;
	};
	typedef QVector<MatchSelection> Matches;
	Matches matches;
};

#endif
