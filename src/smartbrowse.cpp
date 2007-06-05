/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>
#include <QWheelEvent>
#include "revsview.h"
#include "smartbrowse.h"

#define GO_UP   1
#define GO_DOWN 2
#define GO_LOG  3
#define GO_DIFF 4

#define AT_TOP 1
#define AT_BTM 2

void SmartLabel::contextMenuEvent(QContextMenuEvent* e) {

	if (text().count("href=") != 2)
		return;

	QMenu* menu = new QMenu(this);
	menu->addAction("Switch links", this, SLOT(switchLinks()));
	menu->exec(e->globalPos());
	delete menu;
}

void SmartLabel::switchLinks() {

	QString t(text());
	QString link1(t.section("<a href=", 1). section("</a>", 0, 0));
	QString link2(t.section("<a href=", 2). section("</a>", 0, 0));
	t.replace(link1, "%1").replace(link2, "%2");
	setText(t.arg(link2, link1));
	adjustSize();
}

SmartBrowse::SmartBrowse(RevsView* par) : QObject(par) {

	rv = par;
	wheelCnt = 0;
	lablesEnabled = QGit::testFlag(QGit::SMART_LBL_F);

	QString txt("<p><img src=\":/icons/resources/%1\"> %2 %3</p>");
	QString link("<a href=\"%1\">%2</a>");
	QString linkUp(link.arg(QString::number(GO_UP), "Up"));
	QString linkDown(link.arg(QString::number(GO_DOWN), "Down"));
	QString linkLog(link.arg(QString::number(GO_LOG), "Log"));
	QString linkDiff(link.arg(QString::number(GO_DIFF), "Diff"));

	QTextEdit* log = static_cast<QTextEdit*>(rv->tab()->textBrowserDesc);
	QTextEdit* diff = static_cast<QTextEdit*>(rv->tab()->textEditDiff);

	logTopLbl = new SmartLabel(txt.arg("1uparrow.png", linkUp, ""), log);
	logBottomLbl = new SmartLabel(txt.arg("1downarrow.png", linkDiff, linkDown), log);
	diffTopLbl = new SmartLabel(txt.arg("1uparrow.png", linkLog, linkUp), diff);
	diffBottomLbl = new SmartLabel(txt.arg("1downarrow.png", linkUp, linkDown), diff);

	diffTopLbl->setFont(qApp->font());    // override parent's font to
	diffBottomLbl->setFont(qApp->font()); // avoid QGit::TYPE_WRITER_FONT

	setVisible(false);

	log->installEventFilter(this);
	diff->installEventFilter(this);
	log->verticalScrollBar()->installEventFilter(this);
	diff->verticalScrollBar()->installEventFilter(this);

	connect(logTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(logBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));
}

void SmartBrowse::flagChanged(uint flag) {

	if (flag == QGit::SMART_LBL_F) {
		lablesEnabled = QGit::testFlag(QGit::SMART_LBL_F);
		setVisible(curTextEdit()->isEnabled());
		updatePosition();
	}
}

QTextEdit* SmartBrowse::curTextEdit(bool* isDiff) {

	bool b = rv->tab()->textEditDiff->isVisible();
	if (isDiff)
		*isDiff = b;

	return (b ? static_cast<QTextEdit*>(rv->tab()->textEditDiff)
	          : static_cast<QTextEdit*>(rv->tab()->textBrowserDesc));
}

void SmartBrowse::setVisible(bool b) {

	b = b && lablesEnabled;
	logTopLbl->setVisible(b);
	logBottomLbl->setVisible(b);
	diffTopLbl->setVisible(b);
	diffBottomLbl->setVisible(b);
}

void SmartBrowse::linkActivated(const QString& text) {

	int key = text.toInt();
	switch (key) {
	case GO_LOG:
	case GO_DIFF:
		rv->toggleDiffView();
		break;
	case GO_UP:
		rv->tab()->listViewLog->on_keyUp();
		break;
	case GO_DOWN:
		rv->tab()->listViewLog->on_keyDown();
		break;
	default:
		dbp("ASSERT in SmartBrowse::linkActivated, key %1 not known", text);
	}
}

