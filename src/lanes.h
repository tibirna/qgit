/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef LANES_H
#define LANES_H

#include <QString>
#include <QStringList>
#include <QVector>

class Lanes {
public:
	Lanes() {} // init() will setup us later, when data is available
	bool isEmpty() { return typeVec.empty(); }
	void init(const QString& expectedSha);
	void clear();
	bool isFork(const QString& sha, bool& isDiscontinuity);
	void setBoundary(bool isBoundary);
	void setFork(const QString& sha);
	void setMerge(const QStringList& parents);
	void setInitial();
	void setApplied();
	void changeActiveLane(const QString& sha);
	void afterMerge();
	void afterFork();
	bool isBranch();
	void afterBranch();
	void afterApplied();
	void nextParent(const QString& sha);
	void getLanes(QVector<uint> &ln) { ln = typeVec; } // O(1) vector is implicitly shared

private:
	int findNextSha(const QString& next, int pos);
	int findType(uint type, int pos);
	int add(uint type, const QString& next, int pos);

	int activeLane;
	QVector<uint> typeVec;
	QVector<QString> nextShaVec;
	bool boundary;
	uint NODE, NODE_L, NODE_R;
};

#endif
