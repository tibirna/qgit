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
    bool operator== (const QString& o) const {return QString::operator== (o);}
    bool operator!= (const QString& o) const {return !operator== (o);}
};

typedef QVector<QString>    StrVect;
typedef QVector<ShaString>  ShaVect;
typedef QSet<QString>       ShaSet;
