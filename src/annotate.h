/*
    Author: Marco Costalba (C) 2005-2007

    Copyright: See COPYING file that comes with this distribution

*/
#ifndef ANNOTATE_H
#define ANNOTATE_H

#include <QObject>
#include <QTime>
#include "exceptionmanager.h"
#include "common.h"

class Git;
class FileHistory;
class MyProcess;


class RangeInfo {
public:
    RangeInfo() { clear(); }
    RangeInfo(int s, int e, bool m) : start(s), end(e), modified(m) {}
    void clear() { start = end = 0; modified = false; }
    int start, end; // ranges count file lines from 1 like patches diffs
    bool modified;
};
typedef QHash<QString, RangeInfo> Ranges;

class Annotate : public QObject {
Q_OBJECT
public:
    Annotate(Git* parent, QObject* guiObj);
    void deleteWhenDone();
    const FileAnnotation* lookupAnnotation(const QString& sha);
    bool start(const FileHistory* fh);
    bool isCanceled() { return canceled; }
    const QString getAncestor(const QString& sha, int* shaIdx);
    bool getRange(const QString& sha, RangeInfo* r);
    bool seekPosition(int* rangeStart, int* rangeEnd, const QString& fromSha, const QString& toSha);
    const QString computeRanges(const QString& sha, int paraFrom, int paraTo, const QString& target = "");

signals:
    void annotateReady(Annotate*, bool, const QString&);

private slots:
    void on_deleteWhenDone();
    void slotComputeDiffs();

private:
    void annotateFileHistory();
    void doAnnotate(const ShaString& sha);
    FileAnnotation* getFileAnnotation(const QString& sha);
    void setInitialAnnotation(const QString& fileSha, FileAnnotation* fa);
    const QString setupAuthor(const QString& origAuthor, int annId);
    bool setAnnotation(const QString& diff, const QString& aut, const QStringList& pAnn, QStringList& nAnn, int ofs = 0);
    bool getNextLine(const QString& d, int& idx, QString& line);
    static void unify(QStringList& dst, const QStringList& src);
    const QString getPatch(const QString& sha, int parentNum = 0);
    bool getNextSection(const QString& d, int& idx, QString& sec, const QString& target);
    void updateRange(RangeInfo* r, const QString& diff, bool reverse);
    void updateCrossRanges(const QString& cnk, bool rev, int oStart, int oLineCnt, RangeInfo* r);
    bool isDescendant(const QString& sha, const QString& target);

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
    ShaVect histRevOrder; // TODO use reference
    bool valid;
    bool canceled;
    QTime processingTime;
    Ranges ranges;
};

#endif
