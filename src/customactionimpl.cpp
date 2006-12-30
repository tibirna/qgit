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
	actionList = set.value(ACT_LIST_KEY).toStringList();
	Q3ListViewItem* lastItem = NULL;
	FOREACH_SL (it, actionList)
		lastItem = new Q3ListViewItem(listViewNames, lastItem, *it);

	Q3ListViewItem* item = listViewNames->currentItem();
	listViewNames_currentChanged(item);
	if (item)
		item->setSelected(true);
}

void CustomActionImpl::loadAction(const QString& name) {

	const QString flags(ACT_GROUP_KEY + name + ACT_FLAGS_KEY);
	checkBoxRefreshAfterAction->setChecked(testFlag(ACT_REFRESH_F, flags));
	checkBoxAskArgs->setChecked(testFlag(ACT_CMD_LINE_F, flags));
	QSettings set;
	textEditAction->setText(set.value(ACT_GROUP_KEY + name + ACT_TEXT_KEY, "").toString());
}

void CustomActionImpl::removeAction(const QString& name) {

	QSettings set;
	set.remove(ACT_GROUP_KEY + name);
}

void CustomActionImpl::updateActionList() {

	actionList.clear();
	Q3ListViewItemIterator it(listViewNames);
	while (it.current()) {
		actionList.append(it.current()->text(0));
		++it;
	}
	QSettings settings;
	settings.setValue(ACT_LIST_KEY, actionList);
	emit listChanged(actionList);
}

void CustomActionImpl::listViewNames_currentChanged(Q3ListViewItem* item) {

	bool emptyList = (item == NULL);

	if (!emptyList) {
		curAction = item->text(0);
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
	Q3ListViewItem* prevItem = item->itemAbove();
	delete item;
	updateActionList();
	if (prevItem)
		listViewNames->setCurrentItem(prevItem);
	else if (listViewNames->firstChild())
		listViewNames->setCurrentItem(listViewNames->firstChild());
	else
		textEditAction->clear();
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

	if (!curAction.isEmpty()) {
		QSettings s;
		QString key(ACT_GROUP_KEY + curAction + ACT_TEXT_KEY);
		s.setValue(key, textEditAction->text());
	}
}

void CustomActionImpl::checkBoxRefreshAfterAction_toggled(bool b) {

	if (!curAction.isEmpty()) {
		QString flags(ACT_GROUP_KEY + curAction + ACT_FLAGS_KEY);
		setFlag(ACT_REFRESH_F, b, flags);
	}
}

void CustomActionImpl::checkBoxAskArgs_toggled(bool b) {

	if (!curAction.isEmpty()) {
		QString flags(ACT_GROUP_KEY + curAction + ACT_FLAGS_KEY);
		setFlag(ACT_CMD_LINE_F, b, flags);
	}
}

void CustomActionImpl::pushButtonOk_clicked() {

	close();
}
