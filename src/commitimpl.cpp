/*
	Description: changes commit dialog

	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution

   Part of this code is taken from Fredrik Kuivinen "Gct"
   tool. I have just translated from Python to C++
*/
#include <q3listview.h>
#include <qsettings.h>
#include <qlabel.h>
#include <q3textedit.h>
#include <qmessagebox.h>
#include <qapplication.h>
#include <qcursor.h>
#include <qsplitter.h>
#include <qpushbutton.h>
#include <qinputdialog.h>
#include <q3popupmenu.h>
#include <qregexp.h>
#include <qtooltip.h>
#include <q3stylesheet.h>
#include <qtextcodec.h>
//Added by qt3to4:
#include <Q3ValueList>
#include "exceptionmanager.h"
#include "common.h"
#include "git.h"
#include "settingsimpl.h"
#include "commitimpl.h"

using namespace QGit;

// ******************************* CheckListFileItem ****************************

class CheckListFileItem: public Q3CheckListItem {
public:
	CheckListFileItem(Q3ListView* lv, SCRef file, SCRef status, SCRef index) :
	                  Q3CheckListItem(lv, file, Q3CheckListItem::CheckBox) {

		setText(1, status);
		setText(2, index);
		myColor = Qt::black;

		if (status == NEW || status == UNKNOWN)
			myColor = Qt::darkGreen;

		else if (status == DELETED)
			myColor = Qt::red;
	}
	virtual void paintCell(QPainter *p, const QColorGroup& cg, int column,
	                       int width, int alignment) {

		QColorGroup _cg(cg);
		if (column == 0)
			_cg.setColor(QColorGroup::Text, myColor);

		Q3CheckListItem::paintCell(p, _cg, column, width, alignment);
	}
private:
	QColor myColor;
};

// ******************************* CommitImpl ****************************

CommitImpl::CommitImpl(Git* g) : QWidget(0, 0, Qt::WDestructiveClose), git(g) {

	// adjust GUI
	setupUi(this);
	listViewFiles->setColumnAlignment(1, Qt::AlignHCenter);
	listViewFiles->setColumnAlignment(2, Qt::AlignHCenter);
	textEditMsg->setFont(TYPE_WRITER_FONT);

	// read settings
	QSettings settings;
	QString tmp = settings.readEntry(APP_KEY + CMT_GEOM_KEY, CMT_GEOM_DEF);
	QStringList sl = QStringList::split(',', tmp);
	QPoint pos(sl[0].toInt(), sl[1].toInt());
	QSize size(sl[2].toInt(), sl[3].toInt());
	resize(size);
	move(pos);
	tmp = settings.readEntry(APP_KEY + CMT_SPLIT_KEY, CMT_SPLIT_DEF);
	sl = QStringList::split(',', tmp);
	Q3ValueList<int> sz;
	sz.append(sl[0].toInt());
	sz.append(sl[1].toInt());
	splitter->setSizes(sz);
	tmp = settings.readEntry(APP_KEY + CMT_TEMPL_KEY, CMT_TEMPL_DEF);
	QString msg;
	QDir d;
	if (d.exists(tmp))
		readFromFile(tmp, msg);

	// set-up files list
	const RevFile* files = git->getFiles(ZERO_SHA);
	for (int i = 0; i < files->names.count(); ++i) {

		SCRef lbl(files->isInIndex(i) ? "In sync" : "Out of sync");

		CheckListFileItem* item = new CheckListFileItem(listViewFiles,
		                          git->filePath(*files, i), files->getStatus(i), lbl);

		item->setOn(!files->statusCmp(i, UNKNOWN));
	}
	// setup textEditMsg with default value
	QString status(git->getDefCommitMsg());
	if (status != "nothing to commit") { // FIXME: dirty hack!!
		status.remove("\nnothing to commit", false);
		msg.append(status);
	}
	textEditMsg->setText(msg);
	textEditMsg->setCursorPosition(0, 0);

	// if message is not changed we avoid calling refresh
	// to change patch name in stgCommit()
	origMsg = msg;

	// setup button functions
	if (git->isStGITStack()) {
		pushButtonOk->setText("&New patch");
		pushButtonOk->setAccel(QKeySequence("Alt+N"));
		QToolTip::remove(pushButtonOk);
		QToolTip::add(pushButtonOk, "Create a new patch");
		pushButtonUpdateCache->setText("&Add to top");
		pushButtonOk->setAccel(QKeySequence("Alt+A"));
		QToolTip::remove(pushButtonUpdateCache);
		QToolTip::add(pushButtonUpdateCache, "Refresh top stack patch");
	}
	// setup listViewFiles popup
	contextMenu = new Q3PopupMenu(this); // will be deleted when this is destroyed
	CHECK_ALL = contextMenu->insertItem("Select All");
	UNCHECK_ALL = contextMenu->insertItem("Unselect All");
	connect(contextMenu, SIGNAL(activated(int)), this, SLOT(checkUncheck(int)));
	connect(listViewFiles, SIGNAL(contextMenuRequested(Q3ListViewItem*, const QPoint&, int)),
	        this, SLOT(contextMenuPopup(Q3ListViewItem*, const QPoint&, int)));
}

