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
	ReachInfo() {};
	ReachInfo(SCRef s, int i, int t) : sha(s), id(i), type(t) {};
	const QString sha;
	int id, type;
	QStringList roots;
};
typedef QVector<ReachInfo> ReachList;

class RangeInfo {
public:
	RangeInfo() { clear(); };
	RangeInfo(int s, int e, bool m) : start(s), end(e), modified(m) {};
	void clear() { start = end = 0; modified = false; };
	int start, end; // ranges count file lines from 1 like patches diffs
	bool modified;
};

class Annotate : public QObject {
Q_OBJECT
public:
	Annotate(Git* parent, QObject* guiObj);
	void deleteWhenDone();
	const FileAnnotation* lookupAnnotation(SCRef sha, SCRef fileName);
	bool start(const FileHistory* fh);
	bool isValid() { return valid; };
	bool isCanceled() { return canceled; };
	int count() { return ah.count(); };
	int elapsed() { return processingTime.elapsed(); };
	const QString file() { return fileName; };
	const QString getAncestor(SCRef sha, SCRef fileName, int* shaIdx);
	bool getRange(SCRef sha, RangeInfo* r);
	bool seekPosition(int* rangeStart, int* rangeEnd, SCRef fromSha, SCRef toSha);
	const QString computeRanges(SCRef sha, int rStart, int rEnd, SCRef target = "");

public slots:
	void procReadyRead(const QByteArray&);
	void procFinished();

private slots:
	void on_progressTimer_timeout();
	void on_deleteWhenDone();
	void slotComputeDiffs();

private:
	typedef QMap<QString, FileAnnotation> AnnotateHistory;

	void annotateFileHistory(SCRef fileName, bool buildShaList);
	void doAnnotate(SCRef fileName, SCRef sha, bool buildShaList);
	FileAnnotation* getFileAnnotation(SCRef sha);
	void setInitialAnnotation(SCRef fileName, SCRef sha, FileAnnotation* fa);
	const QString setupAuthor(SCRef origAuthor, int annId);
	void setAnnotation(SCRef diff, SCRef aut, SCLList pAnn, SLList nAnn, int ofs = 0);
	bool getNextLine(SCRef d, int& idx, QString& line);
	static void unify(SLList dst, SCLList src);
	void updateShaList(SCRef sha, SCRef par);
	const QString getNextPatch(QString& patchFile, SCRef fileName, SCRef sha);
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
	QStringList shaList;
	QString fileName;
	StrVect histRevOrder; // TODO use reference
	QString patchProcBuf;
	QPointer<MyProcess> patchProc;
	QString nextFileSha;
	bool valid;
	bool canceled;
	QTime processingTime;
	QTimer progressTimer;

	typedef QPair<QString, uint> Key;
	QMap<Key, QString> diffMap; // QPair(sha, parentNr)
	QMap<QString, RangeInfo> rangeMap;
};

#endif
