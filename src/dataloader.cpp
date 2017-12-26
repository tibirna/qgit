/*
	Description: async stream reader, used to load repository data on startup

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QDir>
#include <QTemporaryFile>
#include "defmac.h"
#include "FileHistory.h"
#include "git.h"
#include "dataloader.h"

#define GUI_UPDATE_INTERVAL 500
#define READ_BLOCK_SIZE     65535

class UnbufferedTemporaryFile : public QTemporaryFile {
public:
	explicit UnbufferedTemporaryFile(QObject* p) : QTemporaryFile(p) {}
	bool unbufOpen() { return open(QIODevice::ReadOnly | QIODevice::Unbuffered); }
};

DataLoader::DataLoader(Git* g, FileHistory* f) : QProcess(g), git(g), fh(f) {

	canceling = parsing = false;
	isProcExited = true;
	halfChunk = NULL;
	dataFile = NULL;
	loadedBytes = 0;
	guiUpdateTimer.setSingleShot(true);

    chk_connect_a(git, SIGNAL(cancelAllProcesses()), this, SLOT(on_cancel()));
    chk_connect_a(&guiUpdateTimer, SIGNAL(timeout()), this, SLOT(on_timeout()));
}

DataLoader::~DataLoader() {

	// avoid a Qt warning in case we are
	// destroyed while still running
	waitForFinished(1000);
}

void DataLoader::on_cancel(const FileHistory* f) {

	if (f == fh)
		on_cancel();
}

void DataLoader::on_cancel() {

	if (!canceling) { // just once
		canceling = true;
		kill(); // SIGKILL (Unix and Mac), TerminateProcess (Windows)
	}
}

bool DataLoader::start(SCList args, SCRef wd, SCRef buf) {

	if (!isProcExited) {
		dbs("ASSERT in DataLoader::start(), called while processing");
		return false;
	}
	isProcExited = false;
	setWorkingDirectory(wd);

    chk_connect_a(this, SIGNAL(finished(int, QProcess::ExitStatus)),
                  this, SLOT(on_finished(int, QProcess::ExitStatus)));

	if (!createTemporaryFile() || !QGit::startProcess(this, args, buf)) {
		deleteLater();
		return false;
	}
	loadTime.start();
	guiUpdateTimer.start(GUI_UPDATE_INTERVAL);
	return true;
}

void DataLoader::on_finished(int, QProcess::ExitStatus) {

	isProcExited = true;

	if (parsing && guiUpdateTimer.isActive())
		dbs("ASSERT in DataLoader: timer active while parsing");

	if (parsing == guiUpdateTimer.isActive() && !canceling)
		dbs("ASSERT in DataLoader: inconsistent timer");

	if (guiUpdateTimer.isActive()) // no need to wait anymore
		guiUpdateTimer.start(1);
}

void DataLoader::on_timeout() {

	if (canceling) {
		deleteLater();
		return; // we leave with guiUpdateTimer not active
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

	/* Due to unknown reasons randomly first byte
	 * of 'ba' is 0, this seems to happen only when
	 * using QFile::read(), i.e. with temporary file
	 * interface. Until we discover the real reason
	 * workaround this skipping the bogus byte
	 */
	if (ba.at(0) == 0 && bz > 1 && !halfChunk)
		ofs++;

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

ulong DataLoader::readNewData(bool lastBuffer) {

	/*
	   QByteArray copy c'tor uses shallow copy, but there is a deep copy in
	   QProcess::readStdout(), from an internal buffers list to return value.

	   Qt uses a select() to detect new data is ready, copies immediately the
	   data to the heap with a read() and stores the pointer to new data in a
	   pointer list, from qprocess_unix.cpp:

		const int basize = 4096;
		QByteArray *ba = new QByteArray(basize);
		n = ::read(fd, ba->data(), basize);
		buffer->append(ba); // added to a QPtrList<QByteArray> pointer list

	   When we call QProcess::readStdout() data from buffers pointed by the
	   pointer list is memcpy() to the function return value, from qprocess.cpp:

		....
		return buf->readAll(); // memcpy() here
	*/
	QByteArray* ba = new QByteArray(readAllStandardOutput());
	if (lastBuffer)
		ba->append('\0'); // be sure stream is null terminated

	if (ba->size() == 0) {
		delete ba;
		return 0;
	}
	fh->rowData.append(ba);
	parseSingleBuffer(*ba);
	return ba->size();
}

bool DataLoader::createTemporaryFile() { return true; }

#else // temporary file as data exchange facility

ulong DataLoader::readNewData(bool lastBuffer) {

	bool ok = dataFile &&
	         (dataFile->isOpen() || (dataFile->exists() && dataFile->unbufOpen()));

	if (!ok)
		return 0;

	ulong cnt = 0;
	qint64 readPos = dataFile->pos();

	while (true) {
		// this is the ONLY deep copy involved in the whole loading
		// QFile::read() calls standard C read() function when
		// file is open with Unbuffered flag, or fread() otherwise
		QByteArray* ba = new QByteArray();
		ba->resize(READ_BLOCK_SIZE);
        int len = static_cast<int>(dataFile->read(ba->data(), READ_BLOCK_SIZE));

		if (len <= 0) {
			delete ba;
			break;

		} else if (len < ba->size()) // unlikely
			ba->resize(len);

		// current read position must be updated manually, it's
		// not correctly incremented by read() if the producer
		// process has already finished
		readPos += len;
		dataFile->seek(readPos);

		cnt += len;
		fh->rowData.append(ba);
		parseSingleBuffer(*ba);

		// avoid reading small chunks if data producer is still running
		if (len < READ_BLOCK_SIZE && !lastBuffer)
			break;
	}
	if (lastBuffer) { // be sure stream is null terminated
		QByteArray* zb = new QByteArray(1, '\0');
		fh->rowData.append(zb);
		parseSingleBuffer(*zb);
	}
	return cnt;
}

bool DataLoader::createTemporaryFile() {

	// redirect 'git log' output to a temporary file
	dataFile = new UnbufferedTemporaryFile(this);

#ifndef Q_OS_WIN32
	/*
	   For performance reasons we would like to use a tmpfs filesystem
	   if available, this is normally mounted under '/tmp' in Linux.

	   According to Qt docs, a temporary file is placed in QDir::tempPath(),
	   that should be system's temporary directory. On Unix/Linux systems this
	   is usually /tmp; on Windows this is usually the path in the TEMP or TMP
	   environment variable.

	   But due to a bug in Qt 4.2 QDir::tempPath() is instead set to $HOME/tmp
	   under Unix/Linux, that is not a tmpfs filesystem.

	   So try to manually set the best directory for our temporary file.
	*/
		QDir dir("/tmp");
		bool foundTmpDir = (dir.exists() && dir.isReadable());
		if (foundTmpDir && dir.absolutePath() != QDir::tempPath()) {

			dataFile->setFileTemplate(dir.absolutePath() + "/qt_temp");
			if (!dataFile->open()) { // test for write access

				delete dataFile;
				dataFile = new UnbufferedTemporaryFile(this);
				dbs("WARNING: directory '/tmp' is not writable, "
				    "fallback on Qt default one, there could "
				    "be a performance penalty.");
			} else
				dataFile->close();
		}
#endif
	if (!dataFile->open()) // to read the file name
		return false;

	setStandardOutputFile(dataFile->fileName());
	dataFile->close();
	return true;
}

#endif // USE_QPROCESS
