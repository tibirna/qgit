/*
	Description: start-up dialog

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qcombobox.h>
#include <qlineedit.h>
#include <qcheckbox.h>
#include <qsettings.h>
#include <qregexp.h>
#include "common.h"
#include "git.h"
#include "rangeselectimpl.h"

using namespace QGit;

RangeSelectImpl::RangeSelectImpl(QWidget* par, QString* r, const QStringList& tl,
                                 bool* q, Git* g) : QDialog(par, 0, true) {
	setupUi(this);
	range = r;
	quit = q;
	git = g;
	*quit = true; // if user press ESC everything works as expected

	QStringList otl;
	orderTags(tl, otl);

	// check if top tag is current HEAD
	if (!otl.empty()) {
		const QString tagSha(git->getRefSha(otl.first(), Git::TAG, false));
		if (git->checkRef(tagSha, Git::CUR_BRANCH))
			// in this case remove from list to avoid an empty view
			otl.pop_front();
	}
	comboBoxTo->insertStringList(otl);
	comboBoxTo->insertItem("HEAD", 0);
	comboBoxFrom->insertStringList(otl);
	comboBoxFrom->setFocus();

	int f = flags();
	checkBoxDiffCache->setChecked(f & DIFF_INDEX_F);
	checkBoxShowAll->setChecked(f & ALL_BRANCHES_F);
	checkBoxShowWholeHistory->setChecked(f & WHOLE_HISTORY_F);
	checkBoxShowDialog->setChecked(f & RANGE_SELECT_F);
}

void RangeSelectImpl::orderTags(const QStringList& src, QStringList& dst) {
// we use an heuristic to list release candidates before corresponding
// releases as example v.2.6.18-rc4 before v.2.6.18

	// match a (dotted) number + something else + a number + EOL
	QRegExp re("[\\d\\.]+([^\\d\\.]+\\d+$)");

	// in ASCII the space ' ' (32) comes before '!' (33) and both
	// before the rest, we need this to correctly order a sequence like
	//
	//	[v1.5, v1.5-rc1, v1.5.1] --> [v1.5.1, v1.5, v1.5-rc1]

	const QString rcMark(" $$%%");   // an impossible to find string starting with a space
	const QString noRcMark("!$$%%"); // an impossible to find string starting with a '!'

	QStringList tmpLs;
	FOREACH_SL (it, src) {
		const QString& s = *it;
		if (re.search(s) != -1) {
			QString t(s);
			tmpLs.append(t.insert(re.pos(1), rcMark));
		} else
			tmpLs.append(s + noRcMark);
	}
	tmpLs.sort();
	dst.clear();
	FOREACH_SL (it, tmpLs) {
		const QString& s = *it;
		if (s.endsWith(noRcMark))
			dst.prepend(s.left(s.length() - noRcMark.length()));
		else {
			QString t(s);
			t.remove(rcMark);
			dst.prepend(t);
		}
	}
}

void RangeSelectImpl::checkBoxDiffCache_toggled(bool b) {

	setFlag(DIFF_INDEX_F, b);
}

void RangeSelectImpl::checkBoxShowDialog_toggled(bool b) {

	setFlag(RANGE_SELECT_F, b);
}

void RangeSelectImpl::pushButtonOk_clicked() {

	*range = comboBoxFrom->currentText();
	if (!range->isEmpty())
		range->append("..");

	range->append(comboBoxTo->currentText());
	range->prepend(lineEditOptions->text() + " ");
	*range = range->stripWhiteSpace();
	*quit = false;
	close();
}

void RangeSelectImpl::checkBoxShowAll_toggled(bool b) {

	QString opt(lineEditOptions->text());
	opt.remove("--all");
	if (b)
		opt.append(" --all");

	lineEditOptions->setText(opt.stripWhiteSpace());
	setFlag(ALL_BRANCHES_F, b);
}

void RangeSelectImpl::checkBoxShowWholeHistory_toggled(bool b) {

	if (b) {
		fromBckUp = comboBoxFrom->currentText();
		toBckUp = comboBoxTo->currentText();
		comboBoxFrom->setCurrentText("");
		comboBoxTo->setCurrentText("HEAD");

	} else {
		comboBoxFrom->setCurrentText(fromBckUp);
		comboBoxTo->setCurrentText(toBckUp);
	}
	comboBoxFrom->setEnabled(!b);
	comboBoxTo->setEnabled(!b);
	setFlag(WHOLE_HISTORY_F, b);
}