bool SmartBrowse::eventFilter(QObject *obj, QEvent *event) {

	if (!lablesEnabled)
		return QObject::eventFilter(obj, event);

	QTextEdit* te = dynamic_cast<QTextEdit*>(obj);
	QScrollBar* vsb = dynamic_cast<QScrollBar*>(obj);

	QEvent::Type t = event->type();
	if (te && t == QEvent::Resize)
		updatePosition();

	if (vsb && (t == QEvent::Show || t == QEvent::Hide))
		updatePosition();

	if (te && t == QEvent::EnabledChange) {
		setVisible(te->isEnabled());
		updatePosition();
	}
	if (vsb && t == QEvent::Wheel) {

		QWheelEvent* we = static_cast<QWheelEvent*>(event);
		int v = updateVisibility(we->delta());

		if (wheelRolled(we->delta(), v != 0))
			return true; // filter event out
	}
	return QObject::eventFilter(obj, event);
}

int SmartBrowse::updateVisibility(int delta) {

	static int MIN = 5;

	bool isDiff;
	QTextEdit* te = curTextEdit(&isDiff);
	QScrollBar* vsb = te->verticalScrollBar();

	bool v = lablesEnabled && te->isEnabled();
	bool top = v && (!vsb->isVisible() || (vsb->value() - vsb->minimum() < MIN));
	bool btm = v && (!vsb->isVisible() || (vsb->maximum() - vsb->value() < MIN));

	if (delta) {
		top = top && delta > 0;
		btm = btm && delta < 0;
	}
	if (isDiff) {
		diffTopLbl->setVisible(top);
		diffBottomLbl->setVisible(btm);
	} else {
		logTopLbl->setVisible(top);
		logBottomLbl->setVisible(btm);
	}
	return AT_TOP * top + AT_BTM * btm;
}

void SmartBrowse::updatePosition() {

	QTextEdit* te = curTextEdit();
	QScrollBar* vb = te->verticalScrollBar();
	QScrollBar* hb = te->horizontalScrollBar();

	int w = te->width() - vb->width() * vb->isVisible();
	int h = te->height() - hb->height() * hb->isVisible();

	logTopLbl->move(w - logTopLbl->width() - 10, 10);
	diffTopLbl->move(w - diffTopLbl->width() - 10, 10);
	logBottomLbl->move(w - logBottomLbl->width() - 10, h - logBottomLbl->height() - 10);
	diffBottomLbl->move(w - diffBottomLbl->width() - 10, h - diffBottomLbl->height() - 10);

	updateVisibility();

	// we are called also when user toggle view manually,
	// so reset wheel counters to be sure we don't have alias
	scrollTimer.restart();
	wheelCnt = 0;
}

bool SmartBrowse::wheelRolled(int delta, bool outOfRange) {

	bool justSwitched = (switchTimer.isValid() && switchTimer.elapsed() < 400);
	if (justSwitched)
		switchTimer.restart();

	bool scrolling = (scrollTimer.isValid() && scrollTimer.elapsed() < 400);
	bool directionChanged = (wheelCnt * delta < 0);

	// a scroll action have to start when in range
	// but can continue also when goes out of range
	if (!outOfRange || scrolling)
		scrollTimer.restart();

	if (!outOfRange || justSwitched)
		return justSwitched; // filter wheels events just after a switch

	// we want a quick rolling action to be considered valid
	bool tooSlow = (timeoutTimer.isValid() && timeoutTimer.elapsed() > 300);
	timeoutTimer.restart();

	if (directionChanged || scrolling || tooSlow)
		wheelCnt = 0;

	// ok, we would be ready to switch, but we want to add some inertia
	wheelCnt += (delta > 0 ? 1 : -1);
	if (wheelCnt * wheelCnt < 9)
		return false;

	QLabel* l;
	if (wheelCnt > 0)
		l = logTopLbl->isVisible() ? logTopLbl : diffTopLbl;
	else
		l = logBottomLbl->isVisible() ? logBottomLbl : diffBottomLbl;

	wheelCnt = 0;
	switchTimer.restart();
	linkActivated(l->text().section("href=", 1).section("\"", 1, 1));
	return false;
}
