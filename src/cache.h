/*
    Author: Marco Costalba (C) 2005-2007

    Copyright: See COPYING file that comes with this distribution

*/
#ifndef CACHE_H
#define CACHE_H

#include "git.h"

class Cache : public QObject {
Q_OBJECT
public:
    explicit Cache(QObject* par) : QObject(par) {}
    static bool save(const QString& gitDir, const RevFileMap& rf,
                     const StrVect& dirs, const StrVect& files);
    static bool load(const QString& gitDir, RevFileMap& rf,
                     StrVect& dirs, StrVect& files);
};

#endif
