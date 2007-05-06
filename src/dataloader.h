/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef DATALOADER_H
#define DATALOADER_H

#include <QProcess>
#include <QTime>
#include <QTimer>

class QString;
class QTemporaryFile;
class Git;
class FileHistory;

// data exchange facility with git-rev-list could be based on QProcess or on
// a temporary file (default). Uncomment following line to use QProcess
// #define USE_QPROCESS

class DataLoader : public QProcess {
Q_OBJECT
public:
	DataLoader(Git* g, FileHistory* f);
	~DataLoader();
	bool start(const QStringList& args, const QString& wd, const QString& buf);

signals:
	void newDataReady(const FileHistory*);
	void loaded(const FileHistory*,ulong,int,bool,const QString&,const QString&);

private slots:
	void on_finished(int, QProcess::ExitStatus);
	void on_cancel();
	void on_cancel(const FileHistory*);
	void on_timeout();

private:
	void parseSingleBuffer(const QByteArray& ba);
	void baAppend(QByteArray** src, const char* ascii, int len);
	void addSplittedChunks(const QByteArray* halfChunk);
	bool createTemporaryFile();
	ulong readNewData(bool lastBuffer);

	Git* git;
	FileHistory* fh;
	QByteArray* halfChunk;
	QTemporaryFile* dataFile;
	QTime loadTime;
	QTimer guiUpdateTimer;
	ulong loadedBytes;
	bool isProcExited;
	bool parsing;
	bool canceling;
};

#endif
