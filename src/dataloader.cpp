/*
	Description: async stream reader, used to load repository data on startup

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QDir>
#include "myprocess.h"
#include "git.h"
#include "dataloader.h"

#define GUI_UPDATE_INTERVAL 500
#define READ_BLOCK_SIZE     65535

DataLoader::DataLoader(Git* g, FileHistory* f) : QObject(g), git(g), fh(f) {

	canceling = parsing = false;
	isProcExited = true;
	halfChunk = NULL;
	proc = NULL;
	loadedBytes = 0;
	guiUpdateTimer.setSingleShot(true);

	connect(git, SIGNAL(cancelAllProcesses()), this, SLOT(on_cancel()));
	connect(&guiUpdateTimer, SIGNAL(timeout()), this, SLOT(on_timeout()));
}

void DataLoader::on_cancel(const FileHistory* f) {

	if (f == fh)
		on_cancel();
}

bool DataLoader::start(SCList args, SCRef wd) {

	if (!isProcExited) {
		dbs("ASSERT in DataLoader::start, called while processing");
		return false;
	}
	isProcExited = false;
	if (!doStart(args, wd))
		return false;

	loadTime.start();
	guiUpdateTimer.start(GUI_UPDATE_INTERVAL);
	return true;
}

void DataLoader::on_finished(int, QProcess::ExitStatus) {

	isProcExited = true;

	if (parsing && guiUpdateTimer.isActive())
		dbs("ASSERT in DataLoader: timer active while parsing");

	if (parsing == guiUpdateTimer.isActive())
		dbs("ASSERT in DataLoader: inconsistent timer");

	if (guiUpdateTimer.isActive()) // no need to wait anymore
		guiUpdateTimer.start(1);
}

void DataLoader::on_timeout() {

	if (canceling) {
		deleteLater();
		return;
	}
	parsing = true;

	// process could exit while we are processing so save the flag now
	bool lastBuffer = isProcExited;
	loadedBytes += readNewData(lastBuffer);
	emit newDataReady(fh); // inserting in list view is about 3% of total time

	if (lastBuffer) {
		emit loaded(fh, loadedBytes, loadTime.elapsed(), true, "", "");
		deleteLater();

	} else if (isProcExited) { // exited while parsing
		dbs("Exited while parsing!!!!");
		guiUpdateTimer.start(1);
	} else
		guiUpdateTimer.start(GUI_UPDATE_INTERVAL);

	parsing = false;
}

void DataLoader::parseSingleBuffer(const QByteArray& ba) {

	if (ba.size() == 0 || canceling)
		return;

	int ofs = 0, newOfs, bz = ba.size();
	while (bz - ofs > 0) {

		if (!halfChunk) {

			newOfs = git->addChunk(fh, ba, ofs);
			if (newOfs == -1)
				break; // half chunk detected

			ofs = newOfs;

		} else { // less then 1% of cases with READ_BLOCK_SIZE = 64KB

			int end = ba.indexOf('\0');
			if (end == -1) // consecutives half chunks
				break;

			ofs = end + 1;
			baAppend(&halfChunk, ba.constData(), ofs);
			fh->rowData.append(halfChunk);
			addSplittedChunks(halfChunk);
			halfChunk = NULL;
		}
	}
	// save any remaining half chunk
	if (bz - ofs > 0)
		baAppend(&halfChunk, ba.constData() + ofs,  bz - ofs);
}

void DataLoader::addSplittedChunks(const QByteArray* hc) {

	if (hc->at(hc->size() - 1) != 0) {
		dbs("ASSERT in DataLoader, bad half chunk");
		return;
	}
	// do not assume we have only one chunk in hc
	int ofs = 0;
	while (ofs != -1 && ofs != (int)hc->size())
		ofs = git->addChunk(fh, *hc, ofs);
}

void DataLoader::baAppend(QByteArray** baPtr, const char* ascii, int len) {

	if (*baPtr)
		// we cannot use QByteArray::append(const char*)
		// because 'ascii' is not '\0' terminating
		(*baPtr)->append(QByteArray::fromRawData(ascii, len));
	else
		*baPtr = new QByteArray(ascii, len);
}

// *************** git interface facility dependant code *****************************

#ifdef USE_QPROCESS

DataLoader::~DataLoader() {}

void DataLoader::on_cancel() {

	if (!canceling) { // just once
		canceling = true;
		if (proc)
			proc->terminate();
	}
}

bool DataLoader::doStart(SCList args, SCRef wd) {

	proc = new QProcess(this);
	proc->setWorkingDirectory(wd);
	if (!QGit::startProcess(proc, args))
		return false;

	connect(proc, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(on_finished(int, QProcess::ExitStatus)));
	// signal readyReadStdout() is not connected, read is timeout based. Faster.
	return true;
}

void DataLoader::procReadyRead(const QString&) { /* timeout based */ }

