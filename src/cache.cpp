/*
	Description: file names persistent cache

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QFile>
#include <QDir>
#include "cache.h"

using namespace QGit;

bool Cache::save(const QString& gitDir, const RevFileMap& rf,
                 const StrVect& dirs, const StrVect& files) {

	if (gitDir.isEmpty() || rf.isEmpty())
		return false;

	QString path(gitDir + C_DAT_FILE);
	QString tmpPath(path + BAK_EXT);

	QDir dir;
	if (!dir.exists(gitDir)) {
		dbs("Git directory not found, unable to save cache");
		return false;
	}
	QFile f(tmpPath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Unbuffered))
		return false;

	dbs("Saving cache. Please wait...");

	// compress in memory before write to file
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);

	// Write a header with a "magic number" and a version
	stream << (quint32)C_MAGIC;
	stream << (qint32)C_VERSION;

	stream << (qint32)dirs.count();
	for (int i = 0; i < dirs.count(); ++i)
		stream << dirs.at(i);

	stream << (qint32)files.count();
	for (int i = 0; i < files.count(); ++i)
		stream << files.at(i);

	// to achieve a better compression we save the sha's as
	// one very long string instead of feeding the stream with
	// each one. With this trick we gain a 15% size reduction
	// in the final compressed file. The save/load speed is
	// almost the same.
	uint bufSize = rf.count() * 41 + 1000; // a little bit more space then required

	QByteArray buf;
	buf.reserve(bufSize);

	QVector<const RevFile*> v;
	v.reserve(rf.count());

	QVector<QByteArray> ba;
	ShaString CUSTOM_SHA_RAW(toPersistentSha(CUSTOM_SHA, ba));
	unsigned int newSize = 0;

	FOREACH (RevFileMap, it, rf) {

		const ShaString& sha = it.key();
		if (   sha == ZERO_SHA_RAW
		    || sha == CUSTOM_SHA_RAW
		    || sha.latin1()[0] == 'A') // ALL_MERGE_FILES + rev sha
			continue;

		v.append(it.value());
		buf.append(sha.latin1()).append('\0');
		newSize += 41;
		if (newSize > bufSize) {
			dbs("ASSERT in Cache::save, out of allocated space");
			return false;
		}
	}
	buf.resize(newSize);
	stream << (qint32)newSize;
	stream << buf;

	for (int i = 0; i < v.size(); ++i)
		*(v.at(i)) >> stream;

	dbs("Compressing data...");
	f.write(qCompress(data, 1)); // no need to encode with compressed data
	f.close();

	// rename C_DAT_FILE + BAK_EXT -> C_DAT_FILE
	if (dir.exists(path)) {
		if (!dir.remove(path)) {
			dbs("access denied to " + path);
			dir.remove(tmpPath);
			return false;
		}
	}
	dir.rename(tmpPath, path);
	dbs("Done.");
	return true;
}

bool Cache::load(const QString& gitDir, RevFileMap& rfm,
                 StrVect& dirs, StrVect& files, QByteArray& revsFilesShaBuf) {

	// check for cache file
	QString path(gitDir + C_DAT_FILE);
	QFile f(path);
	if (!f.exists())
		return true; // no cache file is not an error

	if (!f.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		return false;

	QDataStream stream(qUncompress(f.readAll()));
	quint32 magic;
	qint32 version;
	qint32 dirsNum, filesNum, bufSize;
	stream >> magic;
	stream >> version;
	if (magic != C_MAGIC || version != C_VERSION) {
		f.close();
		return false;
	}
	// read the data
	stream >> dirsNum;
	dirs.resize(dirsNum);
	for (int i = 0; i < dirsNum; ++i)
		stream >> dirs[i];

	stream >> filesNum;
	files.resize(filesNum);
	for (int i = 0; i < filesNum; ++i)
		stream >> files[i];

	stream >> bufSize;
	revsFilesShaBuf.clear();
	revsFilesShaBuf.reserve(bufSize);
	stream >> revsFilesShaBuf;

	const char* data = revsFilesShaBuf.constData();

	while (!stream.atEnd()) {

		RevFile* rf = new RevFile();
		*rf << stream;

		ShaString sha(data);
		rfm.insert(sha, rf);

		data += 40;
		if (*data != '\0') {
			dbp("ASSERT in Cache::load, corrupted SHA after %1", sha);
			return false;
		}
		data++;
	}
	f.close();
	return true;
}
