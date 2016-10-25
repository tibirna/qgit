#pragma once

#include <QtCore>

// Class ShaString  is necessary for overloading qHash() function.
class ShaString : public QString {
public:
    ShaString() = default;
    ShaString(const ShaString&) = default;
    ShaString& operator= (const ShaString&) = default;
    ShaString(const QString& s) : QString(s) {}
    ShaString& operator= (const QString& s) {

        QString::operator= (s);
        return *this;
    }
};

inline bool operator== (const ShaString& s1, const QString& s2) {
    return ((QString)s1 == s2);
}
inline bool operator!= (const ShaString& s1, const QString& s2) {
    return !(s1 == s2);
}

typedef QVector<QString>    StrVect;
typedef QVector<ShaString>  ShaVect;
typedef QSet<QString>       ShaSet;
