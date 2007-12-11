#ifndef SHASTRING_H
#define SHASTRING_H

#include <QLatin1String>

class ShaString : public QLatin1String {

public:
	inline ShaString() : QLatin1String(NULL) {}
	inline ShaString(const ShaString& sha) : QLatin1String(sha.latin1()) {}
	inline explicit ShaString(const char* sha) : QLatin1String(sha) {}

	inline bool operator!=(const ShaString& o) const { return !operator==(o); }
	inline bool operator==(const ShaString& o) const {

		return (latin1() == o.latin1()) || !qstrcmp(latin1(), o.latin1());
	}
};

/* Value returned by this function should be used only as function argument,
 * and not stored in a variable because 'ba' value is overwritten at each
 * call so the returned ShaString could became stale very quickly
 */
inline const ShaString toTempSha(const QString& sha) {

	static QByteArray ba;
	ba = sha.toLatin1();
	return ShaString(sha.isEmpty() ? NULL : ba.constData());
}

inline const ShaString toPersistentSha(const QString& sha, QVector<QByteArray>& v) {

	v.append(sha.toLatin1());
	return ShaString(v.last().constData());
}

uint qHash(const ShaString&); // optimized custom hash for sha strings

#endif
