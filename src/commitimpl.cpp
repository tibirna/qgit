/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#include <QTextCodec>
#include <QSettings>
#include <QMenu>
#include <QRegExp>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QToolTip>
#include <QScrollBar>
#include "exceptionmanager.h"
#include "common.h"
#include "git.h"
#include "settingsimpl.h"
#include "commitimpl.h"

using namespace QGit;

CommitImpl::CommitImpl(Git* g) : git(g) {

	// adjust GUI
	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);
	textEditMsg->setFont(TYPE_WRITER_FONT);

	// read settings
	QSettings settings;
	restoreGeometry(settings.value(CMT_GEOM_KEY).toByteArray());
	QSize sz(settings.value(CMT_SPLIT_KEY).toSize());
	if (sz.isValid()) {
		QList<int> szList;
		szList.append(sz.width());
		szList.append(sz.height());
		splitter->setSizes(szList);
	}
	QString templ(settings.value(CMT_TEMPL_KEY, CMT_TEMPL_DEF).toString());
	QString msg;
	QDir d;
	if (d.exists(templ))
		readFromFile(templ, msg);

	// set-up files list
	const RevFile* f = git->getFiles(ZERO_SHA);
	for (int i = 0; i < f->count(); ++i) {

		bool inIndex = f->statusCmp(i, RevFile::IN_INDEX);
		bool isNew = (f->statusCmp(i, RevFile::NEW) || f->statusCmp(i, RevFile::UNKNOWN));
		QColor myColor = Qt::black;
		if (isNew)
			myColor = Qt::darkGreen;
		else if (f->statusCmp(i, RevFile::DELETED))
			myColor = Qt::red;

		QTreeWidgetItem* item = new QTreeWidgetItem(treeWidgetFiles);
		item->setText(0, git->filePath(*f, i));
		item->setText(1, inIndex ? "In sync" : "Out of sync");
		item->setCheckState(0, inIndex || !isNew ? Qt::Checked : Qt::Unchecked);
		item->setForeground(0, myColor);
		item->setTextAlignment(1, Qt::AlignHCenter);
	}
	treeWidgetFiles->resizeColumnToContents(0);
	treeWidgetFiles->resizeColumnToContents(1);

	// setup textEditMsg with default value
	QString status(git->getDefCommitMsg());
	status.prepend('\n').replace(QRegExp("\\n([^#])"), "\n#\\1"); // comment all the lines
	msg.append(status.trimmed());
	textEditMsg->setPlainText(msg);

	// compute cursor offsets. Take advantage of fixed width font
	textEditMsg->moveCursor(QTextCursor::Start);
	textEditMsg->verticalScrollBar()->setValue(0);
	textEditMsg->horizontalScrollBar()->setValue(0);
	int y0 = textEditMsg->cursorRect().y();
	int x0 = textEditMsg->cursorRect().x();
	textEditMsg->moveCursor(QTextCursor::Down);
	textEditMsg->moveCursor(QTextCursor::Right);
	textEditMsg->verticalScrollBar()->setValue(0);
	int y1 = textEditMsg->cursorRect().y();
	int x1 = textEditMsg->cursorRect().x();
	_ofsX = x1 - x0;
	_ofsY = y1 - y0;
	textEditMsg->moveCursor(QTextCursor::Start);
	textEditMsg_cursorPositionChanged();

	// if message is not changed we avoid calling refresh
	// to change patch name in stgCommit()
	origMsg = msg;

	// setup button functions
	if (git->isStGITStack()) {
		pushButtonOk->setText("&New patch");
		pushButtonOk->setShortcut(QKeySequence("Alt+N"));
		pushButtonOk->setToolTip("Create a new patch");
		pushButtonUpdateCache->setText("&Add to top");
		pushButtonOk->setShortcut(QKeySequence("Alt+A"));
		pushButtonUpdateCache->setToolTip("Refresh top stack patch");
	}
	connect(treeWidgetFiles, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(contextMenuPopup(const QPoint&)));
	connect(textEditMsg, SIGNAL(cursorPositionChanged()),
	        this, SLOT(textEditMsg_cursorPositionChanged()));
}

void CommitImpl::closeEvent(QCloseEvent*) {

	QSettings settings;
	settings.setValue(CMT_GEOM_KEY, saveGeometry());
	QList<int> sz = splitter->sizes();
	settings.setValue(CMT_SPLIT_KEY, QSize(sz[0], sz[1]));
}

void CommitImpl::contextMenuPopup(const QPoint& pos)  {

	QMenu* contextMenu = new QMenu(this);
	QAction* a = contextMenu->addAction("Select All");
	connect(a, SIGNAL(triggered()), this, SLOT(checkAll()));
	a = contextMenu->addAction("Unselect All");
	connect(a, SIGNAL(triggered()), this, SLOT(unCheckAll()));
	contextMenu->popup(mapToGlobal(pos));
}

void CommitImpl::checkAll() { checkUncheck(true); }
void CommitImpl::unCheckAll() { checkUncheck(false); }

void CommitImpl::checkUncheck(bool checkAll) {

	QTreeWidgetItemIterator it(treeWidgetFiles);
	while (*it) {
		(*it)->setCheckState(0, checkAll ? Qt::Checked : Qt::Unchecked);
		++it;
	}
}

