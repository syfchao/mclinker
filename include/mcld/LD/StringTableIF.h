/*****************************************************************************
 *   The MCLinker Project, Copyright (C), 2011 -                             *
 *   Embedded and Web Computing Lab, National Taiwan University              *
 *   MediaTek, Inc.                                                          *
 *                                                                           *
 *   Nowar Gu <nowar100@gmail.com>                                           *
 ****************************************************************************/
#ifndef LDSTRINGTABLEIF_H
#define LDSTRINGTABLEIF_H
#include <vector>
#include <mcld/LD/StringStorage.h>
#include <llvm/ADT/StringRef.h>
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

namespace mcld
{

/** \class StringTableIF
 *  \brief String table interface.
 *
 *  \see
 *  \author Nowar Gu <nowar100@gmail.com>
 */
class StringTableIF
{
protected:
  StringTableIF(StringStorage* pImpl)
  : f_Storage(pImpl) {}

public:
  typedef f_Track::const_iterator const_iterator;
  typedef f_Track::iterator iterator;

public:
  virtual ~StringTableIF() {}

  const char* insert(const char* pStr)
  {
    char* p = f_Storage->allocate(strlen(pStr)+1);
    f_Storage->construct(p, pStr);
    f_Track.push_back(p);
    return p;
  }

  size_t size() const { return f_Track.size(); }

  StringStorage* getStorage() const  { return f_Storage; }  // For unittest

protected:
  StringStorage* f_Storage;
  vector<const char*> f_Track;
};

} // namespace of mcld

#endif
