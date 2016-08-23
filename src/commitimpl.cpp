/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2007

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
#include <QKeyEvent>
#include "exceptionmanager.h"
#include "common.h"
#include "git.h"
#include "settingsimpl.h"
#include "commitimpl.h"

using namespace QGit;

QString CommitImpl::lastMsgBeforeError;

CommitImpl::CommitImpl(Git* g, bool amend) : git(g) {

	// adjust GUI
	setAttribute(Qt::WA_DeleteOnClose);
	setupUi(this);
	textEditMsg->setFont(TYPE_WRITER_FONT);

	QVector<QSplitter*> v(1, splitter);
	QGit::restoreGeometrySetting(CMT_GEOM_KEY, this, &v);

	QSettings settings;
	QString templ(settings.value(CMT_TEMPL_KEY, CMT_TEMPL_DEF).toString());
	QString msg;
	QDir d;
	if (d.exists(templ))
		readFromFile(templ, msg);

	// set-up files list
	const RevFile* f = git->getFiles(ZERO_SHA);
	for (int i = 0; f && i < f->count(); ++i) { // in case of amend f could be null

		bool inIndex = f->statusCmp(i, RevFile::IN_INDEX);
		bool isNew = (f->statusCmp(i, RevFile::NEW) || f->statusCmp(i, RevFile::UNKNOWN));
		QColor myColor = QPalette().color(QPalette::WindowText);
		if (isNew)
			myColor = Qt::darkGreen;
		else if (f->statusCmp(i, RevFile::DELETED))
			myColor = Qt::red;

		QTreeWidgetItem* item = new QTreeWidgetItem(treeWidgetFiles);
		item->setText(0, git->filePath(*f, i));
		item->setText(1, inIndex ? "Updated in index" : "Not updated in index");
		item->setCheckState(0, inIndex || !isNew ? Qt::Checked : Qt::Unchecked);
		item->setForeground(0, myColor);
	}
	treeWidgetFiles->resizeColumnToContents(0);

	// compute cursor offsets. Take advantage of fixed width font
	textEditMsg->setPlainText("\nx\nx"); // cursor doesn't move on empty text
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
	ofsX = x1 - x0;
	ofsY = y1 - y0;
	textEditMsg->moveCursor(QTextCursor::Start);
	textEditMsg_cursorPositionChanged();

	if (lastMsgBeforeError.isEmpty()) {
		// setup textEditMsg with old commit message to be amended
		QString status("");
		if (amend)
			status = git->getLastCommitMsg();

		// setup textEditMsg with default value if user opted to do so (default)
		if (testFlag(USE_CMT_MSG_F, FLAGS_KEY))
			status += git->getNewCommitMsg();

		msg = status.trimmed();
	} else
		msg = lastMsgBeforeError;

	textEditMsg->setPlainText(msg);
	textEditMsg->setFocus();

	// if message is not changed we avoid calling refresh
	// to change patch name in stgCommit()
	origMsg = msg;

	// setup button functions
	if (amend) {
		if (git->isStGITStack()) {
			pushButtonOk->setText("&Add to top");
			pushButtonOk->setShortcut(QKeySequence("Alt+A"));
			pushButtonOk->setToolTip("Refresh top stack patch");
		} else {
			pushButtonOk->setText("&Amend");
			pushButtonOk->setShortcut(QKeySequence("Alt+A"));
			pushButtonOk->setToolTip("Amend latest commit");
		}
		connect(pushButtonOk, SIGNAL(clicked()),
			this, SLOT(pushButtonAmend_clicked()));
	} else {
		if (git->isStGITStack()) {
			pushButtonOk->setText("&New patch");
			pushButtonOk->setShortcut(QKeySequence("Alt+N"));
			pushButtonOk->setToolTip("Create a new patch");
		}
		connect(pushButtonOk, SIGNAL(clicked()),
			this, SLOT(pushButtonCommit_clicked()));
	}
	connect(treeWidgetFiles, SIGNAL(customContextMenuRequested(const QPoint&)),
	        this, SLOT(contextMenuPopup(const QPoint&)));
	connect(textEditMsg, SIGNAL(cursorPositionChanged()),
	        this, SLOT(textEditMsg_cursorPositionChanged()));

    textEditMsg->installEventFilter(this);
}

