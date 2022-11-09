/*
        Description: interface to git programs

        Author: Marco Costalba (C) 2005-2007

        Copyright: See COPYING file that comes with this distribution

*/

#ifndef FILEHISTORY_H
#define FILEHISTORY_H

#include <QAbstractItemModel>
#include "common.h"

//class Annotate;
//class DataLoader;
class Git;
class Lanes;

class FileHistory : public QAbstractItemModel
{
  Q_OBJECT
public:
  FileHistory(QObject* parent, Git* git);
  ~FileHistory();
  void clear(bool complete = true);
  const QString sha(int row) const;
  int row(SCRef sha) const;
  const QStringList fileNames() const { return fNames; }
  void resetFileNames(SCRef fn);
  void setEarlyOutputState(bool b = true) { earlyOutputCnt = (b ? earlyOutputCntBase : -1); }
  void setAnnIdValid(bool b = true) { annIdValid = b; }

  virtual QVariant data(const QModelIndex &index, int role) const override;
  virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
  virtual QVariant headerData(int s, Qt::Orientation o, int role = Qt::DisplayRole) const override;
  virtual QModelIndex index(int r, int c, const QModelIndex& par = QModelIndex()) const override;
  virtual QModelIndex parent(const QModelIndex& index) const override;
  virtual int rowCount(const QModelIndex& par = QModelIndex()) const override;
  virtual bool hasChildren(const QModelIndex& par = QModelIndex()) const override;
  virtual int columnCount(const QModelIndex&) const override { return 6; }

public slots:
  void on_changeFont(const QFont&);

private slots:
  void on_newRevsAdded(const FileHistory*, const QVector<ShaString>&);
  void on_loadCompleted(const FileHistory*, const QString&);

private:
  friend class Annotate;
  friend class DataLoader;
  friend class Git;

  void flushTail();
  const QString timeDiff(unsigned long secs) const;

  Git* git;
  RevMap revs;
  ShaVect revOrder;
  Lanes* lns;
  uint firstFreeLane;
  QList<QByteArray*> rowData;
  QList<QVariant> headerInfo;
  int rowCnt;
  bool annIdValid;
  unsigned long secs;
  int loadTime;
  int earlyOutputCnt;
  int earlyOutputCntBase;
  QStringList fNames;
  QStringList curFNames;
  QStringList renamedRevs;
  QHash<QString, QString> renamedPatches;
  
};

#endif // FILEHISTORY_H
