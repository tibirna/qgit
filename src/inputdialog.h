#pragma once
#include <QDialog>
#include <QMap>

namespace QGit {

/** create an input dialog from a command containing tokens of the form
 * %<widget_type>:<widget name>=<default value>%
 * For default values, variables of the form $VAR_NAME can be used.
 * For each of those tokens, an input widget is created.
 */
class InputDialog : public QDialog
{
	Q_OBJECT
	struct WidgetItem;
	typedef QSharedPointer<WidgetItem> WidgetItemPtr;
	typedef QMap<QString, WidgetItemPtr> WidgetMap;

	// map from token names to
	WidgetMap widgets;
	QString cmd;

public:
	typedef QMap<QString, QVariant> VariableMap;
	explicit InputDialog(const QString &cmd, const VariableMap &variables,
	                     const QString &title="",
	                     QWidget *parent = 0, Qt::WindowFlags f = 0);

	/// retrieve value of given token
	QVariant value(const QString &token) const;
	/// replace all tokens in cmd by their values
	QString replace() const;
};

} // namespace QGit
