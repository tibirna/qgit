/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef PATCHVIEW_H
#define PATCHVIEW_H

#include "ui_patchview.h"
#include "domain.h"

class Git;

class PatchView :public Domain {
Q_OBJECT
public:
	PatchView() {}
	PatchView(MainImpl* mi, Git* g);
	~PatchView();
	void clear(bool complete = true) override;
	Ui_TabPatch* tab() { return patchTab; }

signals:
	void diffTo(const QString&);
	void diffViewerDocked();

public slots:
	void on_updateRevDesc();
	void lineEditDiff_returnPressed();
	void button_clicked(int);
	void buttonFilterPatch_clicked();

protected slots:
	virtual void on_contextMenu(const QString&, int) override;

protected:
	virtual bool doUpdate(bool force) override;

private:
	void updatePatch();
	void saveRestoreSizes(bool startup = false);

	Ui_TabPatch* patchTab;
	QString normalizedSha;

	enum ButtonId {
		DIFF_TO_PARENT = 0,
		DIFF_TO_HEAD   = 1,
		DIFF_TO_SHA    = 2
	};
};

#endif
