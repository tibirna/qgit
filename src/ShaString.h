#ifndef SHASTRING_H
#define SHASTRING_H

#include <QLatin1String>

class ShaString : public QLatin1String {

public:
	inline ShaString() : QLatin1String(NULL) {}
	inline ShaString(const ShaString& sha) : QLatin1String(sha.latin1()) {}
	inline explicit ShaString(const char *s) : QLatin1String(s) {}
};

inline const ShaString toSha(const QString& sha) {

	return ShaString(sha.toLatin1().constData());
}

uint qHash(const ShaString& s);

#endif
