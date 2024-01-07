/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef REVSVIEW_H
#define REVSVIEW_H

#include <QPointer>
#include "ui_revsview.h" // needed by moc_* file to understand tab() function
#include "common.h"
#include "domain.h"

class MainImpl;
class Git;
class FileHistory;
class PatchView;

class RevsView : public Domain {
Q_OBJECT
public:
	RevsView(MainImpl* parent, Git* git, bool isMain = false);
	~RevsView();
	void clear(bool complete) override;
	void viewPatch(bool newTab);
	void setEnabled(bool b);
	void setTabLogDiffVisible(bool);
	Ui_TabRev* tab() { return revTab; }

public slots:
	void toggleDiffView();

private slots:
	void on_newRevsAdded(const FileHistory*, const QVector<ShaString>&);
	void on_loadCompleted(const FileHistory*, const QString& stats);
	void on_lanesContextMenuRequested(const QStringList&, const QStringList&);
	void on_updateRevDesc();
	void on_flagChanged(uint flag);

protected:
	virtual bool doUpdate(bool force) override;

private:
	friend class MainImpl;

	void updateLineEditSHA(bool clear = false);

	Ui_TabRev* revTab;
	QPointer<PatchView> linkedPatchView;
};

#endif
