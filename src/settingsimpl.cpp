/*
	Description: settings dialog

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <QTextCodec>
#include <QFileDialog>
#include <QFontDialog>
#include <QSettings>
#include "common.h"
#include "git.h"
#include "settingsimpl.h"

/*
By default, there are two entries in the search path:

   1. SYSCONF - where SYSCONF is a directory specified when configuring Qt;
			by default it is INSTALL/etc/settings.
   2. $HOME/.qt/ - where $HOME is the user's home directory.
*/

const char* SettingsImpl::en[] = { "Latin1", "Big5 -- Chinese", "EUC-JP -- Japanese",
	"EUC-KR -- Korean", "GB18030 -- Chinese", "ISO-2022-JP -- Japanese",
	"Shift_JIS -- Japanese", "UTF-8 -- Unicode, 8-bit",
	"KOI8-R -- Russian", "KOI8-U -- Ukrainian", "ISO-8859-1 -- Western",
	"ISO-8859-2 -- Central European", "ISO-8859-3 -- Central European",
	"ISO-8859-4 -- Baltic", "ISO-8859-5 -- Cyrillic", "ISO-8859-6 -- Arabic",
	"ISO-8859-7 -- Greek", "ISO-8859-8 -- Hebrew, visually ordered",
	"ISO-8859-8-i -- Hebrew, logically ordered", "ISO-8859-9 -- Turkish",
	"ISO-8859-10", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15 -- Western",
	"windows-1250 -- Central European", "windows-1251 -- Cyrillic",
	"windows-1252 -- Western", "windows-1253 -- Greek", "windows-1254 -- Turkish",
	"windows-1255 -- Hebrew", "windows-1256 -- Arabic", "windows-1257 -- Baltic",
	"windows-1258", 0 };

using namespace QGit;

SettingsImpl::SettingsImpl(QWidget* p, Git* g, int defTab) : QDialog(p), git(g) {

	setupUi(this);
	int f = flags(FLAGS_KEY);
	checkBoxDiffCache->setChecked(f & DIFF_INDEX_F);
	checkBoxNumbers->setChecked(f & NUMBERS_F);
	checkBoxSign->setChecked(f & SIGN_PATCH_F);
	checkBoxCommitSign->setChecked(f & SIGN_CMT_F);
	checkBoxCommitVerify->setChecked(f & VERIFY_CMT_F);
	checkBoxRangeSelectDialog->setChecked(f & RANGE_SELECT_F);
	checkBoxRelativeDate->setChecked(f & REL_DATE_F);

	QSettings set;
	SCRef FPArgs(set.value(PATCH_ARGS_KEY).toString());
	SCRef extDiff(set.value(EXT_DIFF_KEY, EXT_DIFF_DEF).toString());
	SCRef exFile(set.value(EX_KEY, EX_DEF).toString());
	SCRef exPDir(set.value(EX_PER_DIR_KEY, EX_PER_DIR_DEF).toString());
	SCRef tmplt(set.value(CMT_TEMPL_KEY, CMT_TEMPL_DEF).toString());
	SCRef CMArgs(set.value(CMT_ARGS_KEY).toString());

	lineEditExtraOptions->setText(FPArgs);
	lineEditExternalDiffViewer->setText(extDiff);
	lineEditExcludeFile->setText(exFile);
	lineEditExcludePerDir->setText(exPDir);
	lineEditTemplate->setText(tmplt);
	lineEditCommitExtraOptions->setText(CMArgs);
	lineEditTypeWriterFont->setText(TYPE_WRITER_FONT.toString());
	lineEditTypeWriterFont->setCursorPosition(0); // font description could be long

	setupCodecsCombo();
	checkBoxDiffCache_toggled(checkBoxDiffCache->isChecked());
	tabDialog->setCurrentIndex(defTab);
	userInfo();
}

void SettingsImpl::userInfo() {
/*
	QGit::userInfo() returns a QStringList formed by
	triples (defined in, user, email)
*/
	git->userInfo(_uInfo);
	if (_uInfo.count() % 3 != 0) {
		dbs("ASSERT in SettingsImpl::userInfo(), bad info returned");
		return;
	}
	bool found = false;
	int idx = 0;
	FOREACH_SL(it, _uInfo) {
		comboBoxUserSrc->addItem(*it);
		++it;
		if (!found && !(*it).isEmpty())
			found = true;
		if (!found)
			idx++;
		++it;
	}
	if (!found)
		idx = 0;

	comboBoxUserSrc->setCurrentIndex(idx);
	comboBoxUserSrc_activated(idx);
}

