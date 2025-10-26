/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef PATCHCONTENT_H
#define PATCHCONTENT_H

#include <QPointer>
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <QRegExp>
#else
#include <QRegularExpression>
#endif
#include <QTextEdit>
#include <QSyntaxHighlighter>
#include "common.h"

class Domain;
class Git;
class MyProcess;
class StateInfo;

class DiffHighlighter : public QSyntaxHighlighter {
public:
	DiffHighlighter(QTextEdit* p) : QSyntaxHighlighter(p), cl(0) {}
	void setCombinedLength(uint c) { cl = c; }
	virtual void highlightBlock(const QString& text);
private:
	uint cl;
};

class PatchContent: public QTextEdit {
Q_OBJECT
public:
	PatchContent(QWidget* parent);
	void setup(Domain* parent, Git* git);
	void clear();
	void centerOnFileHeader(StateInfo& st);
	void refresh();
	void update(StateInfo& st);

	enum PatchFilter {
		VIEW_ALL,
		VIEW_ADDED,
		VIEW_REMOVED
	};
	PatchFilter curFilter, prevFilter;

public slots:
	void on_highlightPatch(const QString&, bool);
	void typeWriterFontChanged();
	void procReadyRead(const QByteArray& data);
	void procFinished();

private:
	friend class DiffHighlighter;

	void scrollCursorToTop();
	void scrollLineToTop(int lineNum);
	int positionToLineNum(int pos);
	int topToLineNum();
	void saveRestoreSizes(bool startup = false);
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	int doSearch(const QString& txt, int pos);
#endif
	bool computeMatches();
	bool getMatch(int para, int* indexFrom, int* indexTo);
	void centerMatch(int id = 0);
	bool centerTarget(SCRef target);
	void processData(const QByteArray& data, int* prevLineNum = NULL);

	Git* git;
	DiffHighlighter* diffHighlighter;
	QPointer<MyProcess> proc;
	bool diffLoaded;
	QByteArray patchRowData;
	QString halfLine;
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	bool isRegExp;
	QRegExp pickAxeRE;
#else
	QRegularExpression pickAxeRE;
#endif
	QString target;
	bool seekTarget;

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