ulong DataLoader::readNewData(bool) {

	/*
	   QByteArray copy c'tor uses shallow copy, but there is a deep copy in
	   QProcess::readStdout(), from an internal buffers list to return value.

	   Qt uses a select() to detect new data is ready, copies immediately the
	   data to the heap with a read() and stores the pointer to new data in a
	   pointer list, from qprocess_unix.cpp:

		const int basize = 4096;
		QByteArray *ba = new QByteArray( basize );
		n = ::read( fd, ba->data(), basize );
		buffer->append( ba ); // added to a QPtrList<QByteArray> pointer list

	   When we call QProcess::readStdout() data from buffers pointed by the
	   pointer list is memcpy() to the function return value, from qprocess.cpp:

		....
		return buf->readAll(); // memcpy() here
	*/
	QByteArray* ba = new QByteArray(proc->readAllStandardOutput());
	if (ba->size() == 0) {
		delete ba;
		return 0;
	}
	fh->rowData.append(ba);
	parseSingleBuffer(*ba);
	return ba->size();
}

#else // temporary file as data exchange facility

DataLoader::~DataLoader() {

	if (dataFile.isOpen())
		dataFile.close();

	QDir dir;
	dir.remove(dataFileName);
	dir.remove(scriptFileName);
}

void DataLoader::on_cancel() {

	if (!canceling) { // just once
		canceling = true;
		git->cancelProcess(proc);
		if (!procPID.isEmpty())
			git->run("kill " + procPID.stripWhiteSpace());
	}
}

bool DataLoader::doStart(SCList args, SCRef wd) {

	// ensure unique names for our DataLoader instance file
	dataFileName = "/qgit_" + QString::number((ulong)this) + ".txt";
	scriptFileName = "/qgit_" + QString::number((ulong)this) + QGit::SCRIPT_EXT;

	// create a script to redirect 'git rev-list' stdout to dataFile
	QDir dir("/tmp"); // use a tmpfs mounted filesystem if available
	dataFileName.prepend(dir.exists() && dir.isReadable() ? "/tmp" : wd);
	dataFile.setName(dataFileName);
	QString runCmd(args.join(" ") + " > " +  dataFileName);
	runCmd.append(" &\necho $!\nwait"); // we want to read git-rev-list PID
	scriptFileName.prepend(wd);
	if (!QGit::writeToFile(scriptFileName, runCmd, true))
		return false;

	proc = git->runAsync(scriptFileName, this, "");
	return (proc != NULL);
}

void DataLoader::procReadyRead(const QString& data) {
// the script sends pid of launched git-rev-list, to be used for canceling

	procPID.append(data);
}

ulong DataLoader::readNewData(bool lastBuffer) {

	bool ok =     dataFile.isOpen()
	          || (dataFile.exists() && dataFile.open(QIODevice::Unbuffered | QIODevice::ReadOnly));
	if (!ok)
		return 0;

	ulong cnt = 0;
	qint64 readPos = dataFile.pos();

	while (!dataFile.atEnd()) {

		// this is the ONLY deep copy involved in the whole loading
		// QFile::read() calls standard C read() function when
		// file is open with Unbuffered flag, or fread() otherwise
		QByteArray* ba = new QByteArray(READ_BLOCK_SIZE);
		uint len = dataFile.read(ba->data(), READ_BLOCK_SIZE);
		if (len <= 0) {
			delete ba;
			break;

		} else if (len < READ_BLOCK_SIZE) // unlikely
			ba->resize(len);

		// current read position must be updated manually, it's
		// not correctly incremented by read() if the producer
		// process has already finished
		readPos += len;
		dataFile.seek(readPos);

		cnt += len;
		fh->rowData.append(ba);
		parseSingleBuffer(*ba);

		// avoid reading small chunks if data producer is still running
		if (len < READ_BLOCK_SIZE && !lastBuffer)
			break;
	}
	return cnt;
}

#endif // USE_QPROCESS