CommitImpl::~CommitImpl() {

	QSettings settings;
	QString tmp = QString("%1,%2,%3,%4").arg(pos().x()).arg(pos().y())
	                      .arg(size().width()).arg(size().height());
	settings.writeEntry(APP_KEY + CMT_GEOM_KEY, tmp);
	Q3ValueList<int> sz = splitter->sizes();
	tmp = QString::number(sz[0]) + "," + QString::number(sz[1]);
	settings.writeEntry(APP_KEY + CMT_SPLIT_KEY, tmp);
	close();
}

void CommitImpl::contextMenuPopup(Q3ListViewItem*, const QPoint& pos, int)  {

	contextMenu->popup(pos);
}

void CommitImpl::checkUncheck(int id) {

	Q3ListViewItemIterator it(listViewFiles);
	while (it.current()) {
		((Q3CheckListItem*)it.current())->setOn(id == CHECK_ALL);
		++it;
	}
}

bool CommitImpl::checkFiles(SList selFiles) {

	// check for files to commit
	selFiles.clear();
	Q3ListViewItemIterator it(listViewFiles);
	while (it.current()) {
		if (((Q3CheckListItem*)it.current())->isOn())
			selFiles.append(it.current()->text(0));
		++it;
	}
	if (selFiles.isEmpty())
		QMessageBox::warning(this, "Commit changes - QGit",
		                     "Sorry, no files are selected for updating.",
		                     QMessageBox::Ok, QMessageBox::NoButton);
	return !selFiles.isEmpty();
}

bool CommitImpl::checkMsg(QString& msg) {

	msg = textEditMsg->text();
	msg.remove(QRegExp("\\n\\s*#[^\\n]*")); // strip comments
	msg.replace(QRegExp("[ \\t\\r\\f\\v]+\\n"), "\n"); // strip line trailing cruft
	msg = msg.stripWhiteSpace();
	if (msg.isEmpty()) {
		QMessageBox::warning(this, "Commit changes - QGit",
		                     "Sorry, I don't want an empty message.",
		                     QMessageBox::Ok, QMessageBox::NoButton);
		return false;
	}
	// split subject from message body
	QString subj(msg.section('\n', 0, 0, QString::SectionIncludeTrailingSep));
	QString body(msg.section('\n', 1).stripWhiteSpace());
	msg = subj + '\n' + body + '\n';
	return true;
}

bool CommitImpl::checkPatchName(QString& patchName) {

	bool ok;
	patchName = patchName.simplifyWhiteSpace().stripWhiteSpace();
	patchName.replace(' ', "_");
	patchName = QInputDialog::getText("Create new patch - QGit", "Enter patch name:",
	                                  QLineEdit::Normal, patchName, &ok, this);
	if (!ok || patchName.isEmpty())
		return false;

	QString tmp(patchName.simplifyWhiteSpace());
	if (patchName != tmp.remove(' ')) {
		QMessageBox::warning(this, "Create new patch - QGit", "Sorry, control "
		                     "characters or spaces\n are not allowed in patch name.");
		return false;
	}
	if (git->isPatchName(patchName)) {
		QMessageBox::warning(this, "Create new patch - QGit", "Sorry, patch name "
			             "already exists.\nPlease choose a different name.");
		return false;
	}
	return true;
}

