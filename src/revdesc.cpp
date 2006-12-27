/*
	Author: Marco Costalba (C) 2005-2006

	Copyright: See COPYING file that comes with this distribution
*/
#include <qapplication.h>
#include <qregexp.h>
#include <q3popupmenu.h>
#include <q3action.h>
#include <qclipboard.h>
#include <qaction.h>
#include "common.h"
#include "domain.h"
#include "revdesc.h"

RevDesc::RevDesc(QWidget* p) : Q3TextBrowser(p), d(NULL) {

	setTextFormat(Qt::RichText);

	connect(this, SIGNAL(linkClicked(const QString&)),
	        this, SLOT(on_linkClicked(const QString&)));

	connect(this, SIGNAL(highlighted(const QString&)),
	        this, SLOT(on_highlighted(const QString&)));
}

void RevDesc::on_linkClicked(const QString& link) {

	QRegExp reSHA("[0-9a-f]{40}", false);
	if (link.find(reSHA) != -1) {

		setText(text()); // without this Qt warns on missing MIME source
		d->st.setSha(link);
		UPDATE_DOMAIN(d);
	}
}

void RevDesc::on_highlighted(const QString& link) {

	highlightedLink = link;
}

Q3PopupMenu* RevDesc::createPopupMenu(const QPoint& pos) {

	Q3PopupMenu* popup = Q3TextBrowser::createPopupMenu(pos);

	if (highlightedLink.isEmpty())
		return popup;

	Q3Action* act = new Q3Action("Copy link sha1", 0, popup);
	connect(act, SIGNAL(activated()), this, SLOT(on_linkCopy()));
	act->addTo(popup);
	return popup;
}

void RevDesc::on_linkCopy() {

	QClipboard* cb = QApplication::clipboard();
	cb->setText(highlightedLink, QClipboard::Clipboard);
}
