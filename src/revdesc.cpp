/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution
*/
#include <QApplication>
#include <QMenu>
#include <QContextMenuEvent>
#include <QRegExp>
#include <QClipboard>
#include "defmac.h"
#include "domain.h"
#include "revdesc.h"

RevDesc::RevDesc(QWidget* p) : QTextBrowser(p), d(NULL) {

	chk_connect_a(this, SIGNAL(anchorClicked(const QUrl&)),
                  this, SLOT(on_anchorClicked(const QUrl&)));

	chk_connect_a(this, SIGNAL(highlighted(const QUrl&)),
                  this, SLOT(on_highlighted(const QUrl&)));
}

void RevDesc::on_anchorClicked(const QUrl& link) {

	QRegExp re("[0-9a-f]{40}", Qt::CaseInsensitive);
	if (re.exactMatch(link.toString())) {

		setSource(QUrl()); // override default navigation behavior
		d->st.setSha(link.toString());
		UPDATE_DOMAIN(d);
	}
}

void RevDesc::on_highlighted(const QUrl& link) {

	highlightedLink = link.toString();
}

void RevDesc::on_linkCopy() {

	QClipboard* cb = QApplication::clipboard();
	cb->setText(highlightedLink);
}

void RevDesc::contextMenuEvent(QContextMenuEvent* e) {

	QMenu* menu = createStandardContextMenu();
	if (!highlightedLink.isEmpty()) {
		QAction* act = menu->addAction("Copy link SHA1");
		chk_connect_a(act, SIGNAL(triggered()), this, SLOT(on_linkCopy()));
	}
	menu->exec(e->globalPos());
	delete menu;
}