void SettingsImpl::comboBoxUserSrc_activated(int i) {

	lineEditAuthor->setText(_uInfo[i * 3 + 1]);
	lineEditMail->setText(_uInfo[i * 3 + 2]);
}

void SettingsImpl::writeSetting(const QString& key, const QVariant& value) {

	QSettings settings;
	settings.setValue(key, value);
}

void SettingsImpl::setupCodecList(QStringList& list) {

	int i = 0;
	while (en[i] != 0)
		list.append(QString::fromLatin1(en[i++]));
}

void SettingsImpl::setupCodecsCombo() {

	const QString localCodec(QTextCodec::codecForLocale()->name());
	QStringList codecs;
	codecs.append(QString("Local Codec (" + localCodec + ")"));
	setupCodecList(codecs);
	comboBoxCodecs->insertItems(0, codecs);

	bool isGitArchive;
	QTextCodec* tc = git->getTextCodec(&isGitArchive);
	if (!isGitArchive) {
		comboBoxCodecs->setEnabled(false);
		return;
	}
	const QString curCodec(tc != 0 ? tc->name() : "Latin1");
	QRegExp re("*" + curCodec + "*", Qt::CaseInsensitive, QRegExp::Wildcard);
	int idx = codecs.indexOf(re);
	if (idx == -1) {
		dbp("ASSERT: codec <%1> not available, using local codec", curCodec);
		idx = 0;
	}
	comboBoxCodecs->setCurrentIndex(idx);
	if (idx == 0) // signal activated() will not fire in this case
		comboBoxCodecs_activated(0);
}

void SettingsImpl::comboBoxCodecs_activated(int idx) {

	QString codecName(QTextCodec::codecForLocale()->name());
	if (idx != 0)
		codecName = comboBoxCodecs->currentText().section(" --", 0, 0);

	git->setTextCodec(QTextCodec::codecForName(codecName.toLatin1()));
}

void SettingsImpl::pushButtonExtDiff_clicked() {

	QString extDiffName(QFileDialog::getOpenFileName(this,
	                    "Select the patch viewer"));
	if (!extDiffName.isEmpty())
		lineEditExternalDiffViewer->setText(extDiffName);
}

void SettingsImpl::pushButtonFont_clicked() {

	bool ok;
	QFont fnt = QFontDialog::getFont(&ok, TYPE_WRITER_FONT, this);
	if (ok) {
		TYPE_WRITER_FONT = fnt;
		lineEditTypeWriterFont->setText(fnt.toString());
		lineEditTypeWriterFont->setCursorPosition(0);
		writeSetting(TYPWRT_FNT_KEY, fnt.toString());
	}
}

void SettingsImpl::checkBoxDiffCache_toggled(bool b) {

	lineEditExcludeFile->setEnabled(b);
	lineEditExcludePerDir->setEnabled(b);
	setFlag(DIFF_INDEX_F, b);
}

void SettingsImpl::checkBoxNumbers_toggled(bool b) {

	setFlag(NUMBERS_F, b);
}

void SettingsImpl::checkBoxSign_toggled(bool b) {

	setFlag(SIGN_PATCH_F, b);
}

void SettingsImpl::checkBoxRangeSelectDialog_toggled(bool b) {

	setFlag(RANGE_SELECT_F, b);
}

void SettingsImpl::checkBoxRelativeDate_toggled(bool b) {

	setFlag(REL_DATE_F, b);
}

void SettingsImpl::checkBoxCommitSign_toggled(bool b) {

	setFlag(SIGN_CMT_F, b);
}

void SettingsImpl::checkBoxCommitVerify_toggled(bool b) {

	setFlag(VERIFY_CMT_F, b);
}

void SettingsImpl::lineEditExternalDiffViewer_textChanged(const QString& s) {

	writeSetting(EXT_DIFF_KEY, s);
}

void SettingsImpl::lineEditExtraOptions_textChanged(const QString& s) {

	writeSetting(PATCH_ARGS_KEY, s);
}

void SettingsImpl::lineEditExcludeFile_textChanged(const QString& s) {

	writeSetting(EX_KEY, s);
}

void SettingsImpl::lineEditExcludePerDir_textChanged(const QString& s) {

	writeSetting(EX_PER_DIR_KEY, s);
}

void SettingsImpl::lineEditTemplate_textChanged(const QString& s) {

	writeSetting(CMT_TEMPL_KEY, s);
}

void SettingsImpl::lineEditCommitExtraOptions_textChanged(const QString& s) {

	writeSetting(CMT_ARGS_KEY, s);
}