bool CommitImpl::checkFiles(SList selFiles) {

	// check for files to commit
	selFiles.clear();
	QTreeWidgetItemIterator it(treeWidgetFiles);
	while (*it) {
		if ((*it)->checkState(0) == Qt::Checked)
			selFiles.append((*it)->text(0));
		++it;
	}
	if (selFiles.isEmpty())
		QMessageBox::warning(this, "Commit changes - QGit",
		                     "Sorry, no files are selected for updating.",
		                     QMessageBox::Ok, QMessageBox::NoButton);
	return !selFiles.isEmpty();
}

bool CommitImpl::checkMsg(QString& msg) {

	msg = textEditMsg->toPlainText();
	msg.remove(QRegExp("(^|\\n)\\s*#[^\\n]*")); // strip comments
	msg.replace(QRegExp("[ \\t\\r\\f\\v]+\\n"), "\n"); // strip line trailing cruft
	msg = msg.trimmed();
	if (msg.isEmpty()) {
		QMessageBox::warning(this, "Commit changes - QGit",
		                     "Sorry, I don't want an empty message.",
		                     QMessageBox::Ok, QMessageBox::NoButton);
		return false;
	}
	// split subject from message body
	QString subj(msg.section('\n', 0, 0, QString::SectionIncludeTrailingSep));
	QString body(msg.section('\n', 1).trimmed());
	msg = subj + '\n' + body + '\n';
	return true;
}

bool CommitImpl::checkPatchName(QString& patchName) {

	bool ok;
	patchName = patchName.simplified();
	patchName.replace(' ', "_");
	patchName = QInputDialog::getText(this, "Create new patch - QGit", "Enter patch name:",
	                                  QLineEdit::Normal, patchName, &ok);
	if (!ok || patchName.isEmpty())
		return false;

	QString tmp(patchName.trimmed());
	if (patchName != tmp.remove(' '))
		QMessageBox::warning(this, "Create new patch - QGit", "Sorry, control "
		                     "characters or spaces\n are not allowed in patch name.");

	else if (git->isPatchName(patchName))
		QMessageBox::warning(this, "Create new patch - QGit", "Sorry, patch name "
		                     "already exists.\nPlease choose a different name.");
	else
		return true;

	return false;
}

bool CommitImpl::checkConfirm(SCRef msg, SCRef patchName, SCList selFiles) {

	QTextCodec* tc = QTextCodec::codecForCStrings();
	QTextCodec::setCodecForCStrings(0); // set temporary Latin-1

	QString whatToDo = (git->isStGITStack() ? "create a new patch with" : "commit");
	QString text("Do you want to " + whatToDo + " the following file(s)?\n\n" +
	             selFiles.join("\n") + "\n\nwith the message:\n\n");
	text.append(msg);
	if (git->isStGITStack())
		text.append("\n\nAnd patch name: " + patchName);

	QTextCodec::setCodecForCStrings(tc);

	int but = QMessageBox::question(this, "Commit changes - QGit",
	                                text, "&Yes", "&No", QString(), 0, 1);
	return (but != 1);
}

void CommitImpl::pushButtonSettings_clicked() {

	SettingsImpl setView(this, git, 3);
	setView.exec();
}

void CommitImpl::pushButtonCancel_clicked() {

	close();
}

void CommitImpl::pushButtonOk_clicked() {

	QStringList selFiles; // retrieve selected files
	if (!checkFiles(selFiles))
		return;

	QString msg; // check for commit message and strip comments
	if (!checkMsg(msg))
		return;

	QString patchName(msg.section('\n', 0, 0)); // the subject
	if (git->isStGITStack() && !checkPatchName(patchName))
		return;

	if (!checkConfirm(msg, patchName, selFiles)) // ask for confirmation
		return;

	// ok, let's go
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	EM_PROCESS_EVENTS; // to close message box
	bool ok;
	if (git->isStGITStack())
		ok = git->stgCommit(selFiles, msg, patchName, !Git::optFold);
	else
		ok = git->commitFiles(selFiles, msg);

	QApplication::restoreOverrideCursor();
	hide();
	emit changesCommitted(ok);
	close();
}

void CommitImpl::pushButtonUpdateCache_clicked() {

	QStringList selFiles;
	if (!checkFiles(selFiles))
		return;

	if (git->isStGITStack())
		if (QMessageBox::question(this, "Refresh stack - QGit",
			"Do you want to refresh current top stack patch?",
			"&Yes", "&No", QString(), 0, 1) == 1)
			return;

	QString msg(textEditMsg->toPlainText());
	if (msg == origMsg)
		msg = ""; // to tell stgCommit() not to refresh patch name

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	EM_PROCESS_EVENTS; // to close message box
	bool ok;
	if (git->isStGITStack())
		ok = git->stgCommit(selFiles, msg, "", Git::optFold);
	else
		ok = git->updateIndex(selFiles);

	QApplication::restoreOverrideCursor();
	emit changesCommitted(ok);
	close();
}

void CommitImpl::textEditMsg_cursorPositionChanged() {

	int col_pos, line_pos;
	computePosition(col_pos, line_pos);
	QString lineNumber = QString("Line: %1 Col: %2")
	                             .arg(line_pos + 1).arg(col_pos + 1);
	textLabelLineCol->setText(lineNumber);
}

void CommitImpl::computePosition(int &col_pos, int &line_pos) {

	QRect r = textEditMsg->cursorRect();
	int vs = textEditMsg->verticalScrollBar()->value();
	int hs = textEditMsg->horizontalScrollBar()->value();

	// when in start position r.x() = -r.width() / 2
	col_pos = (r.x() + hs + r.width() / 2) / _ofsX;
	line_pos = (r.y() + vs) / _ofsY;
}
