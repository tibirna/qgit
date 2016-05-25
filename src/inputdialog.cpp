#include "inputdialog.h"
#include "common.h"
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>

namespace QGit {

struct InputDialog::WidgetItem {
	WidgetItem() : widget(NULL) {}
	void init(QWidget* w, const char *name) {
		widget = w;
		prop_name = name;
	}

	const char *prop_name; // property name
	QWidget *widget;
	int start, end;
};

InputDialog::InputDialog(const QString &cmd, const DefaultsMap &defaults, QWidget *parent, Qt::WindowFlags f)
	: cmd(cmd)
{
	QGridLayout *layout = new QGridLayout(this);

	QRegExp re("%([a-z]+:)?([^%]+)%");
	int start = 0;
	int row = 0;
	while ((start = re.indexIn(cmd, start)) != -1) {
		const QString type = re.cap(1);
		const QString name = re.cap(2);
		if (widgets.count(name)) { // widget already created
			if (!type.isEmpty()) dbs("token must not be redefined: " + name);
			continue;
		}

		WidgetItemPtr item (new WidgetItem());
		item->start = start;
		item->end = start = start + re.matchedLength();

		if (type == "combobox") {
			QComboBox *w = new QComboBox(this);
			w->addItems(defaults.value(name, QStringList()).toStringList());
			item->init(w, "currentText");
		} else if (type == "lineedit" || type == "") {
			QLineEdit *w = new QLineEdit(this);
			w->setText(defaults.value(name, QString()).toString());
			item->init(w, "text");
		} else {
			dbs("unknown widget type: " + type);
			continue;
		}
		widgets.insert(name, item);
		layout->addWidget(new QLabel(name), row, 0);
		layout->addWidget(item->widget, row, 1);
		++row;
	}
	QDialogButtonBox *buttons =	new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons, row, 0, 1, 2);

	connect(buttons->button(QDialogButtonBox::Ok), SIGNAL(pressed()), this, SLOT(accept()));
	connect(buttons->button(QDialogButtonBox::Cancel), SIGNAL(pressed()), this, SLOT(reject()));
}

QVariant InputDialog::value(const QString &token) const
{
	WidgetItemPtr item = widgets.value(token);
	if (!item) {
		dbs("unknown token: " + token);
		return QString();
	}
	return item->widget->property(item->prop_name);
}

QString InputDialog::replace() const
{
	QString result = cmd;
	for (WidgetMap::const_iterator it = widgets.begin(), end = widgets.end(); it != end; ++it) {
		QString token = "%" + it.key() + "%";
		WidgetItemPtr item = it.value();
		QString value = item->widget->property(item->prop_name).toString();
		result.replace(item->start, item->end - item->start, value); // replace main token
		result.replace(token, value); // replace all other occurences of %name%
	}
	return result;
}

} // namespace QGit
