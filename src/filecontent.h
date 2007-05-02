/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef FILECONTENT_H
#define FILECONTENT_H

#include <QPointer>
#include <QTextEdit>
#include "common.h"

class FileHighlighter;
class Domain;
class StateInfo;
class Annotate;
class Git;
class MyProcess;
class RangeInfo;
class FileHistory;

class FileContent: public QTextEdit {
Q_OBJECT
public:
	FileContent(QWidget* parent);
	~FileContent();
	void setup(Domain* parent, Git* git);
	void update(bool force = false);
	bool annotateAvailable() { return curAnn != NULL; };
	void clearAll();
	void copySelection();
	void goToAnnotation(int id);
	bool goToRangeStart();
	bool rangeFilter(bool b);
	bool getRange(SCRef sha, RangeInfo* r);
	bool startAnnotate(FileHistory* fh);
	void setShowAnnotate(bool b);
	void setHighlightSource(bool b);
	void setSelection(int paraFrom, int indexFrom, int paraTo, int indexTo);

signals:
	void annotationAvailable(bool);
	void fileAvailable(bool);
	void revIdSelected(int);

public slots:
	void on_annotateReady(Annotate*, const QString&, bool, const QString&);
	void procReadyRead(const QByteArray&);
	void procFinished(bool emitSignal = true);

private:
	friend class FileHighlighter;

	void clear(); // declared as private, to avoid indirect access to QTextEdit::clear()
	void clearAnnotate();
	void clearText(bool emitSignal);
	bool isCurAnnotation(SCRef annLine);
	void findInFile(SCRef str);
	void scrollCursorToTop();
	void scrollLineToTop(int lineNum);
	int positionToLineNum(int pos);
	bool lookupAnnotation();
	uint annotateLength(const FileAnnotation* curAnn);
	void saveScreenState();
	void restoreScreenState();
	uint processData(const QByteArray& fileChunk);
	void showFileImage();
	virtual void mouseDoubleClickEvent(QMouseEvent*);

	Domain* d;
	Git* git;
	StateInfo* st;
	RangeInfo* rangeInfo;
	FileHighlighter* fileHighlighter;
	QPointer<Annotate> annotateObj;
	QPointer<MyProcess> proc;
	const FileAnnotation* curAnn;
	QByteArray fileRowData;
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
	bool isImageFile;

	struct ScreenState {
		bool isValid, hasSelectedText, isAnnotationAppended;
		int topPara, paraFrom, indexFrom, paraTo, indexTo, annoLen;
	};
	ScreenState ss;

	enum BoolOption { // used as self-documenting boolean parameters
		optFalse,
		optEmitSignal
	};
};

#endif
