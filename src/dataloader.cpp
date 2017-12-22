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
#include "break_point.h"

#define GUI_UPDATE_INTERVAL 500
#define READ_BLOCK_SIZE     65535

class UnbufferedTemporaryFile : public QTemporaryFile {
public:
	explicit UnbufferedTemporaryFile(QObject* p) : QTemporaryFile(p) {}
	bool unbufOpen() { return open(QIODevice::ReadOnly | QIODevice::Unbuffered); }
};

DataLoader::DataLoader(Git* g, FileHistory* f) : QProcess(g), git(g), fh(f) {

    parsing = false;
    canceling = false;
    procFinished = true;
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

bool DataLoader::start(const QStringList& args, const QString& wd, const QString& buf) {

    if (!procFinished) {
		dbs("ASSERT in DataLoader::start(), called while processing");
		return false;
	}
    procFinished = false;
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

    procFinished = true;

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

    qint64 len = readNewData();
    if (len == -1) {
        emit loaded(fh, loadedBytes, loadTime.elapsed(), true, "", "");
        deleteLater();
        return;
    }
    else if (len > 0) {
        loadedBytes += len;
        emit newDataReady(fh);
    }

    if (procFinished) {
        dbs("Exited while parsing!!!!");
        guiUpdateTimer.start(1);
    }
    else
        guiUpdateTimer.start(GUI_UPDATE_INTERVAL);

	parsing = false;
}

// *************** git interface facility dependant code *****************************

//#ifdef USE_QPROCESS

//ulong DataLoader::readNewData(bool lastBuffer) {

//	/*
//	   QByteArray copy c'tor uses shallow copy, but there is a deep copy in
//	   QProcess::readStdout(), from an internal buffers list to return value.

//	   Qt uses a select() to detect new data is ready, copies immediately the
//	   data to the heap with a read() and stores the pointer to new data in a
//	   pointer list, from qprocess_unix.cpp:

//		const int basize = 4096;
//		QByteArray *ba = new QByteArray(basize);
//		n = ::read(fd, ba->data(), basize);
//		buffer->append(ba); // added to a QPtrList<QByteArray> pointer list

//	   When we call QProcess::readStdout() data from buffers pointed by the
//	   pointer list is memcpy() to the function return value, from qprocess.cpp:

//		....
//		return buf->readAll(); // memcpy() here
//	*/
//	QByteArray* ba = new QByteArray(readAllStandardOutput());
//	if (lastBuffer)
//		ba->append('\0'); // be sure stream is null terminated

//	if (ba->size() == 0) {
//		delete ba;
//		return 0;
//	}
//	fh->rowData.append(ba);
//	parseSingleBuffer(*ba);
//	return ba->size();
//}

//bool DataLoader::createTemporaryFile() { return true; }

//#else // temporary file as data exchange facility

qint64 DataLoader::readNewData() {

	bool ok = dataFile &&
	         (dataFile->isOpen() || (dataFile->exists() && dataFile->unbufOpen()));

	if (!ok)
		return 0;

    QByteArray ba;
    ba.resize(READ_BLOCK_SIZE);
    qint64 len = dataFile->read((char*) ba.constData(), READ_BLOCK_SIZE);

    QTextCodec* tc = QTextCodec::codecForLocale();

    if (len == 0) {
        bool atEnd = dataFile->atEnd();
        if (procFinished && atEnd) {
            if (!rawBuff.isEmpty()) {
                rawBuff.append('\0');
                QString s = tc->toUnicode(rawBuff);
                git->addChunk(fh, s);
            }
            return -1;
        }
        return 0;
    }
    if (len < ba.size())
        ba.resize(int(len));

    rawBuff.append(ba);

    int pos = -1;
    while ((pos = rawBuff.indexOf('\0')) != -1) {
        QByteArray b = QByteArray::fromRawData(rawBuff.constData(), pos + 1);
        QString s = tc->toUnicode(b);
        git->addChunk(fh, s);
        rawBuff.remove(0, pos + 1);
    }
    return len;
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

//#endif // USE_QPROCESS
