#pragma once
#include <QDialog>
#include <QMap>

class QValidator;
class QPushButton;
namespace QGit {

/** create an input dialog from a command containing tokens of the form
 * %<widget_type>[options]:<widget name>=<default value>%
 * For default values, variables of the form $VAR_NAME can be used.
 * For each of those tokens, an input widget is created.
 * Supported widgets include: lineedit, combobox, textedit
 * options include:
 * - [ref]: enable ref name validation
 * - [editable]: ediable combobox
 */
class InputDialog : public QDialog
{
	Q_OBJECT
	struct WidgetItem {
		WidgetItem();
		void init(QWidget* w, const char *name);
		const char *prop_name; // property name
		QWidget *widget;
		int start, end;
	};
	typedef QSharedPointer<WidgetItem> WidgetItemPtr;
	typedef QMap<QString, WidgetItemPtr> WidgetMap;

	// map from token names to
	WidgetMap widgets;
	QString cmd;
	QMap<QString, const QValidator*> validators;
	QPushButton *okButton;

public:
	typedef QMap<QString, QVariant> VariableMap;
	explicit InputDialog(const QString &cmd, const VariableMap &variables,
	                     const QString &title="",
	                     QWidget *parent = 0, Qt::WindowFlags f = Qt::WindowFlags());

	/// any widgets defined?
	bool empty() const {return widgets.empty();}

	/// retrieve widget of given token
	QWidget *widget(const QString &token);
	/// retrieve value of given token
	QVariant value(const QString &token) const;
	/// replace all tokens in cmd by their values
	QString replace(const VariableMap &variables) const;

public Q_SLOTS:
	virtual bool validate();
};

} // namespace QGit
