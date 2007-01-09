/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include "exceptionmanager.h"
#include "mainimpl.h"
#include "git.h"
#include "domain.h"

using namespace QGit;

void StateInfo::S::clear() {

	sha = fn = dtSha = "";
	isM = allM = false;
	sel = true;
}

bool StateInfo::S::operator==(const StateInfo::S& st) const {

	if (&st == this)
		return true;

	return (   sha   == st.sha
	        && fn    == st.fn
	        && dtSha == st.dtSha
	        && sel   == st.sel
	        && isM   == st.isM
	        && allM  == st.allM);
}

bool StateInfo::S::operator!=(const StateInfo::S& st) const {

	return !(StateInfo::S::operator==(st));
}

void StateInfo::clear() {

	nextS.clear();
	curS.clear();
	prevS.clear();
	isLocked = false;
}

StateInfo& StateInfo::operator=(const StateInfo& newState) {

	if (&newState != this) {
		if (isLocked)
			nextS = newState.curS;
		else
			curS = newState.curS; // prevS is mot modified to allow a rollback
	}
	return *this;
}

bool StateInfo::operator==(const StateInfo& newState) const {

	if (&newState == this)
		return true;

	return (curS == newState.curS); // compare is made on curS only
}

bool StateInfo::operator!=(const StateInfo& newState) const {

	return !(StateInfo::operator==(newState));
}

bool StateInfo::isChanged(uint what) const {

	bool ret = false;
	if (what & SHA)
		ret = (sha(true) != sha(false));

	if (!ret && (what & FILE_NAME))
		ret = (fileName(true) != fileName(false));

	if (!ret && (what & DIFF_TO_SHA))
		ret = (diffToSha(true) != diffToSha(false));

	if (!ret && (what & ALL_MERGE_FILES))
		ret = (allMergeFiles(true) != allMergeFiles(false));

	return ret;
}

// ************************* Domain ****************************

Domain::Domain(MainImpl* m, Git* g, bool isMain) : QObject(m), git(g) {

	EM_INIT(exDeleteRequest, "Deleting domain");

	_model = new FileHistory(this, git);
	if (isMain)
		git->setDefaultModel(_model);

	container = NULL;
	st.clear();
	busy = readyToDrag = dragging = dropping = linked = false;
	popupType = 0;
	tabPosition = -1;

	connect(m, SIGNAL(tabClosed(int)), this, SLOT(on_tabClosed(int)));
}

void Domain::clear(bool complete) {

	if (complete)
		st.clear();

	_model->clear();
}

void Domain::on_closeAllTabs() {

	delete this; // must be sync, deleteLater() does not work
}

void Domain::deleteWhenDone() {

	if (!EM_IS_PENDING(exDeleteRequest))
		EM_RAISE(exDeleteRequest);

	emit cancelDomainProcesses();

	on_deleteWhenDone();
}

void Domain::on_deleteWhenDone() {

	if (!EM_IS_PENDING(exDeleteRequest))
		deleteLater();
	else
		QTimer::singleShot(20, this, SLOT(on_deleteWhenDone()));
}

void Domain::on_tabClosed(int tabPos) {

	if (tabPosition > tabPos)
		tabPosition--;
}

void Domain::setThrowOnDelete(bool b) {

	if (b)
		EM_REGISTER(exDeleteRequest);
	else
		EM_REMOVE(exDeleteRequest);
}

bool Domain::isThrowOnDeleteRaised(int excpId, SCRef curContext) {

	return EM_MATCH(excpId, exDeleteRequest, curContext);
}

MainImpl* Domain::m() const {

	return static_cast<MainImpl*>(parent());
}

const QString Domain::dragHostName() const {

	return QString::fromLatin1("@") + m()->curWorkDir();
}

bool Domain::setReadyToDrag(bool b) {

	readyToDrag = (b && !busy && !dragging && !dropping);
	return readyToDrag;
}

bool Domain::setDragging(bool b) {

	bool dragFinished = (!b && dragging);

	dragging = (b && readyToDrag && !dropping);

	if (dragging)
		readyToDrag = false;

	if (dragFinished)
		flushQueue();

	return dragging;
}

