/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef FILECONTENT_H
#define FILECONTENT_H

#include <qobject.h>
#include <qpointer.h>
#include "common.h"

class Q3TextEdit;
class Q3ListView;
class FileHighlighter;
class Domain;
class StateInfo;
class Annotate;
class Git;
class MyProcess;
class RangeInfo;
class FileHistory;

class FileContent: public QObject {
Q_OBJECT
public:
	FileContent(Domain* parent, Git* git, Q3TextEdit* f);
	~FileContent();
	void update(bool force = false);
	bool annotateAvailable() { return curAnn != NULL; };
	void clear();
	void copySelection();
	void goToAnnotation(int id);
	bool goToRangeStart();
	bool rangeFilter(bool b);
	bool getRange(SCRef sha, RangeInfo* r);
	void startAnnotate(FileHistory* fh);
	void setShowAnnotate(bool b);
	void setHighlightSource(bool b);

signals:
	void annotationAvailable(bool);
	void fileAvailable(bool);
	void revIdSelected(int);

public slots:
	void on_doubleClicked(int, int);
	void on_annotateReady(Annotate*, const QString&, bool, const QString&);
	void procReadyRead(const QString&);
	void procFinished(bool emitSignal = true);

private:
	friend class FileHighlighter;

	void clearAnnotate();
	void clearText(bool emitSignal = true);
	void findInFile(SCRef str);
	bool lookupAnnotation();
	uint annotateLength(const FileAnnotation* curAnn);
	void saveScreenState();
	void restoreScreenState();
	uint processData(const QString& fileChunk);

	Domain* d;
	Git* git;
	Q3TextEdit* ft;
	StateInfo* st;
	RangeInfo* rangeInfo;
	FileHighlighter* fileHighlighter;
	QPointer<Annotate> annotateObj;
	QPointer<MyProcess> proc;
	const FileAnnotation* curAnn;
	QString fileRowData;
	QString fileProcessedData;
	QString halfLine;
	uint curLine;
	QLinkedList<QString>::const_iterator curAnnIt;
	uint annoLen;
	bool isFileAvailable;
	bool isAnnotationAvailable;
	bool isAnnotationAppended;
	bool isRangeFilterActive;
	bool isShowAnnotate;
	bool isHtmlSource;

	struct ScreenState {
		bool isValid, hasSelectedText, isAnnotationAppended;
		int topPara, paraFrom, indexFrom, paraTo, indexTo, annoLen;
	};
	ScreenState ss;

	static const QString HTML_HEAD;
	static const QString HTML_TAIL;
	static const QString HTML_FILE_START;
	static const QString HTML_FILE_END;
};

#endif
