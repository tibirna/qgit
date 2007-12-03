#ifndef SHASTRING_H
#define SHASTRING_H

class ShaString : public QString
{
public:
	ShaString() : QString() {}
	ShaString(const QString& s) : QString(s) {}
};
uint qHash(const ShaString& s);


#endif