bool CommitImpl::checkConfirm(SCRef msg, SCRef patchName, SCList selFiles) {

	QTextCodec* tc = QTextCodec::codecForCStrings();
	QTextCodec::setCodecForCStrings(0); // set temporary Latin-1

	QString whatToDo = (git->isStGITStack()) ? "create a new patch with" : "commit";
	QString text(msg);
	text.replace("\n", "\n\t");
	text.prepend("Do you want to " + whatToDo + " the following file(s)?\n\n\t" +
	             selFiles.join("\n\t") +"\n\nwith the message:\n\n\t");

	if (git->isStGITStack())
		text.append("\n\nAnd patch name: " + patchName);

	text = Q3StyleSheet::convertFromPlainText(text);
	QTextCodec::setCodecForCStrings(tc);

	if (QMessageBox::question(this, "Commit changes - QGit", text, "&Yes",
	                          "&No", QString::null, 0, 1) == 1)
		return false;

	return true;
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
			"&Yes", "&No", QString::null, 0, 1) == 1)
			return;

	QString msg(textEditMsg->text());
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

void CommitImpl::pushButtonSettings_clicked() {

	SettingsImpl* setView = new SettingsImpl(this, git, 3);
	setView->exec();
	// SettingsImpl has Qt::WDestructiveClose, no need to delete
}

void CommitImpl::pushButtonCancel_clicked() {

	close();
}

void CommitImpl::textEditMsg_cursorPositionChanged(int para, int pos) {

	int col_pos, line_pos;
	computePosition(para, pos, col_pos, line_pos);
	QString lineNumber = QString("Line: %1 Col: %2")
	                             .arg(line_pos + 1).arg(col_pos + 1);
	textLabelLineCol->setText(lineNumber);
}

/*
	Following code to compute cursor row and col position is
	shameless taken from KEdit, indeed, it comes from
	http://websvn.kde.org/branches/KDE/3.4/kdelibs/kdeui/keditcl1.cpp
*/
void CommitImpl::computePosition(int line, int col, int &col_pos, int &line_pos) {

	// line is expressed in paragraphs, we now need to convert to lines
	line_pos = 0;
	if (textEditMsg->wordWrap() == Q3TextEdit::NoWrap)
		line_pos = line;
	else {
		for (int i = 0; i < line; i++)
			line_pos += textEditMsg->linesOfParagraph(i);
	}
	int line_offset = textEditMsg->lineOfChar(line, col);
	line_pos += line_offset;

	// We now calculate where the current line starts in the paragraph.
	const QString linetext(QString::number(line));
	int start_of_line = 0;
	if (line_offset > 0) {
		start_of_line = col;
		while (textEditMsg->lineOfChar(line, --start_of_line) == line_offset);
			start_of_line++;
	}
	// O.K here is the deal: The function getCursorPositoin returns the character
	// position of the cursor, not the screenposition. I.e,. assume the line
	// consists of ab\tc then the character c will be on the screen on position 8
	// whereas getCursorPosition will return 3 if the cursors is on the character c.
	// Therefore we need to compute the screen position from the character position.
	// That's what all the following trouble is all about:
	int coltemp = col - start_of_line;
	int pos  = 0;
	int find = 0;
	int mem  = 0;
	bool found_one = false;

	// if you understand the following algorithm you are worthy to look at the
	// kedit+ sources -- if not, go away ;-)
	while (find >= 0 && find <= coltemp - 1) {
		find = linetext.find('\t', find + start_of_line, true) - start_of_line;
		if (find >=0 && find <= coltemp - 1) {
			found_one = true;
			pos = pos + find - mem;
			pos = pos + 8 - pos % 8;
			mem = find;
			find ++;
		}
	}
	// add the number of characters behind the last tab on the line.
	pos = pos + coltemp - mem;
	if (found_one)
		pos = pos - 1;

	col_pos = pos;
}
