/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef FILEVIEW_H
#define FILEVIEW_H

#include "ui_fileview.h" // needed by moc_* file to understand tab() function
#include "common.h"
#include "domain.h"

class MainImpl;
class Git;
class FileHistory;

class FileView: public Domain {
Q_OBJECT
public:
	FileView() {}
	FileView(MainImpl* m, Git* git);
	~FileView();
	virtual void clear(bool complete = true);
	void append(SCRef data);
	void historyReady();
	void updateHistViewer(SCRef revSha, SCRef fileName, bool fromUpstream = true);
	void eof();
	Ui_TabFile* tab() { return fileTab; }

public slots:
	void on_toolButtonCopy_clicked();
	void on_toolButtonShowAnnotate_toggled(bool);
	void on_toolButtonFindAnnotate_toggled(bool);
	void on_toolButtonGoNext_clicked();
	void on_toolButtonGoPrev_clicked();
	void on_toolButtonRangeFilter_toggled(bool);
	void on_toolButtonPin_toggled(bool);
	void on_toolButtonHighlightText_toggled(bool);
	void on_spinBoxRevision_valueChanged(int);
	void on_loadCompleted(const FileHistory*, const QString&);
	void on_annotationAvailable(bool);
	void on_fileAvailable(bool);
	void on_revIdSelected(int);

protected:
	virtual bool doUpdate(bool force);
	virtual bool isMatch(SCRef sha);
	virtual bool eventFilter(QObject *obj, QEvent *e);

private:
	friend class MainImpl;
	friend class FileHighlighter;

	void showAnnotation();
	bool goToCurrentAnnotation(int direction = 0);
	void filterOnRange(bool b);
	void updateSpinBoxValue();
	void updateEnabledButtons();

	Ui_TabFile* fileTab;
};

#endif
