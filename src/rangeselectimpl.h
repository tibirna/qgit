/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef RANGESELECTIMPL_H
#define RANGESELECTIMPL_H

#include "ui_rangeselect.h"

class Git;

class RangeSelectImpl: public QDialog, public Ui_RangeSelectBase {
Q_OBJECT
public:
	RangeSelectImpl(QWidget* par, QString* range, bool rc, Git* g);
	static QString getDefaultArgs();

public slots:
	void pushButtonOk_clicked();
	void checkBoxDiffCache_toggled(bool b);
	void checkBoxShowAll_toggled(bool b);
	void checkBoxShowDialog_toggled(bool b);
	void checkBoxShowWholeHistory_toggled(bool b);

private:
	void orderRefs(const QStringList& src, QStringList& dst);

	Git* git;
	QString* range;
};

#endif
