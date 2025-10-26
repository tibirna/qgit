/*
	Description: start-up dialog

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSettings>
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <QRegExp>
#define QRegularExpression QRegExp
#else
#include <QRegularExpression>
#endif
#include "common.h"
#include "git.h"
#include "rangeselectimpl.h"

using namespace QGit;

RangeSelectImpl::RangeSelectImpl(QWidget* par, QString* r, bool repoChanged, Git* g)
                : QDialog(par), git(g), range(r) {

	setupUi(this);

	QStringList orl, tmp;
	orderRefs(git->getAllRefNames(Git::BRANCH, !Git::optOnlyLoaded), tmp);
	if (!tmp.isEmpty())
		orl << tmp << "";

	orderRefs(git->getAllRefNames(Git::RMT_BRANCH, !Git::optOnlyLoaded), tmp);
	if (!tmp.isEmpty())
		orl << tmp << "";

	orderRefs(git->getAllRefNames(Git::TAG, !Git::optOnlyLoaded), tmp);
	if (!tmp.isEmpty())
		orl << tmp;

	// as default select first tag that is not also the current HEAD
	int defIdx = orl.count() - tmp.count();
	if (!tmp.empty()) {
		SCRef tagSha(git->getRefSha(tmp.first(), Git::TAG, false));
		if (!tagSha.isEmpty() && git->checkRef(tagSha, Git::CUR_BRANCH))
			// in this case set as default tag the next one if any
			defIdx += (tmp.count() > 1 ? 1 : -1);
	}

	if (!orl.isEmpty() && orl.last().isEmpty())
		orl.pop_back();

	QString from, to, options;

	if (!repoChanged) {
		// range values are sensible only when reloading the same repo
		QSettings settings;
		from = settings.value(RANGE_FROM_KEY).toString();
		to = settings.value(RANGE_TO_KEY).toString();
		options = settings.value(RANGE_OPT_KEY).toString();
	}
	comboBoxTo->insertItem(0, "HEAD");
	comboBoxTo->insertItems(1, orl);
	int idx = repoChanged ? 0 : comboBoxTo->findText(to);
	if (idx != -1)
		comboBoxTo->setCurrentIndex(idx);
	else
		comboBoxTo->setEditText(to);

	comboBoxFrom->insertItems(0, orl);
	idx = repoChanged ? defIdx : comboBoxFrom->findText(from);
	if (idx != -1)
		comboBoxFrom->setCurrentIndex(idx);
	else
		comboBoxFrom->setEditText(from);

	comboBoxFrom->setFocus();

	lineEditOptions->setText(options);

	int f = flags(FLAGS_KEY);
	checkBoxDiffCache->setChecked(f & DIFF_INDEX_F);
	checkBoxShowAll->setChecked(f & ALL_BRANCHES_F);
	checkBoxShowWholeHistory->setChecked(f & WHOLE_HISTORY_F);
	checkBoxShowDialog->setChecked(f & RANGE_SELECT_F);
}

void RangeSelectImpl::orderRefs(const QStringList& src, QStringList& dst) {
// we use an heuristic to list release candidates before corresponding
// releases as example v.2.6.18-rc4 before v.2.6.18

	// match a (dotted) number + something else + a number + EOL
	QRegularExpression re("[\\d\\.]+([^\\d\\.]+\\d+$)");

	// in ASCII the space ' ' (32) comes before '!' (33) and both
	// before the rest, we need this to correctly order a sequence like
	//
	//	[v1.5, v1.5-rc1, v1.5.1] --> [v1.5.1, v1.5, v1.5-rc1]

	const QString rcMark(" $$%%");   // an impossible to find string starting with a space
	const QString noRcMark("!$$%%"); // an impossible to find string starting with a '!'

	typedef QMap<QString, QString> OrderedMap;
	QRegularExpression verRE("([^\\d])(\\d{1,2})(?=[^\\d])");
	OrderedMap map;

	FOREACH_SL (it, src) {

		QString tmpStr(*it);
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
		if (re.indexIn(tmpStr) != -1)
			tmpStr.insert(re.pos(1), rcMark);
#else
		QRegularExpressionMatch match;
		if (tmpStr.indexOf(re, 0, &match) != -1)
			tmpStr.insert(match.capturedStart(1), rcMark);
#endif
		else
			tmpStr += noRcMark;

		// Normalize all numbers to 3 digits with leading zeros, so one-digit
		// version numbers are always smaller than two-digit version numbers
		//     [v1.10.3, v1.5.1, v1.7.2] --> [v.1.10.3, v1.7.2, v1.5.1]
		// QMap automatically sorts by keys, so we only have to iterate over it
		// and return the original strings (stored as the data() in the map)
		while (tmpStr.contains(verRE))
			tmpStr.replace(verRE, "\\10\\2");

		map[tmpStr] = *it;
	}
	dst.clear();
	FOREACH (OrderedMap, it, map)
		dst.prepend(it.value());
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

	lineEditOptions->setText(opt.trimmed());
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
	}
	// all stuff after "--" should go after range
	if (lineEditOptions->text().contains("--")) {
		QString tmp(lineEditOptions->text());
		tmp.insert(tmp.indexOf("--"), *range + " ");
		*range = tmp;
	} else
		range->prepend(lineEditOptions->text() + " ");

	*range = range->trimmed();

	// save settings before leaving
	QSettings settings;
	settings.setValue(RANGE_FROM_KEY, comboBoxFrom->currentText());
	settings.setValue(RANGE_TO_KEY, comboBoxTo->currentText());
	settings.setValue(RANGE_OPT_KEY, lineEditOptions->text());

	done(QDialog::Accepted);
}

QString RangeSelectImpl::getDefaultArgs() {
	QSettings settings;
	return settings.value(RANGE_OPT_KEY).toString();
}
