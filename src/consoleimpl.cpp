/*
    Description: stdout viewer

    Author: Marco Costalba (C) 2006-2007

    Copyright: See COPYING file that comes with this distribution

*/
#include <QSettings>
#include <QStatusBar>
#include <QMessageBox>
#include "myprocess.h"
#include "git.h"
#include "consoleimpl.h"

ConsoleImpl::ConsoleImpl(const QString& nm, Git* g) : git(g), actionName(nm) {

    setAttribute(Qt::WA_DeleteOnClose);
    setupUi(this);
    textEditOutput->setFont(QGit::TYPE_WRITER_FONT);
    QFont f = textLabelCmd->font();
    f.setBold(true);
    textLabelCmd->setFont(f);
    setWindowTitle("\'" + actionName + "\' output window - QGit");
    QGit::restoreGeometrySetting(QGit::CON_GEOM_KEY, this);
}

void ConsoleImpl::typeWriterFontChanged() {

    QTextEdit* te = textEditOutput;
    te->setFont(QGit::TYPE_WRITER_FONT);
    te->setPlainText(te->toPlainText());
    te->moveCursor(QTextCursor::End);
}

void ConsoleImpl::on_pushButtonOk_clicked(bool) {

    close();
}

void ConsoleImpl::on_pushButtonTerminate_clicked(bool) {

    git->cancelProcess(proc);
    procFinished();
}

void ConsoleImpl::closeEvent(QCloseEvent* ce) {

    if (proc && proc->state() == QProcess::Running)
        if (QMessageBox::question(this, "Action output window - QGit",
            "Action is still running.\nAre you sure you want to close "
            "the window and leave the action running in background?",
            "&Yes", "&No", QString(), 1, 1) == 1) {
            ce->ignore();
            return;
        }
    if (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();

    QGit::saveGeometrySetting(QGit::CON_GEOM_KEY, this);
    QMainWindow::closeEvent(ce);
}

bool ConsoleImpl::start(const QString& cmd) {

    textLabelEnd->setText("Executing \'" + actionName + "\' action...");
    textLabelCmd->setText(cmd);
    if (cmd.indexOf('\n') < 0)
        proc = git->runAsync(cmd, this);
    else
        proc = git->runAsScript(cmd, this); // wrap multiline cmd in a script

    if (proc.isNull())
        deleteLater();
    else
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    return !proc.isNull();
}

void ConsoleImpl::procReadyRead(const QByteArray& data) {

    QString newParagraph;
    if (QGit::stripPartialParaghraps(data, &newParagraph, &inpBuf))
        // QTextEdit::append() adds a new paragraph,
        // i.e. inserts a LF if not already present.
        textEditOutput->append(newParagraph);
}

void ConsoleImpl::procFinished() {

    textEditOutput->append(inpBuf);
    inpBuf = "";
    QApplication::restoreOverrideCursor();
    textLabelEnd->setText("End of \'" + actionName + "\' execution.");
    pushButtonTerminate->setEnabled(false);
    emit customAction_exited(actionName);
}
