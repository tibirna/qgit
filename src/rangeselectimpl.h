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
	RangeSelectImpl(QWidget* par, QString* range, const QStringList& tl, Git* g);

public slots:
	void pushButtonOk_clicked();
	void checkBoxDiffCache_toggled(bool b);
	void checkBoxShowAll_toggled(bool b);
	void checkBoxShowDialog_toggled(bool b);
	void checkBoxShowWholeHistory_toggled(bool b);

private:
	void orderTags(const QStringList& src, QStringList& dst);

	Git* git;
	QString* range;
};

#endif
