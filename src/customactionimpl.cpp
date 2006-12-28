/*
	Description: custom action handling

	Author: Marco Costalba (C) 2006

	Copyright: See COPYING file that comes with this distribution

*/
#include <qsettings.h>
#include <q3listview.h>
#include <qinputdialog.h>
#include <qmessagebox.h>
#include <qpushbutton.h>
#include <q3textedit.h>
#include <qcheckbox.h>
#include "common.h"
#include "customactionimpl.h"

using namespace QGit;

CustomActionImpl::CustomActionImpl() : QWidget(0, 0, Qt::WDestructiveClose) {

	setupUi(this);
	listViewNames->setSorting(-1);

	QSettings set;
	actionList = QStringList::split(",", set.readEntry(APP_KEY + MCR_LIST_KEY, ""));
	Q3ListViewItem* lastItem = NULL;
	FOREACH_SL (it, actionList)
		lastItem = new Q3ListViewItem(listViewNames, lastItem, *it);

	Q3ListViewItem* item = listViewNames->currentItem();
	listViewNames_currentChanged(item);
	if (item)
		item->setSelected(true);
}

void CustomActionImpl::loadAction(const QString& name) {

	checkBoxRefreshAfterAction->setChecked(testFlag(MCR_REFRESH_F, name));
	checkBoxAskArgs->setChecked(testFlag(MCR_CMD_LINE_F, name));
	QSettings set;
	textEditAction->setText(set.readEntry(APP_KEY + name + MCR_TEXT_KEY, ""));
}

void CustomActionImpl::removeAction(const QString& name) {

	QSettings set;
	set.removeEntry(APP_KEY + name + FLAGS_KEY);
	set.removeEntry(APP_KEY + name + MCR_TEXT_KEY);
}

void CustomActionImpl::updateActionList() {

	actionList.clear();
	Q3ListViewItemIterator it(listViewNames);
	while (it.current()) {
		actionList.append(it.current()->text(0));
		++it;
	}
	writeSetting(MCR_LIST_KEY, actionList.join(","));
	emit listChanged(actionList);
}

void CustomActionImpl::listViewNames_currentChanged(Q3ListViewItem* item) {

	bool emptyList = (item == NULL);

	if (!emptyList) {
		curAction = "Macro " + item->text(0) + "/";
		loadAction(curAction);
		listViewNames->ensureItemVisible(item);
	} else {
		curAction = "";
		textEditAction->clear();
		if (checkBoxRefreshAfterAction->isChecked())
			checkBoxRefreshAfterAction->toggle();

		if (checkBoxAskArgs->isChecked())
			checkBoxAskArgs->toggle();
	}
	textEditAction->setEnabled(!emptyList);
	checkBoxRefreshAfterAction->setEnabled(!emptyList);
	checkBoxAskArgs->setEnabled(!emptyList);
	pushButtonRename->setEnabled(!emptyList);
	pushButtonRemove->setEnabled(!emptyList);
	pushButtonMoveUp->setEnabled(!emptyList && (item != listViewNames->firstChild()));
	pushButtonMoveDown->setEnabled(!emptyList && (item != listViewNames->lastItem()));
}

bool CustomActionImpl::getNewName(QString& name, const QString& caption) {

	bool ok;
	const QString oldName = name;
	name = QInputDialog::getText(caption + " - QGit", "Enter action name:",
	                             QLineEdit::Normal, name, &ok, this);

	if (!ok || name.isEmpty() || name == oldName)
		return false;

	if (actionList.contains(name)) {
		QMessageBox::warning(this, caption + " - QGit", "Sorry, action name "
		                     "already exists.\nPlease choose a different name.");
		return false;
	}
	return true;
}

void CustomActionImpl::pushButtonNew_clicked() {

	QString name;
	if (!getNewName(name, "Create new action"))
		return;

	Q3ListViewItem* item = new Q3ListViewItem(listViewNames, listViewNames->lastItem(), name);
	listViewNames->setCurrentItem(item);
	listViewNames_currentChanged(item);
	updateActionList();
	textEditAction->setText("<write here your action's commands sequence>");
	textEditAction->selectAll();
	textEditAction->setFocus();
}

void CustomActionImpl::pushButtonRename_clicked() {

	Q3ListViewItem* item = listViewNames->currentItem();
	if (!item || !item->isSelected())
		return;

	QString newName(item->text(0));
	if (!getNewName(newName, "Rename action"))
		return;

	item->setText(0, newName);
	updateActionList();
	const QString oldActionName(curAction);
	listViewNames_currentChanged(listViewNames->currentItem()); // updates curAction
	loadAction(oldActionName);
	removeAction(oldActionName);
}

void CustomActionImpl::pushButtonRemove_clicked() {

	Q3ListViewItem* item = listViewNames->currentItem();
	if (!item || !item->isSelected())
		return;

	removeAction(curAction);
	delete item;
	updateActionList();
	if (listViewNames->currentItem())
		listViewNames->currentItem()->setSelected(true);
}

void CustomActionImpl::pushButtonMoveUp_clicked() {

	Q3ListViewItem* item = listViewNames->currentItem();
	if (!item || item == listViewNames->firstChild())
		return;

	item->itemAbove()->moveItem(item);
	updateActionList();
	listViewNames_currentChanged(item);
}

void CustomActionImpl::pushButtonMoveDown_clicked() {

	Q3ListViewItem* item = listViewNames->currentItem();
	if (!item || item == listViewNames->lastItem())
		return;

	item->moveItem(item->itemBelow());
	updateActionList();
	listViewNames_currentChanged(item);
}

void CustomActionImpl::textEditAction_textChanged() {

	if (!curAction.isEmpty())
		writeSetting(MCR_TEXT_KEY, textEditAction->text(), curAction);
}

void CustomActionImpl::checkBoxRefreshAfterAction_toggled(bool b) {

	if (!curAction.isEmpty())
		setFlag(MCR_REFRESH_F, b, curAction);
}

void CustomActionImpl::checkBoxAskArgs_toggled(bool b) {

	if (!curAction.isEmpty())
		setFlag(MCR_CMD_LINE_F, b, curAction);
}

void CustomActionImpl::pushButtonOk_clicked() {

	close();
}
