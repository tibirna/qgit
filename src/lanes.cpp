/*
	Description: history graph computation

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QStringList>
#include "common.h"
#include "lanes.h"

#define IS_NODE(x) (x == NODE || x == NODE_R || x == NODE_L)

using namespace QGit;

void Lanes::init(const QString& expectedSha) {

	clear();
	activeLane = 0;
	setBoundary(false);
	add(BRANCH, expectedSha, activeLane);
}

void Lanes::clear() {

	typeVec.clear();
	nextShaVec.clear();
}

void Lanes::setBoundary(bool b) {
// changes the state so must be called as first one

	NODE   = b ? BOUNDARY_C : MERGE_FORK;
	NODE_R = b ? BOUNDARY_R : MERGE_FORK_R;
	NODE_L = b ? BOUNDARY_L : MERGE_FORK_L;
	boundary = b;

	if (boundary)
		typeVec[activeLane] = BOUNDARY;
}

bool Lanes::isFork(const QString& sha, bool& isDiscontinuity) {

	int pos = findNextSha(sha, 0);
	isDiscontinuity = (activeLane != pos);
	if (pos == -1) // new branch case
		return false;

	return (findNextSha(sha, pos + 1) != -1);
/*
	int cnt = 0;
	while (pos != -1) {
		cnt++;
		pos = findNextSha(sha, pos + 1);
//		if (isDiscontinuity)
//			isDiscontinuity = (activeLane != pos);
	}
	return (cnt > 1);
*/
}

void Lanes::setFork(const QString& sha) {

	int rangeStart, rangeEnd, idx;
	rangeStart = rangeEnd = idx = findNextSha(sha, 0);

	while (idx != -1) {
		rangeEnd = idx;
		typeVec[idx] = TAIL;
		idx = findNextSha(sha, idx + 1);
	}
	typeVec[activeLane] = NODE;

	int& startT = typeVec[rangeStart];
	int& endT = typeVec[rangeEnd];

	if (startT == NODE)
		startT = NODE_L;

	if (endT == NODE)
		endT = NODE_R;

	if (startT == TAIL)
		startT = TAIL_L;

	if (endT == TAIL)
		endT = TAIL_R;

	for (int i = rangeStart + 1; i < rangeEnd; i++) {

		int& t = typeVec[i];

		if (t == NOT_ACTIVE)
			t = CROSS;

		else if (t == EMPTY)
			t = CROSS_EMPTY;
	}
}

void Lanes::setMerge(const QStringList& parents) {
// setFork() must be called before setMerge()

	if (boundary)
		return; // handle as a simple active line

	int& t = typeVec[activeLane];
	bool wasFork   = (t == NODE);
	bool wasFork_L = (t == NODE_L);
	bool wasFork_R = (t == NODE_R);
	bool startJoinWasACross = false, endJoinWasACross = false;

	t = NODE;

	int rangeStart = activeLane, rangeEnd = activeLane;
	QStringList::const_iterator it(parents.constBegin());
	for (++it; it != parents.constEnd(); ++it) { // skip first parent

		int idx = findNextSha(*it, 0);
		if (idx != -1) {

			if (idx > rangeEnd) {

				rangeEnd = idx;
				endJoinWasACross = typeVec[idx] == CROSS;
			}

			if (idx < rangeStart) {

				rangeStart = idx;
				startJoinWasACross = typeVec[idx] == CROSS;
			}

			typeVec[idx] = JOIN;
		} else
			rangeEnd = add(HEAD, *it, rangeEnd + 1);
	}
	int& startT = typeVec[rangeStart];
	int& endT = typeVec[rangeEnd];

	if (startT == NODE && !wasFork && !wasFork_R)
		startT = NODE_L;

	if (endT == NODE && !wasFork && !wasFork_L)
		endT = NODE_R;

	if (startT == JOIN && !startJoinWasACross)
		startT = JOIN_L;

	if (endT == JOIN && !endJoinWasACross)
		endT = JOIN_R;

	if (startT == HEAD)
		startT = HEAD_L;

	if (endT == HEAD)
		endT = HEAD_R;

	for (int i = rangeStart + 1; i < rangeEnd; i++) {

		int& type = typeVec[i];

		if (type == NOT_ACTIVE)
			type = CROSS;

		else if (type == EMPTY)
			type = CROSS_EMPTY;

		else if (type == TAIL_R || type == TAIL_L)
			type = TAIL;
	}
}

void Lanes::setInitial() {

	int& t = typeVec[activeLane];
	if (!IS_NODE(t) && t != APPLIED)
		t = (boundary ? BOUNDARY : INITIAL);
}

void Lanes::setApplied() {

	// applied patches are not merges, nor forks
	typeVec[activeLane] = APPLIED; // TODO test with boundaries
}

void Lanes::changeActiveLane(const QString& sha) {

	int& t = typeVec[activeLane];
	if (t == INITIAL || isBoundary(t))
		t = EMPTY;
	else
		t = NOT_ACTIVE;

	int idx = findNextSha(sha, 0); // find first sha
	if (idx != -1)
		typeVec[idx] = ACTIVE; // called before setBoundary()
	else
		idx = add(BRANCH, sha, activeLane); // new branch

	activeLane = idx;
}

void Lanes::afterMerge() {

	if (boundary)
		return; // will be reset by changeActiveLane()

	for (int i = 0; i < typeVec.count(); i++) {

		int& t = typeVec[i];

		if (isHead(t) || isJoin(t) || t == CROSS)
			t = NOT_ACTIVE;

		else if (t == CROSS_EMPTY)
			t = EMPTY;

		else if (IS_NODE(t))
			t = ACTIVE;
	}
}

void Lanes::afterFork() {

	for (int i = 0; i < typeVec.count(); i++) {

		int& t = typeVec[i];

		if (t == CROSS)
			t = NOT_ACTIVE;

		else if (isTail(t) || t == CROSS_EMPTY)
			t = EMPTY;

		if (!boundary && IS_NODE(t))
			t = ACTIVE; // boundary will be reset by changeActiveLane()
	}
	while (typeVec.last() == EMPTY) {
		typeVec.pop_back();
		nextShaVec.pop_back();
	}
}

bool Lanes::isBranch() {

	return (typeVec[activeLane] == BRANCH);
}

void Lanes::afterBranch() {

	typeVec[activeLane] = ACTIVE; // TODO test with boundaries
}

void Lanes::afterApplied() {

	typeVec[activeLane] = ACTIVE; // TODO test with boundaries
}

void Lanes::nextParent(const QString& sha) {

	nextShaVec[activeLane] = (boundary ? "" : sha);
}

int Lanes::findNextSha(const QString& next, int pos) {

	for (int i = pos; i < nextShaVec.count(); i++)
		if (nextShaVec[i] == next)
			return i;
	return -1;
}

int Lanes::findType(int type, int pos) {

	for (int i = pos; i < typeVec.count(); i++)
		if (typeVec[i] == type)
			return i;
	return -1;
}

int Lanes::add(int type, const QString& next, int pos) {

	// first check empty lanes starting from pos
	if (pos < static_cast<int>(typeVec.count())) {
		pos = findType(EMPTY, pos);
		if (pos != -1) {
			typeVec[pos] = type;
			nextShaVec[pos] = next;
			return pos;
		}
	}
	// if all lanes are occupied add a new lane
	typeVec.append(type);
	nextShaVec.append(next);
	return typeVec.count() - 1;
}