void CommitImpl::closeEvent(QCloseEvent*) {

	QVector<QSplitter*> v(1, splitter);
	QGit::saveGeometrySetting(CMT_GEOM_KEY, this, &v);
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

bool CommitImpl::getFiles(SList selFiles) {

	// check for files to commit
	selFiles.clear();
	QTreeWidgetItemIterator it(treeWidgetFiles);
	while (*it) {
		if ((*it)->checkState(0) == Qt::Checked)
			selFiles.append((*it)->text(0));
		++it;
	}

	return !selFiles.isEmpty();
}

void CommitImpl::warnNoFiles() {

	QMessageBox::warning(this, "Commit changes - QGit",
			     "Sorry, no files are selected for updating.",
			     QMessageBox::Ok, QMessageBox::NoButton);
}

bool CommitImpl::checkFiles(SList selFiles) {

	if (getFiles(selFiles))
		return true;

	warnNoFiles();
	return false;
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

bool CommitImpl::checkConfirm(SCRef msg, SCRef patchName, SCList selFiles, bool amend) {

//	QTextCodec* tc = QTextCodec::codecForCStrings();
//	QTextCodec::setCodecForCStrings(0); // set temporary Latin-1

	// NOTEME: i18n-ugly
	QString whatToDo = amend ?
	    (git->isStGITStack() ? "refresh top patch with" :
	     			   "amend last commit with") :
	    (git->isStGITStack() ? "create a new patch with" : "commit");

        QString text("Do you want to " + whatToDo);

        bool const fullList = selFiles.size() < 20;
        if (fullList)
            text.append(" the following file(s)?\n\n" + selFiles.join("\n") +
                        "\n\nwith the message:\n\n");
        else
            text.append(" those " + QString::number(selFiles.size()) +
                        " files the with the message:\n\n");

	text.append(msg);
	if (git->isStGITStack())
		text.append("\n\nAnd patch name: " + patchName);

//	QTextCodec::setCodecForCStrings(tc);

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Commit changes - QGit");
        msgBox.setText(text);
        if (!fullList)
            msgBox.setDetailedText(selFiles.join("\n"));

        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);

        return msgBox.exec() != QMessageBox::No;
}

void CommitImpl::pushButtonSettings_clicked() {

	SettingsImpl setView(this, git, 3);
	setView.exec();
}

void CommitImpl::pushButtonCancel_clicked() {

	close();
}

void CommitImpl::pushButtonCommit_clicked() {

	QStringList selFiles; // retrieve selected files
	if (!checkFiles(selFiles))
		return;

	QString msg; // check for commit message and strip comments
	if (!checkMsg(msg))
		return;

	QString patchName(msg.section('\n', 0, 0)); // the subject
	if (git->isStGITStack() && !checkPatchName(patchName))
		return;

	// ask for confirmation
	if (!checkConfirm(msg, patchName, selFiles, !Git::optAmend))
		return;

	// ok, let's go
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	EM_PROCESS_EVENTS; // to close message box
	bool ok;
	if (git->isStGITStack())
		ok = git->stgCommit(selFiles, msg, patchName, !Git::optFold);
	else
		ok = git->commitFiles(selFiles, msg, !Git::optAmend);

	lastMsgBeforeError = (ok ? "" : msg);
	QApplication::restoreOverrideCursor();
	hide();
	emit changesCommitted(ok);
	close();
}

void CommitImpl::pushButtonAmend_clicked() {

	QStringList selFiles; // retrieve selected files
	getFiles(selFiles);
	// FIXME: If there are no files AND no changes to message, we should not
	// commit. Disabling the commit button in such case might be preferable.

	QString msg(textEditMsg->toPlainText());
	if (msg == origMsg && selFiles.isEmpty()) {
		warnNoFiles();
		return;
	}

	if (msg == origMsg && git->isStGITStack())
		msg = "";
	else if (!checkMsg(msg))
		// We are going to replace the message, so it better isn't empty
		return;

	// ask for confirmation
	// FIXME: We don't need patch name for refresh, do we?
	if (!checkConfirm(msg, "", selFiles, Git::optAmend))
		return;

	// ok, let's go
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	EM_PROCESS_EVENTS; // to close message box
	bool ok;
	if (git->isStGITStack())
		ok = git->stgCommit(selFiles, msg, "", Git::optFold);
	else
		ok = git->commitFiles(selFiles, msg, Git::optAmend);

	QApplication::restoreOverrideCursor();
	hide();
	emit changesCommitted(ok);
	close();
}

void CommitImpl::pushButtonUpdateCache_clicked() {

	QStringList selFiles;
	if (!checkFiles(selFiles))
		return;

	bool ok = git->updateIndex(selFiles);

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
	col_pos = (r.x() + hs + r.width() / 2) / ofsX;
	line_pos = (r.y() + vs) / ofsY;
}

bool CommitImpl::eventFilter(QObject* obj, QEvent* event) {

    if (obj == textEditMsg) {
        if (event->type() == QEvent::KeyPress) {
             QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
             if (( keyEvent->key() == Qt::Key_Return
                   || keyEvent->key() == Qt::Key_Enter
                 )
                 && keyEvent->modifiers() & Qt::ControlModifier) {

                QMetaObject::invokeMethod(pushButtonOk, "clicked", Qt::QueuedConnection);
                return true;
             }
         }
         return false;
    }
    return QObject::eventFilter(obj, event);
}
