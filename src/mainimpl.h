/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef MAINIMPL_H
#define MAINIMPL_H

#include <QSet>
#include <qpointer.h>
#include <qregexp.h>
#include <q3process.h>
#include <qtimer.h>
#include <QCloseEvent>
#include <Q3PopupMenu>
#include <QEvent>
#include "exceptionmanager.h"
#include "common.h"
#include "ui_mainview.h"

class QListWidgetItem;
class Q3ListViewItem;
class Q3ListBoxItem;
class Q3PopupMenu;
class Q3Accel;
class QTextEdit;
class Git;
class Domain;
class ListViewItem;
class RevsView;
class FileView;
class FileHistory;
class MVC;

class QComboBox;

class MainImpl : public QMainWindow, public Ui_MainBase {
Q_OBJECT
public:
	MainImpl(const QString& curDir = "", QWidget* parent = 0);
	QString curWorkDir() const { return curDir; }

signals:
	void highlightedRowsChanged(const QSet<int>&);
	void highlightPatch(const QString&, bool);
	void updateRevDesc();
	void closeAllWindows();
	void closeAllTabs();
	void tabClosed(int tabPos);
	void repaintListViews(const QFont&);
	void closeTabButtonEnabled(bool);

public slots:
	void tabWdg_currentChanged(QWidget*);
	void newRevsAdded(const FileHistory*, const QVector<QString>&);

protected:
	virtual bool event(QEvent* e);

protected slots:
	void initWithEventLoopActive();
	void refreshRepo(bool setCurRevAfterLoad = true);
	void listViewLog_doubleClicked(Q3ListViewItem*);
	void fileList_itemDoubleClicked(QListWidgetItem*);
	void treeView_doubleClicked(Q3ListViewItem*);
	void histListView_doubleClicked(Q3ListViewItem* item);
	void customActionListChanged(const QStringList& list);
	void openRecent_activated(int id);
	void customAction_activated(int id);
	void customAction_exited(const QString& name);
	void goRef_activated(int id);
	void changesCommitted(bool);
	void lineEditSHA_returnPressed();
	void lineEditFilter_returnPressed();
	void pushButtonCloseTab_clicked();
	void accelActivated(int);
	void ActBack_activated();
	void ActForward_activated();
	void ActFind_activated();
	void ActFindNext_activated();
	void ActViewRev_activated();
	void ActViewFile_activated();
	void ActViewFileNewTab_activated();
	void ActViewDiff_activated();
	void ActViewDiffNewTab_activated();
	void ActExternalDiff_activated();
	void ActSplitView_activated();
	void ActOpenRepo_activated();
	void ActOpenRepoNewWindow_activated();
	void ActRefresh_activated();
	void ActSaveFile_activated();
	void ActMailFormatPatch_activated();
	void ActMailApplyPatch_activated();
	void ActSettings_activated();
	void ActCommit_activated();
	void ActTag_activated();
	void ActTagDelete_activated();
	void ActPush_activated();
	void ActPop_activated();
	void ActClose_activated();
	void ActExit_activated();
	void ActSearchAndFilter_toggled(bool);
	void ActSearchAndHighlight_toggled(bool);
	void ActCustomActionSetup_activated();
	void ActCheckWorkDir_toggled(bool);
	void ActShowTree_toggled(bool);
	void ActFilterTree_toggled(bool);
	void ActAbout_activated();
	void ActHelp_activated();
	void closeEvent(QCloseEvent* ce);

private:
	friend class FileView;
	friend class RevsView;
	friend class PatchView;
	friend class setRepoDelayed;

	virtual bool eventFilter(QObject* obj, QEvent* ev);
	void updateGlobalActions(bool b);
	void updateContextActions(SCRef newRevSha, SCRef newFileName, bool isDir, bool found);
	void setupAccelerator(Q3Accel* accel);
	int currentTabType(Domain** t);
	void filterList(bool isOn, bool onlyHighlight);
	bool passFilter(ListViewItem* i, SCRef f, int cn, const QMap<QString,bool>& sm);
	void setRepository(SCRef wd, bool r, bool ks, QStringList* fl = NULL);
	void diffViewerUpdate(Q3ListViewItem* item, bool newDiff);
	void getExternalDiffArgs(QStringList* args);
	void lineEditSHASetText(SCRef text);
	void updateCommitMenu(bool isStGITStack);
	void updateRecentRepoMenu(SCRef newEntry = "");
	void doUpdateRecentRepoMenu(SCRef newEntry);
	void doUpdateCustomActionMenu(const QStringList& list);
	void ActCommit_setEnabled(bool b);
	void doContexPopup(SCRef sha);
	void doFileContexPopup(SCRef fileName, int type);
	void adjustFontSize(int delta);
	void scrollTextEdit(int delta);
	void scrollListView(int delta);
	void goMatch(int delta);
	bool askApplyPatchParameters(bool* commit, bool* fold);
	QTextEdit* getCurrentTextEdit();
	template<class X> QList<X*>* getTabs(int tabPos = -1);
	template<class X> X* firstTab(int startPos = -1);
	void openFileTab(FileView* fv = NULL);

	EM_DECLARE(exExiting);

	Git* git;
	RevsView* rv;
	MVC* mvc;

	// curDir is the repository working dir, could be different from qgit running
	// directory QDir::current(). Note that qgit could be run from subdirectory
	// so only after git->isArchive() that updates curDir to point to working dir
	// we are sure is correct.
	QString curDir;
	QRegExp shortLogRE;
	QRegExp longLogRE;
	QPointer<Q3PopupMenu> contextMenu;
	QPointer<Q3PopupMenu> contextSubMenu;
	QString startUpDir;
	QString textToFind;
	QFont listViewFont;
	bool setRepositoryBusy;
	int recentRepoMenuPos;

	QLineEdit* lineEditSHA;
	QLineEdit* lineEditFilter;
	QComboBox* cmbSearch;
};

class ExternalDiffProc : public Q3Process {
Q_OBJECT
public:
	ExternalDiffProc(const QStringList& args, QObject* p) : Q3Process(args, p) {

		connect(this, SIGNAL(processExited()), this, SLOT(on_processExited()));
	}
	~ExternalDiffProc() {

		tryTerminate();
		removeFiles();
	}
private slots:
	void on_processExited() { deleteLater(); }

private:
	void removeFiles() {

		if (!arguments().empty()) {
			QDir d; // remove temporary files to diff on
			d.remove(arguments()[1]);
			d.remove(arguments()[2]);
		}
	}
};

class setRepoDelayed : public QObject {
Q_OBJECT
public:
	setRepoDelayed(MainImpl* mi, SCRef nd, bool r, bool ks, QStringList* fl) :
	QObject(mi), m(mi), newDir(nd), refresh(r), keepSelection(ks), filterList(fl) {

		QTimer::singleShot(100, this, SLOT(on_timeout()));
	}
private slots:
	void on_timeout() {

		m->setRepository(newDir, refresh, keepSelection, filterList);
		deleteLater();
	 }
private:
	MainImpl* m;
	const QString newDir;
	bool refresh, keepSelection;
	QStringList* filterList;
};

#endif
