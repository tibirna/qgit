/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef DOMAIN_H
#define DOMAIN_H

#include <QObject>
#include <QEvent>
#include "exceptionmanager.h"
#include "common.h"

#define UPDATE_DOMAIN(x)       QApplication::postEvent(x, new UpdateDomainEvent(false))
#define UPDATE()               QApplication::postEvent(this, new UpdateDomainEvent(false))
#define UPDATE_DM_MASTER(x, f) QApplication::postEvent(x, new UpdateDomainEvent(true, f))

class Domain;
class FileHistory;
class Git;
class MainImpl;

class UpdateDomainEvent : public QEvent {
public:
	explicit UpdateDomainEvent(bool fromMaster, bool force = false)
	: QEvent(fromMaster ? (QEvent::Type)QGit::UPD_DM_MST_EV
	                    : (QEvent::Type)QGit::UPD_DM_EV), f(force) {}
	bool isForced() const { return f; };
private:
	bool f;
};

class StateInfo {
public:
	StateInfo() { clear(); }
	StateInfo& operator=(const StateInfo& newState);
	bool operator==(const StateInfo& newState) const;
	bool operator!=(const StateInfo& newState) const;
	void clear();
	const QString sha(bool n = true) const { return (n ? curS.sha : prevS.sha); };
	const QString fileName(bool n = true) const { return (n ? curS.fn : prevS.fn); };
	const QString diffToSha(bool n = true) const {return(n ? curS.dtSha : prevS.dtSha); };
	bool selectItem(bool n = true) const { return( n ? curS.sel : prevS.sel); };
	bool isMerge(bool n = true) const { return( n ? curS.isM : prevS.isM); };
	bool allMergeFiles(bool n = true) const { return( n ? curS.allM : prevS.allM); };
	void setSha(const QString& s) { if (isLocked) nextS.sha = s; else curS.sha = s; };
	void setFileName(const QString& s) { if (isLocked) nextS.fn = s; else curS.fn = s; };
	void setDiffToSha(const QString& s) { if (isLocked) nextS.dtSha = s; else curS.dtSha = s; };
	void setSelectItem(bool b) { if (isLocked) nextS.sel = b; else curS.sel = b; };
	void setIsMerge(bool b) { if (isLocked) nextS.isM = b; else curS.isM = b; };
	void setAllMergeFiles(bool b) { if (isLocked) nextS.allM = b; else curS.allM = b; };
	bool isChanged(uint what = ANY) const;

	enum Field {
		SHA             = 1,
		FILE_NAME       = 2,
		DIFF_TO_SHA     = 4,
		ALL_MERGE_FILES = 8,
		ANY             = 15
	};

private:
	friend class Domain;

	bool requestPending() const { return (!nextS.sha.isEmpty() && (nextS != curS)); };
	void setLock(bool b) { isLocked = b; if (b) nextS = curS; };
	void commit() { prevS = curS; };
	void rollBack() {
		if (nextS == curS)
			nextS.clear(); // invalidate to avoid infinite loop
		curS = prevS;
	};
	bool flushQueue() {
		if (requestPending()) {
			curS = nextS;
			return true;
		}
		return false;
	};

	class S {
	public:
		S() { clear(); }
		void clear();
		bool operator==(const S& newState) const;
		bool operator!=(const S& newState) const;

		QString sha;
		QString fn;
		QString dtSha;
		bool sel;
		bool isM;
		bool allM;
	};
	S curS;  // current state, what returns from StateInfo::sha()
	S prevS; // previous good state, used to rollBack in case state update fails
	S nextS; // next queued state, waiting for current update to finish
	bool isLocked;
};

class Domain: public QObject {
Q_OBJECT
public:
	Domain() {}
	Domain(MainImpl* m, Git* git, bool isMain);
	void deleteWhenDone(); // will delete when no more run() are pending
	void setThrowOnDelete(bool b);
	bool isThrowOnDeleteRaised(int excpId, SCRef curContext);
	MainImpl* m() const;
	FileHistory* model() const { return _model; }
	bool isReadyToDrag() const { return readyToDrag; }
	bool setReadyToDrag(bool b);
	bool isDragging() const { return dragging; }
	bool setDragging(bool b);
	bool isDropping() const { return dropping; }
	void setDropping(bool b) { dropping = b; }
	bool isLinked() const { return linked; }
	QWidget* tabPage() { return container; }
	virtual bool isMatch(SCRef) { return false; }

	StateInfo st;

signals:
	void updateRequested(StateInfo newSt);
	void cancelDomainProcesses();

public slots:
	void on_closeAllTabs();

protected slots:
	virtual void on_contextMenu(const QString&, int);
	void on_updateRequested(StateInfo newSt);
	void on_deleteWhenDone();

protected:
	virtual void clear(bool complete = true);
	virtual bool event(QEvent* e);
	virtual bool doUpdate(bool force) = 0;
	void linkDomain(Domain* d);
	void unlinkDomain(Domain* d);

	Git* git;
	QWidget* container;
	bool busy;

private:
	void populateState();
	void update(bool fromMaster, bool force);
	bool flushQueue();
	void sendPopupEvent();

	EM_DECLARE(exDeleteRequest);
	EM_DECLARE(exCancelRequest);

	FileHistory* _model;
	bool readyToDrag;
	bool dragging;
	bool dropping;
	bool linked;
	int popupType;
	QString popupData;
	QString statusBarRequest;
};

#endif
