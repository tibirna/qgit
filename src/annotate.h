/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef ANNOTATE_H
#define ANNOTATE_H

#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTime>
#include <QTimer>
#include "exceptionmanager.h"
#include "common.h"

class Git;
class FileHistory;
class MyProcess;

class ReachInfo {
public:
	ReachInfo() {}
	ReachInfo(SCRef s, int i, int t) : sha(s), id(i), type(t) {}
	const QString sha;
	int id, type;
	QStringList roots;
};
typedef QVector<ReachInfo> ReachList;

class RangeInfo {
public:
	RangeInfo() { clear(); }
	RangeInfo(int s, int e, bool m) : start(s), end(e), modified(m) {}
	void clear() { start = end = 0; modified = false; }
	int start, end; // ranges count file lines from 1 like patches diffs
	bool modified;
};

class Annotate : public QObject {
Q_OBJECT
public:
	Annotate(Git* parent, QObject* guiObj);
	void deleteWhenDone();
	const FileAnnotation* lookupAnnotation(SCRef sha);
	bool start(const FileHistory* fh);
	bool isCanceled() { return canceled; }
	const QString getAncestor(SCRef sha, int* shaIdx);
	bool getRange(SCRef sha, RangeInfo* r);
	bool seekPosition(int* rangeStart, int* rangeEnd, SCRef fromSha, SCRef toSha);
	const QString computeRanges(SCRef sha, int rStart, int rEnd, SCRef target = "");

signals:
	void annotateReady(Annotate*, bool, const QString&);

private slots:
	void on_deleteWhenDone();
	void slotComputeDiffs();

private:
	typedef QMap<QString, FileAnnotation> AnnotateHistory;

	void annotateFileHistory();
	void doAnnotate(SCRef sha);
	FileAnnotation* getFileAnnotation(SCRef sha);
	void setInitialAnnotation(SCRef fileSha, FileAnnotation* fa);
	const QString setupAuthor(SCRef origAuthor, int annId);
	void setAnnotation(SCRef diff, SCRef aut, SCLList pAnn, SLList nAnn, int ofs = 0);
	bool getNextLine(SCRef d, int& idx, QString& line);
	static void unify(SLList dst, SCLList src);
	const QString getPatch(SCRef sha, int parentNum = 0);
	bool getNextSection(SCRef d, int& idx, QString& sec, SCRef target);
	void updateRange(RangeInfo* r, SCRef diff, bool reverse);
	void updateCrossRanges(SCRef cnk, bool rev, int oStart, int oLineCnt, RangeInfo* r);
	bool isDescendant(SCRef sha, SCRef target);

	EM_DECLARE(exAnnCanceled);

	Git* git;
	QObject* gui;
	const FileHistory* fh;
	AnnotateHistory ah;
	bool cancelingAnnotate;
	bool annotateRunning;
	bool annotateActivity;
	bool isError;
	int annNumLen;
	int annId;
	int annFilesNum;
	StrVect histRevOrder; // TODO use reference
	bool valid;
	bool canceled;
	QTime processingTime;
	QMap<QString, RangeInfo> rangeMap;
};

#endif