void Domain::unlinkDomain(Domain* d) {

	d->linked = false;
	while (d->disconnect(SIGNAL(updateRequested(StateInfo)), this))
		;// a signal is emitted for every connection you make,
		 // so if you duplicate a connection, two signals will be emitted.
}

void Domain::linkDomain(Domain* d) {

	unlinkDomain(d); // be sure only one connection is active
	connect(d, SIGNAL(updateRequested(StateInfo)), this, SLOT(on_updateRequested(StateInfo)));
	d->linked = true;
}

void Domain::on_updateRequested(StateInfo newSt) {

	st = newSt;
	UPDATE();
}

bool Domain::flushQueue() {
// during dragging any state update is queued, so try to flush pending now

	if (!busy && st.flushQueue()) {
		UPDATE();
		return true;
	}
	return false;
}

bool Domain::event(QEvent* e) {

	bool fromMaster = false;

	switch (e->type()) {
	case UPD_DM_MST_EV:
		fromMaster = true;
		// fall through
	case UPD_DM_EV:
		update(fromMaster, ((UpdateDomainEvent*)e)->isForced());
		break;
	case MSG_EV:
		if (!busy && !st.requestPending())
			QApplication::postEvent(m(), new MessageEvent(((MessageEvent*)e)->myData()));
		else // waiting for the end of updating
			statusBarRequest = ((MessageEvent*)e)->myData();
		break;
	default:
		break;
	}
	return QObject::event(e);
}

void Domain::populateState() {

	const Rev* r = git->revLookup(st.sha());
	if (r)
		st.setIsMerge(r->parentsCount() > 1);
}

void Domain::update(bool fromMaster, bool force) {

	if (busy)
		// quick exit current (obsoleted) update
		emit cancelDomainProcesses();

	if (busy || dragging)
		return;

	if (linked && !fromMaster) {
		// in this case let the update to fall down from master domain
		StateInfo tmp(st);
		st.rollBack(); // we don't want to filter out next update sent from master
		emit updateRequested(tmp);
		return;
	}
	try {
		setThrowOnDelete(true);
		git->setThrowOnStop(true);
		git->setCurContext(this);
		busy = true;
		setReadyToDrag(false); // do not start any drag while updating
		populateState(); // complete any missing state information
		st.setLock(true); // any state change will be queued now

		if (doUpdate(force))
			st.commit();
		else
			st.rollBack();

		st.setLock(false);
		busy = false;
		if (git->curContext() != this)
			qDebug("ASSERT in Domain::update, context is %p "
			       "instead of %p", (void*)git->curContext(), (void*)this);

		git->setCurContext(NULL);
		git->setThrowOnStop(false);
		setThrowOnDelete(false);

	} catch(int i) {

		st.rollBack();
		st.setLock(false);
		busy = false;
		git->setCurContext(NULL);
		git->setThrowOnStop(false);
		setThrowOnDelete(false);

		if (QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		QString context("updating ");
		if (git->isThrowOnStopRaised(i,  context + className())) {
			EM_THROW_PENDING;
			return;
		}
		if (isThrowOnDeleteRaised(i,  context + className())) {
			EM_THROW_PENDING;
			return;
		}
		const QString info("Exception \'" + EM_DESC(i) + "\' "
		                   "not handled in init...re-throw");
		dbs(info);
		throw;
	}
	bool nextRequestPending = flushQueue();

	if (!nextRequestPending && !statusBarRequest.isEmpty()) {
		// update status bar when we are sure no more work is pending
		QApplication::postEvent(m(), new MessageEvent(statusBarRequest));
		statusBarRequest = "";
	}
	if (!nextRequestPending && popupType)
		sendPopupEvent();
}

void Domain::sendPopupEvent() {

	// call an async context popup, must be executed
	// after returning to event loop
	DeferredPopupEvent* e = new DeferredPopupEvent(popupData, popupType);
	QApplication::postEvent(m(), e);
	popupType = 0;
}

void Domain::on_contextMenu(const QString& data, int type) {

	popupType = type;
	popupData = data;

	if (busy)
		return; // we are in the middle of an update

	// if list view is already updated pop-up
	// context menu, otherwise it means update()
	// has still not been called, a pop-up request,
	// will be fired up at the end of next update()
	if ((type == POPUP_LIST_EV) && (data != st.sha()))
		return;

	sendPopupEvent();
}
