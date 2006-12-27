/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef DATALOADER_H
#define DATALOADER_H

#include <qobject.h>
#include <qtimer.h>
#include <qdatetime.h>
#include <qfile.h>

class QString;
class Git;
class FileHistory;
class Q3Process;
class MyProcess;

#ifdef _WINDOWS
#define USE_QPROCESS
#else
// data exchange facility with git-rev-list could be based on QProcess or on
// a temporary file (default). Uncomment following line to use QProcess
// #define USE_QPROCESS
#endif

class DataLoader : public QObject {
Q_OBJECT
public:
	DataLoader(Git* g, FileHistory* f);
	~DataLoader();
	bool start(const QStringList& args, const QString& wd);

public slots:
	void procReadyRead(const QString&);
	void procFinished();

signals:
	void newDataReady(const FileHistory*);
	void loaded(const FileHistory*,ulong,int,bool,const QString&,const QString&);

private slots:
	void on_cancel();
	void on_cancel(const FileHistory*);
	void on_timeout();

private:
	void parseSingleBuffer(const QByteArray& ba);
	void baAppend(QByteArray** src, const char* ascii, int len);
	void addSplittedChunks(const QByteArray* halfChunk);
	bool doStart(const QStringList& args, const QString& wd);
	ulong readNewData(bool lastBuffer);

	Git* git;
	FileHistory* fh;
	QByteArray* halfChunk;
	QTime loadTime;
	QTimer guiUpdateTimer;
	ulong loadedBytes;
	bool isProcExited;
	bool parsing;
	bool canceling;

#ifdef USE_QPROCESS
	Q3Process* proc;
#else
	MyProcess* proc;
	QString procPID;
	QString scriptFileName;
	QString dataFileName;
	QFile dataFile;
#endif
};

#endif
