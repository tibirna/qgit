/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
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

SmartLabel::SmartLabel(const QString& text, QWidget* par) : QLabel(text, par) {
	this->setStyleSheet("SmartLabel { border: 1px solid LightGray;"
	                                 "padding: 0px 2px 2px 2px; }");
}

void SmartLabel::paintEvent(QPaintEvent* event) {
	// our QPainter must be destroyed before QLabel's paintEvent is called
	{
		QPainter painter(this);
		QColor backgroundColor = Qt::white;

		// give label a semi-transparent background
		backgroundColor.setAlpha(200);
		painter.fillRect(this->rect(), backgroundColor);
	}

	// let QLabel do the rest
	QLabel::paintEvent(event);
}

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

	logTopLbl = new SmartLabel(txt.arg("go-up.svg", linkUp, ""), log);
	logBottomLbl = new SmartLabel(txt.arg("go-down.svg", linkDiff, linkDown), log);
	diffTopLbl = new SmartLabel(txt.arg("go-up.svg", linkLog, linkUp), diff);
	diffBottomLbl = new SmartLabel(txt.arg("go-down.svg", linkUp, linkDown), diff);

	diffTopLbl->setFont(qApp->font());    // override parent's font to
	diffBottomLbl->setFont(qApp->font()); // avoid QGit::TYPE_WRITER_FONT

	setVisible(false);

	log->installEventFilter(this);
	diff->installEventFilter(this);

	QScrollBar* vsbLog = log->verticalScrollBar();
	QScrollBar* vsbDiff = diff->verticalScrollBar();

	vsbLog->installEventFilter(this);
	vsbDiff->installEventFilter(this);

	log->horizontalScrollBar()->installEventFilter(this);
	diff->horizontalScrollBar()->installEventFilter(this);

	connect(vsbLog, SIGNAL(valueChanged(int)),
	        this, SLOT(updateVisibility()));

	connect(vsbDiff, SIGNAL(valueChanged(int)),
	        this, SLOT(updateVisibility()));

	connect(logTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(logBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffTopLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));

	connect(diffBottomLbl, SIGNAL(linkActivated(const QString&)),
	        this, SLOT(linkActivated(const QString&)));
}

void SmartBrowse::setVisible(bool b) {

	b = b && lablesEnabled;
	logTopLbl->setVisible(b);
	logBottomLbl->setVisible(b);
	diffTopLbl->setVisible(b);
	diffBottomLbl->setVisible(b);
}

QTextEdit* SmartBrowse::curTextEdit(bool* isDiff) {

	QTextEdit* log = static_cast<QTextEdit*>(rv->tab()->textBrowserDesc);
	QTextEdit* diff = static_cast<QTextEdit*>(rv->tab()->textEditDiff);

	if (isDiff)
		*isDiff = diff->isVisible();

	if (!diff->isVisible() && !log->isVisible())
		return NULL;

	return (diff->isVisible() ? diff : log);
}

int SmartBrowse::visibilityFlags(bool* isDiff) {

	static int MIN = 5;

	QTextEdit* te = curTextEdit(isDiff);
	if (!te)
		return 0;

	QScrollBar* vsb = te->verticalScrollBar();

	bool v = lablesEnabled && te->isEnabled();
	bool top = v && (!vsb->isVisible() || (vsb->value() - vsb->minimum() < MIN));
	bool btm = v && (!vsb->isVisible() || (vsb->maximum() - vsb->value() < MIN));

	return AT_TOP * top + AT_BTM * btm;
}

void SmartBrowse::updateVisibility() {

	bool isDiff;
	int flags = visibilityFlags(&isDiff);

	if (isDiff) {
		diffTopLbl->setVisible(flags & AT_TOP);
		diffBottomLbl->setVisible(flags & AT_BTM);
	} else {
		logTopLbl->setVisible(flags & AT_TOP);
		logBottomLbl->setVisible(flags & AT_BTM);
	}
}

void SmartBrowse::flagChanged(uint flag) {

	if (flag == QGit::SMART_LBL_F && curTextEdit()) {
		lablesEnabled = QGit::testFlag(QGit::SMART_LBL_F);
		setVisible(curTextEdit()->isEnabled());
		updatePosition();
	}
	if (flag == QGit::LOG_DIFF_TAB_F)
		rv->setTabLogDiffVisible(QGit::testFlag(QGit::LOG_DIFF_TAB_F));
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
	QScrollBar* sb = dynamic_cast<QScrollBar*>(obj);

	QEvent::Type t = event->type();
	if (te && t == QEvent::Resize)
		updatePosition();

	if (sb && (t == QEvent::Show || t == QEvent::Hide))
		updatePosition();

	if (te && t == QEvent::EnabledChange) {
		setVisible(te->isEnabled());
		updatePosition();
	}
	if (sb && t == QEvent::Wheel && sb->orientation() == Qt::Vertical) {

		QWheelEvent* we = static_cast<QWheelEvent*>(event);
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
		if (wheelRolled(we->delta(), visibilityFlags()))
#else
		if (wheelRolled(we->angleDelta().y(), visibilityFlags()))
#endif
			return true; // filter event out
	}
	return QObject::eventFilter(obj, event);
}

void SmartBrowse::updatePosition() {

	QTextEdit* te = curTextEdit();
	if (!te)
		return;

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

bool SmartBrowse::wheelRolled(int delta, int flags) {

	bool justSwitched = (switchTimer.isValid() && switchTimer.elapsed() < 400);
	if (justSwitched)
		switchTimer.restart();

	bool scrolling = (scrollTimer.isValid() && scrollTimer.elapsed() < 400);
	bool directionChanged = (wheelCnt * delta < 0);

	// we are called before the scroll bar is updated, so we need
	// to take in account roll direction to avoid false positives
	bool scrollingOut = (  ((flags & AT_TOP) && (delta > 0))
	                     ||((flags & AT_BTM) && (delta < 0)));

	// a scroll action have to start when in range
	// but can continue also when goes out of range
	if (!scrollingOut || scrolling)
		scrollTimer.restart();

	if (!scrollingOut || justSwitched)
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
