/*
	Description: custom action handling

	Author: Marco Costalba (C) 2006-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include "common.h"
#include "customactionimpl.h"

using namespace QGit;

CustomActionImpl::CustomActionImpl() {

	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);

	QSettings settings;
	restoreGeometry(settings.value(ACT_GEOM_KEY).toByteArray());
	QStringList actions = settings.value(ACT_LIST_KEY).toStringList();

	listWidgetNames->insertItems(0, actions);
	if (listWidgetNames->count())
		listWidgetNames->setCurrentItem(listWidgetNames->item(0));
	else
		listWidgetNames_currentItemChanged(NULL, NULL);
}

void CustomActionImpl::closeEvent(QCloseEvent* ce) {

	QSettings settings;
	settings.setValue(ACT_GEOM_KEY, saveGeometry());
	QWidget::closeEvent(ce);
}

void CustomActionImpl::loadAction(const QString& name) {

	const QString flags(ACT_GROUP_KEY + name + ACT_FLAGS_KEY);
	checkBoxRefreshAfterAction->setChecked(testFlag(ACT_REFRESH_F, flags));
	checkBoxAskArgs->setChecked(testFlag(ACT_CMD_LINE_F, flags));
	QSettings set;
	const QString& data(set.value(ACT_GROUP_KEY + name + ACT_TEXT_KEY, "").toString());
	textEditAction->setPlainText(data);
}

void CustomActionImpl::removeAction(const QString& name) {

	QSettings set;
	set.remove(ACT_GROUP_KEY + name);
}

const QStringList CustomActionImpl::actions() {

	QStringList actionsList;
	QListWidgetItem* item;
	int row = 0;
	while ((item = listWidgetNames->item(row)) != 0) {
		actionsList.append(item->text());
		row++;
	}
	return actionsList;
}

void CustomActionImpl::updateActions() {

	QSettings settings;
	settings.setValue(ACT_LIST_KEY, actions());
	emit listChanged(actions());
}

void CustomActionImpl::listWidgetNames_currentItemChanged(QListWidgetItem* item, QListWidgetItem*) {

	bool empty = (item == NULL);

	if (!empty) {
		loadAction(item->text());
		listWidgetNames->scrollToItem(item);
	} else {
		textEditAction->clear();
		if (checkBoxRefreshAfterAction->isChecked())
			checkBoxRefreshAfterAction->toggle();

		if (checkBoxAskArgs->isChecked())
			checkBoxAskArgs->toggle();
	}
	textEditAction->setEnabled(!empty);
	checkBoxRefreshAfterAction->setEnabled(!empty);
	checkBoxAskArgs->setEnabled(!empty);
	pushButtonRename->setEnabled(!empty);
	pushButtonRemove->setEnabled(!empty);
	pushButtonMoveUp->setEnabled(!empty && (item != listWidgetNames->item(0)));
	int lastRow = listWidgetNames->count() - 1;
	pushButtonMoveDown->setEnabled(!empty && (item != listWidgetNames->item(lastRow)));
}

bool CustomActionImpl::getNewName(QString& name, const QString& caption) {

	bool ok;
	const QString oldName = name;
	name = QInputDialog::getText(this, caption + " - QGit", "Enter action name:",
	                             QLineEdit::Normal, name, &ok);

	if (!ok || name.isEmpty() || name == oldName)
		return false;

	if (actions().contains(name)) {
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

	QListWidgetItem* item = new QListWidgetItem(name);
	listWidgetNames->addItem(item);
	updateActions();
	listWidgetNames->setCurrentItem(item);
	textEditAction->setPlainText("<write here your action's commands sequence>");
	textEditAction->selectAll();
	textEditAction->setFocus();
}

void CustomActionImpl::pushButtonRename_clicked() {

	QListWidgetItem* item = listWidgetNames->currentItem();
	if (!item || !item->isSelected())
		return;

	QString newName(item->text());
	if (!getNewName(newName, "Rename action"))
		return;

	const QString oldActionName(item->text());
	item->setText(newName);
	updateActions();
	listWidgetNames_currentItemChanged(item, item);
	loadAction(oldActionName);
	removeAction(oldActionName);
}

void CustomActionImpl::pushButtonRemove_clicked() {

	QListWidgetItem* item = listWidgetNames->currentItem();
	if (!item || !item->isSelected())
		return;

	removeAction(item->text());
	delete item;
	updateActions();
	if (!listWidgetNames->count())
		listWidgetNames_currentItemChanged(NULL, NULL);
}

void CustomActionImpl::pushButtonMoveUp_clicked() {

	QListWidgetItem* item = listWidgetNames->currentItem();
	int row = listWidgetNames->row(item);
	if (!item || row == 0)
		return;

	item = listWidgetNames->takeItem(row);
	listWidgetNames->insertItem(row - 1, item);
	updateActions();
	listWidgetNames->setCurrentItem(item);
}

void CustomActionImpl::pushButtonMoveDown_clicked() {

	QListWidgetItem* item = listWidgetNames->currentItem();
	int row = listWidgetNames->row(item);
	if (!item || row == listWidgetNames->count() - 1)
		return;

	item = listWidgetNames->takeItem(row);
	listWidgetNames->insertItem(row + 1, item);
	updateActions();
	listWidgetNames->setCurrentItem(item);
}

void CustomActionImpl::textEditAction_textChanged() {

	QListWidgetItem* item = listWidgetNames->currentItem();
	if (item) {
		QSettings s;
		QString key(ACT_GROUP_KEY + item->text() + ACT_TEXT_KEY);
		s.setValue(key, textEditAction->toPlainText());
	}
}

void CustomActionImpl::checkBoxRefreshAfterAction_toggled(bool b) {

	QListWidgetItem* item = listWidgetNames->currentItem();
	if (item) {
		QString flags(ACT_GROUP_KEY + item->text() + ACT_FLAGS_KEY);
		setFlag(ACT_REFRESH_F, b, flags);
	}
}

void CustomActionImpl::checkBoxAskArgs_toggled(bool b) {

	QListWidgetItem* item = listWidgetNames->currentItem();
	if (item) {
		QString flags(ACT_GROUP_KEY + item->text() + ACT_FLAGS_KEY);
		setFlag(ACT_CMD_LINE_F, b, flags);
	}
}

void CustomActionImpl::pushButtonOk_clicked() {

	close();
}
