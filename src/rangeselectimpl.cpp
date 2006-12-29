/*
	Description: start-up dialog

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSettings>
#include <QRegExp>
#include "common.h"
#include "git.h"
#include "rangeselectimpl.h"

using namespace QGit;

RangeSelectImpl::RangeSelectImpl(QWidget* par, QString* r, const QStringList& tl,
                                 Git* g) : QDialog(par), git(g), range(r) {
	setupUi(this);
	QStringList otl;
	orderTags(tl, otl);

	// check if top tag is current HEAD
	if (!otl.empty()) {
		SCRef tagSha(git->getRefSha(otl.first(), Git::TAG, false));
		if (git->checkRef(tagSha, Git::CUR_BRANCH))
			// in this case remove from list to avoid an empty view
			otl.pop_front();
	}
	comboBoxTo->insertItem(0, "HEAD");
	comboBoxTo->insertItems(1, otl);
	comboBoxFrom->insertItems(0, otl);
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
		if (re.indexIn(s) != -1) {
			QString t(s);
			tmpLs.append(t.insert(re.pos(1), rcMark));
		} else
			tmpLs.append(s + noRcMark);
	}
	tmpLs.sort();
	dst.clear();
	FOREACH_SL (it, tmpLs) {

		QString t(*it);
		if (t.endsWith(noRcMark))
			t.chop(noRcMark.length());
		else
			t.remove(rcMark);

		dst.prepend(t);
	}
}

void RangeSelectImpl::checkBoxDiffCache_toggled(bool b) {

	setFlag(DIFF_INDEX_F, b);
}

void RangeSelectImpl::checkBoxShowDialog_toggled(bool b) {

	setFlag(RANGE_SELECT_F, b);
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

	comboBoxFrom->setEnabled(!b);
	comboBoxTo->setEnabled(!b);
	setFlag(WHOLE_HISTORY_F, b);
}

void RangeSelectImpl::pushButtonOk_clicked() {

	if (testFlag(WHOLE_HISTORY_F))
		*range = "HEAD";
	else {
		*range = comboBoxFrom->currentText();
		if (!range->isEmpty())
			range->append("..");

		range->append(comboBoxTo->currentText());
		range->prepend(lineEditOptions->text() + " ");
		*range = range->stripWhiteSpace();
	}
	done(QDialog::Accepted);
}
