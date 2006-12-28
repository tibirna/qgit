/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef SETTINGSIMPL_H
#define SETTINGSIMPL_H

#include "ui_settings.h"

class Git;

class SettingsImpl: public QDialog, public Ui_settingsBase {
Q_OBJECT
public:
	SettingsImpl(QWidget* parent, Git* git, int defTab = 0);

protected slots:
	void checkBoxNumbers_toggled(bool b);
	void checkBoxSign_toggled(bool b);
	void checkBoxRangeSelectDialog_toggled(bool b);
	void checkBoxRelativeDate_toggled(bool b);
	void checkBoxDiffCache_toggled(bool b);
	void checkBoxCommitSign_toggled(bool b);
	void checkBoxCommitVerify_toggled(bool b);
	void lineEditExternalDiffViewer_textChanged(const QString& s);
	void lineEditExtraOptions_textChanged(const QString& s);
	void lineEditExcludeFile_textChanged(const QString& s);
	void lineEditExcludePerDir_textChanged(const QString& s);
	void lineEditTemplate_textChanged(const QString& s);
	void lineEditCommitExtraOptions_textChanged(const QString& s);
	void comboBoxCodecs_activated(int i);
	void pushButtonExtDiff_clicked();
	void pushButtonFont_clicked();

private:
	void setupCodecList(QStringList& list);
	void setupCodecsCombo();

	Git* git;
	static const char* en[];
};

#endif
