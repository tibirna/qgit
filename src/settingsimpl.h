/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef SETTINGSIMPL_H
#define SETTINGSIMPL_H

#include "ui_settings.h"

class QVariant;
class Git;

class SettingsImpl: public QDialog, public Ui_settingsBase {
Q_OBJECT
public:
	SettingsImpl(QWidget* parent, Git* git, int defTab = 0);

signals:
	void typeWriterFontChanged();
	void flagChanged(uint);

protected slots:
	void checkBoxNumbers_toggled(bool b);
	void checkBoxSign_toggled(bool b);
	void checkBoxRangeSelectDialog_toggled(bool b);
	void checkBoxReopenLastRepo_toggled(bool b);
	void checkBoxOpenInEditor_toggled(bool b);
	void checkBoxRelativeDate_toggled(bool b);
	void checkBoxLogDiffTab_toggled(bool b);
	void checkBoxSmartLabels_toggled(bool b);
	void checkBoxMsgOnNewSHA_toggled(bool b);
	void checkBoxEnableDragnDrop_toggled(bool b);
	void checkBoxShowShortRef_toggled(bool b);
	void checkBoxDiffCache_toggled(bool b);
	void checkBoxCommitSign_toggled(bool b);
	void checkBoxCommitVerify_toggled(bool b);
	void checkBoxCommitUseDefMsg_toggled(bool b);
	void lineEditExternalDiffViewer_textChanged(const QString& s);
	void lineEditExternalEditor_textChanged(const QString& s);
	void lineEditApplyPatchExtraOptions_textChanged(const QString& s);
	void lineEditFormatPatchExtraOptions_textChanged(const QString& s);
	void lineEditExcludeFile_textChanged(const QString& s);
	void lineEditExcludePerDir_textChanged(const QString& s);
	void lineEditTemplate_textChanged(const QString& s);
	void lineEditCommitExtraOptions_textChanged(const QString& s);
	void comboBoxCodecs_activated(int i);
	void comboBoxUserSrc_activated(int i);
	void comboBoxGitConfigSource_activated(int i);
	void treeWidgetGitConfig_itemChanged(QTreeWidgetItem*, int);
	void pushButtonExtDiff_clicked();
	void pushButtonExtEditor_clicked();
	void pushButtonFont_clicked();

private:
	void writeSetting(const QString& key, const QVariant& value);
	void addConfigOption(QTreeWidgetItem* parent, QStringList paths, const QString& value);
	void setupCodecList(QStringList& list);
	void setupCodecsCombo();
	void readGitConfig(const QString& source);
	void userInfo();
	void changeFlag(uint f, bool b);

	Git* git;
	QStringList _uInfo;
	bool populatingGitConfig;
};

#endif
