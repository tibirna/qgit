#include "inputdialog.h"
#include "common.h"
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QCompleter>
#include <QListView>
#include <QStringListModel>

namespace QGit {

InputDialog::WidgetItem::WidgetItem() : widget(NULL)
{
}

void InputDialog::WidgetItem::init(QWidget* w, const char *name) {
	widget = w;
	prop_name = name;
}

QString parseString(const QString &value, const InputDialog::VariableMap &vars) {
	if (value.startsWith('$')) return vars.value(value.mid(1), QString()).toString();
	else return value;
}
QStringList parseStringList(const QString &value, const InputDialog::VariableMap &vars) {
	QStringList values = value.split(',');
	QStringList result;
	for (QStringList::iterator it=values.begin(), end=values.end(); it!=end; ++it) {
		if (it->startsWith('$')) result.append(vars.value(value.mid(1), QStringList()).toStringList());
		else result.append(*it);
	}
	return result;
}

class RefNameValidator : public QValidator {
public:
	RefNameValidator(bool allowEmpty=false, QObject *parent=0)
	    : QValidator(parent)
	    , invalid("[ ~^:\?*[]")
	    , allowEmpty(allowEmpty)
	{}

	void fixup(QString& input) const;
	State validate(QString & input, int & pos) const;
private:
	const QRegExp invalid;
	bool allowEmpty;
};

void RefNameValidator::fixup(QString &input) const
{
	// remove invalid chars
	input.replace(invalid, "");
	input.replace("/.","/"); // no dot after slash
	input.replace("..","."); // no two dots in a row
	input.replace("//","/"); // no two slashes in a row
	input.replace("@{", "@"); // no sequence @{
}

QValidator::State RefNameValidator::validate(QString &input, int &pos) const
{
	// https://www.kernel.org/pub/software/scm/git/docs/git-check-ref-format.html
	// automatically remove invalid chars
	QString front = input.left(pos); fixup(front);
	QString rear = input.mid(pos); fixup(rear);
	input = front + rear;
	// keep cursor were it was
	pos = front.length();

	QString fixed(input); fixup(fixed);
	if (fixed != input) return Invalid;

	// empty string or single @ are not allowed
	if ((input.isEmpty() && !allowEmpty) || input == "@")
		return Intermediate;
	return Acceptable;
}


InputDialog::InputDialog(const QString &cmd, const VariableMap &variables,
                         const QString &title, QWidget *parent, Qt::WindowFlags f)
    : QDialog(parent, f)
    , cmd(cmd)
{
	this->setWindowTitle(title);
	QGridLayout *layout = new QGridLayout(this);

	QRegExp re("%(([a-z_]+)([[]([a-z ,]+)[]])?:)?([^%=]+)(=[^%]+)?%");
	int start = 0;
	int row = 0;
	while ((start = re.indexIn(cmd, start)) != -1) {
		const QString type = re.cap(2);
		const QStringList opts = re.cap(4).split(',', QGIT_SPLITBEHAVIOR(SkipEmptyParts));
		const QString name = re.cap(5);
		const QString value = re.cap(6).mid(1);
		if (widgets.count(name)) { // widget already created
			if (!type.isEmpty()) dbs("token must not be redefined: " + name);
			continue;
		}

		WidgetItemPtr item (new WidgetItem());
		item->start = start;
		item->end = start = start + re.matchedLength();

		if (type == "combobox") {
			QComboBox *w = new QComboBox(this);
			w->addItems(parseStringList(value, variables));
			if (opts.contains("editable")) w->setEditable(true);
			w->setMinimumWidth(100);
			if (opts.contains("ref")) {
				w->setValidator(new RefNameValidator(opts.contains("empty")));
				validators.insert(name, w->validator());
				connect(w, SIGNAL(editTextChanged(QString)), this, SLOT(validate()));
			}
			item->init(w, "currentText");
		} else if (type == "listbox") {
			QListView *w = new QListView(this);
			w->setModel(new QStringListModel(parseStringList(value, variables)));
			item->init(w, NULL);
		} else if (type == "lineedit" || type == "") {
			QLineEdit *w = new QLineEdit(this);
			w->setText(parseString(value, variables));
			QStringList values = parseStringList(value, variables);
			if (!values.isEmpty()) // use default string list as
				w->setCompleter(new QCompleter(values));
			if (opts.contains("ref")) {
				w->setValidator(new RefNameValidator(opts.contains("empty")));
				validators.insert(name, w->validator());
				connect(w, SIGNAL(textEdited(QString)), this, SLOT(validate()));
			}
			item->init(w, "text");
		} else if (type == "textedit") {
			QTextEdit *w = new QTextEdit(this);
			w->setText(parseString(value, variables));
			item->init(w, "plainText");
		} else {
			dbs("unknown widget type: " + type);
			continue;
		}
		widgets.insert(name, item);
		if (name.startsWith('_')) { // _name triggers hiding of label
			layout->addWidget(item->widget, row, 1);
		} else {
			layout->addWidget(new QLabel(name + ":"), row, 0);
			layout->addWidget(item->widget, row, 1);
		}
		++row;
	}
	QDialogButtonBox *buttons =	new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons, row, 0, 1, 2);
	okButton = buttons->button(QDialogButtonBox::Ok);

	connect(okButton, SIGNAL(pressed()), this, SLOT(accept()));
	connect(buttons->button(QDialogButtonBox::Cancel), SIGNAL(pressed()), this, SLOT(reject()));
	validate();
}

QWidget *InputDialog::widget(const QString &token)
{
	WidgetItemPtr item = widgets.value(token);
	return item ? item->widget : NULL;
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

bool InputDialog::validate()
{
	bool result=true;
	for (QMap<QString, const QValidator*>::const_iterator
	     it=validators.begin(), end=validators.end(); result && it != end; ++it) {
		QString val = value(it.key()).toString();
		int pos=0;
		if (it.value()->validate(val, pos) != QValidator::Acceptable)
			result=false;
	}
	okButton->setEnabled(result);
	return result;
}

QString InputDialog::replace(const VariableMap &variables) const
{
	QString result = cmd;
	int shift = 0, start = 0, len = 0; // will keep track of position shifts during replacements
	for (WidgetMap::const_iterator it = widgets.begin(), end = widgets.end(); it != end; ++it) {
		QString token = "%" + it.key() + "%";
		WidgetItemPtr item = it.value();
		start = item->start - shift;
		len   = item->end - item->start;
		QString value = item->widget->property(item->prop_name).toString();
		result.replace(start, len, value); // replace main token
		shift += len - value.length();
		result.replace(token, value); // replace all other occurences of %name%
	}
	for (VariableMap::const_iterator it=variables.begin(), end=variables.end(); it != end; ++it) {
		QString token = "$" + it.key();
		QString val = it.value().type() == QVariant::StringList ? it.value().toStringList().join(" ")
		                                                        : it.value().toString();
		result.replace(token, val);
	}
	return result;
}

} // namespace QGit
