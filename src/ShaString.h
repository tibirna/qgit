#ifndef SHASTRING_H
#define SHASTRING_H

#include <QLatin1String>

class ShaString : public QLatin1String {

public:
	inline ShaString() : QLatin1String(NULL) {}
	inline ShaString(const ShaString& sha) : QLatin1String(sha.latin1()) {}
	inline explicit ShaString(const char *s) : QLatin1String(s) {}

	inline ShaString& operator=(const ShaString &other) {

		QLatin1String::operator=(other);
		return *this;
	}

	ShaString(const QString& sha) : QLatin1String(sha.toLatin1().constData()) {}
};

inline const ShaString toSha(const QString& sha) {

	return ShaString(sha.toLatin1().constData());
}

uint qHash(const ShaString& s);

#endif
