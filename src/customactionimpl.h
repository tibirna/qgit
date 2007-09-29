/*
	Description: custom action handling

	Author: Marco Costalba (C) 2006-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef CUSTOMACTIONIMPL_H
#define CUSTOMACTIONIMPL_H

#include "ui_customaction.h"

class CustomActionImpl : public QWidget, public Ui_CustomActionBase {
Q_OBJECT
public:
	CustomActionImpl();

signals:
	void listChanged(const QStringList&);

protected slots:
	virtual void closeEvent(QCloseEvent*);
	void listWidgetNames_currentItemChanged(QListWidgetItem*, QListWidgetItem*);
	void pushButtonNew_clicked();
	void pushButtonRename_clicked();
	void pushButtonRemove_clicked();
	void pushButtonMoveUp_clicked();
	void pushButtonMoveDown_clicked();
	void checkBoxRefreshAfterAction_toggled(bool);
	void checkBoxAskArgs_toggled(bool);
	void textEditAction_textChanged();
	void pushButtonOk_clicked();

private:
	const QStringList actions();
	void updateActions();
	bool getNewName(QString& name, const QString& caption);
	void loadAction(const QString& name);
	void removeAction(const QString& name);
};

#endif
