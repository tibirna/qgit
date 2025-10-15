/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef MAINIMPL_H
#define MAINIMPL_H

#include <QProcess>
#include <QRegularExpression>
#include <QDir>
#include "exceptionmanager.h"
#include "common.h"
#include "ui_mainview.h"

class QAction;
class QCloseEvent;
class QComboBox;
class QEvent;
class QListWidgetItem;
class QModelIndex;
class QProgressBar;
class QShortcutEvent;
class QTextEdit;

class Domain;
class Git;
class FileHistory;
class FileView;
class RevsView;

class MainImpl : public QMainWindow, public Ui_MainBase {
Q_OBJECT
public:
	MainImpl(const QString& curDir = "", QWidget* parent = 0);
	void updateContextActions(SCRef newRevSha, SCRef newFileName, bool isDir, bool found);
	const QString getRevisionDesc(SCRef sha);
	const QString currentDir() const {return curDir;}

	// not buildable with Qt designer, will be created manually
	QLineEdit* lineEditSHA;
	QLineEdit* lineEditFilter;

	enum ComboSearch {
		CS_SHORT_LOG,
		CS_LOG_MSG,
		CS_AUTHOR,
		CS_SHA1,
		CS_FILE,
		CS_PATCH,
		CS_PATCH_REGEXP
	};

	QComboBox* cmbSearch;

signals:
	void highlightPatch(const QString&, bool);
	void updateRevDesc();
	void closeAllWindows();
	void closeAllTabs();
	void changeFont(const QFont&);
	void typeWriterFontChanged();
	void flagChanged(uint);

private slots:
	void tabWdg_currentChanged(int);
	void newRevsAdded(const FileHistory*, const QVector<ShaString>&);
	void fileNamesLoad(int, int);
	void applyRevisions(const QStringList& shas, const QString& remoteRepo);
	bool applyPatches(const QStringList &files);
	void rebase(const QString& from, const QString& to, const QString& onto);
	void merge(const QStringList& shas, const QString& into);
	void moveRef(const QString& refName, const QString& toSHA);
	void shortCutActivated();

protected:
	virtual bool event(QEvent* e);

protected slots:
	void initWithEventLoopActive();
	void refreshRepo(bool setCurRevAfterLoad = true);
	void listViewLog_doubleClicked(const QModelIndex&);
	void fileList_itemDoubleClicked(QListWidgetItem*);
	void treeView_doubleClicked(QTreeWidgetItem*, int);
	void histListView_doubleClicked(const QModelIndex&);
	void customActionListChanged(const QStringList& list);
	void openRecent_triggered(QAction*);
	void customAction_triggered(QAction*);
	void customAction_exited(const QString& name);
	void goRef_triggered(QAction*);
	void changesCommitted(bool);
	void lineEditSHA_returnPressed();
	void lineEditFilter_returnPressed();
	void tabBar_tabCloseRequested(int index);
	void ActBack_activated();
	void ActForward_activated();
	void ActFind_activated();
	void ActFindNext_activated();
	void ActRangeDlg_activated();
	void ActViewRev_activated();
	void ActViewFile_activated();
	void ActViewFileNewTab_activated();
	void ActViewDiff_activated();
	void ActViewDiffNewTab_activated();
	void ActExternalDiff_activated();
	void ActExternalEditor_activated();
	void ActSplitView_activated();
	void ActToggleLogsDiff_activated();
	void ActShowDescHeader_activated();
	void ActOpenRepo_activated();
	void ActOpenRepoNewWindow_activated();
	void ActRefresh_activated();
	void ActSaveFile_activated();
	void ActMailFormatPatch_activated();
	void ActMailApplyPatch_activated();
	void ActSettings_activated();
	void ActCommit_activated();
	void ActAmend_activated();
	void ActCheckout_activated();
	void ActBranch_activated();
	void ActTag_activated();
	void ActDelete_activated();
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
	void ActMarkDiffToSha_activated();
	void closeEvent(QCloseEvent* ce);

private:
	friend class setRepoDelayed;

	virtual bool eventFilter(QObject* obj, QEvent* ev);
	void updateGlobalActions(bool b);
	void updateRevVariables(SCRef sha);
	void setupShortcuts();
	int currentTabType(Domain** t);
	int tabType(Domain** t, int index);
	void filterList(bool isOn, bool onlyHighlight);
	bool isMatch(SCRef sha, SCRef f, int cn, const QMap<QString,bool>& sm);
	void highlightAbbrevSha(SCRef abbrevSha);
	void setRepository(SCRef wd, bool = false, bool = false, const QStringList* = NULL, bool = false);
	void getExternalDiffArgs(QStringList* args, QStringList* filenames);
	QString copyFileToDiffIfNeeded(QStringList* filenames, QString sha);
	QStringList getExternalEditorArgs();
	void lineEditSHASetText(SCRef text);
	void updateCommitMenu(bool isStGITStack);
	void updateRecentRepoMenu(SCRef newEntry = "");
	void doUpdateRecentRepoMenu(SCRef newEntry);
	void doUpdateCustomActionMenu(const QStringList& list);
	void doBranchOrTag(bool isTag);
	void ActCommit_setEnabled(bool b);
	void doContexPopup(SCRef sha);
	void doFileContexPopup(SCRef fileName, int type);
	void adjustFontSize(int delta);
	void scrollTextEdit(int delta);
	void goMatch(int delta);
	bool askApplyPatchParameters(bool* commit, bool* fold);
	void saveCurrentGeometry();
	QTextEdit* getCurrentTextEdit();
	template<class X> QList<X*>* getTabs(QWidget* tabPage = NULL);
	template<class X> X* firstTab(QWidget* startPage = NULL);
	void openFileTab(FileView* fv = NULL);

	EM_DECLARE(exExiting);

	Git* git;
	RevsView* rv;
	QProgressBar* pbFileNamesLoading;

	// curDir is the repository working directory, could be different from qgit running
	// directory QDir::current(). Note that qgit could be run from subdirectory
	// so only after git->isArchive() that updates curDir to point to working directory
	// we are sure is correct.
	QString curDir;
	QString startUpDir;
	QString startUpFile;
	QString textToFind;
	QRegularExpression shortLogRE;
	QRegularExpression longLogRE;
	QMap<QString, QVariant> revision_variables; // variables used in generic input dialogs
	bool setRepositoryBusy;

};

class ExternalDiffProc : public QProcess {
Q_OBJECT
public:
	ExternalDiffProc(const QStringList& f, QObject* p)
		: QProcess(p), filenames(f) {

		connect(this, SIGNAL(finished(int, QProcess::ExitStatus)),
		        this, SLOT(on_finished(int, QProcess::ExitStatus)));
	}
	~ExternalDiffProc() {

		terminate();
		removeFiles();
	}
	QStringList filenames;

private slots:
	void on_finished(int, QProcess::ExitStatus) { deleteLater(); }

private:
	void removeFiles() {

		if (!filenames.empty()) {
			QDir d; // remove temporary files to diff on
			for (int i = 0; i < filenames.size(); i++)
			{
				d.remove(filenames[i]);
			}
		}
	}
};

class ExternalEditorProc : public QProcess {
Q_OBJECT
public:
	ExternalEditorProc(QObject* p)
	    : QProcess(p) {

		connect(this, SIGNAL(finished(int, QProcess::ExitStatus)),
		        this, SLOT(on_finished(int, QProcess::ExitStatus)));
	}
	~ExternalEditorProc() {
		terminate();
	}

private slots:
	void on_finished(int, QProcess::ExitStatus) { deleteLater(); }

private:
};


#endif
