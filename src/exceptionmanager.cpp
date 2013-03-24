/*
	Description: Support for recursive C++ exception handling
			compatible with Qt qApp->processEvents()

			See exception_manager.txt for details.

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QList>
#include "exceptionmanager.h"

ExceptionManager::ExceptionManager() {

	excpId = regionId = currentRegionId = 1;
	descriptions.append("we start from 1");
}

void ExceptionManager::init(int* excp, const QString& desc) {

	*excp = excpId++;
	descriptions.append(desc);
}

const QString ExceptionManager::desc(int excpId) {

	return descriptions[excpId];
}

bool ExceptionManager::isMatch(int value, int excp, const QString& context) {

	bool match = (value == excp);
	if (match) {
		QString info("Caught exception \'" + descriptions[excp] +
		             "\' while in " + context);
		qDebug("%s", info.toLatin1().constData());
	}
	return match;
}

void ExceptionManager::add(int excpId, bool verbose) {
// add a new exception in currentThrowableSet

	// are prepended so to use a for loop starting
	// from begin to find the latest. Exceptions are
	// always added/removed from both totalThrowableSet
	// and regionThrowableSet
	totalThrowableSet.prepend(Exception(excpId, verbose));
	regionThrowableSet.prepend(Exception(excpId, verbose));
}

void ExceptionManager::remove(int excpId) {
// removes ONE exception in totalThrowableSet and ONE in regionThrowableSet.
// if add and remove calls are correctly nested the removed
// excp should be the first in both throwable sets

	if (totalThrowableSet.isEmpty() || regionThrowableSet.isEmpty()) {
		qDebug("ASSERT in remove: removing %i from an empty set", excpId);
		return;
	}
	// remove from region.
	SetIt itReg(regionThrowableSet.begin());
	if ((*itReg).excpId != excpId) {
		qDebug("ASSERT in remove: %i is not the first in list", excpId);
		return;
	}
	regionThrowableSet.erase(itReg);

	// remove from total.
	SetIt itTot(totalThrowableSet.begin());
	if ((*itTot).excpId != excpId) {
		qDebug("ASSERT in remove: %i is not the first in list", excpId);
		return;
	}
	totalThrowableSet.erase(itTot);
}

ExceptionManager::SetIt ExceptionManager::findExcp(ThrowableSet& ts,
		const SetIt& startIt, int excpId) {

	SetIt it(startIt);
	for ( ; it != ts.end(); ++it)
		if ((*it).excpId == excpId)
			break;
	return it;
}

void ExceptionManager::setRaisedFlag(ThrowableSet& ts, int excpId) {

	SetIt it(findExcp(ts, ts.begin(), excpId));
	while (it != ts.end()) {
		(*it).isRaised = true;
		it = findExcp(ts, ++it, excpId);
	}
}

void ExceptionManager::raise(int excpId) {

	if (totalThrowableSet.isEmpty())
		return;

	// check totalThrowableSet to find if excpId is throwable
	SetIt it = findExcp(totalThrowableSet, totalThrowableSet.begin(), excpId);
	if (it == totalThrowableSet.end())
		return;

	// we have found an exception. Set raised flag in regionThrowableSet
	setRaisedFlag(regionThrowableSet, excpId);

	// then set the flag in all regions throwableSetList
	QMap<int, ThrowableSet>::iterator itList(throwableSetMap.begin());
	while (itList != throwableSetMap.end()) {
		setRaisedFlag(*itList, excpId);
		++itList;
	}
}

int ExceptionManager::saveThrowableSet() {
// here we save regionThrowableSet _and_ update the region.
// regionThrowableSet is saved with the current region index.
// then current region is changed to a new and never used index

	int oldCurrentRegionId = currentRegionId;
	throwableSetMap.insert(currentRegionId, regionThrowableSet);
	currentRegionId = ++regionId;

	// we use this call to trigger a region boundary crossing
	// so we have to clear the new region throwables. We still
	// have totalThrowableSet to catch any request.
	regionThrowableSet.clear();

	return oldCurrentRegionId;
}

void ExceptionManager::restoreThrowableSet(int regionId) {

	if (!throwableSetMap.contains(regionId)) {
		qDebug("ASSERT in restoreThrowableSet: region %i not found", regionId);
		return;
	}
	regionThrowableSet = throwableSetMap[regionId];
	throwableSetMap.remove(regionId);
}

bool ExceptionManager::isPending(int excpId) {

	// check in ALL regions if an exception request is pending
	QMap<int, ThrowableSet>::const_iterator itList(throwableSetMap.constBegin());
	while (itList != throwableSetMap.constEnd()) {

		ThrowableSet::const_iterator it((*itList).constBegin());
		for ( ; it != (*itList).constEnd(); ++it)
			if ((*it).isRaised && (*it).excpId == excpId)
				return true;
		++itList;
	}
	return false;
}

void ExceptionManager::throwPending() {

	if (regionThrowableSet.isEmpty())
		return;

	ThrowableSet::const_iterator it(regionThrowableSet.constBegin());
	for ( ; it != regionThrowableSet.constEnd(); ++it)
		if ((*it).isRaised)
			break;

	if (it == regionThrowableSet.constEnd())
		return;

	int excpToThrow = (*it).excpId;
	if ((*it).verbose)
		qDebug("Thrown exception \'%s\'", desc(excpToThrow).toLatin1().constData());

	throw excpToThrow;
}
