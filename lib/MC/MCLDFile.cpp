/*****************************************************************************
 *   The MCLinker Project, Copyright (C), 2011 -                             *
 *   Embedded and Web Computing Lab, National Taiwan University              *
 *   MediaTek, Inc.                                                          *
 *                                                                           *
 *   Duo <pinronglu@gmail.com>                                               *
 ****************************************************************************/
#include <mcld/MC/MCLDFile.h>
#include <mcld/MC/MCLDContext.h>
#include <mcld/Support/FileSystem.h>
#include <mcld/Support/MemoryArea.h>
#include <cstring>
#include <cstdlib>

using namespace mcld;

//===----------------------------------------------------------------------===//
// MCLDFile
MCLDFile::MCLDFile()
  : m_Type(Unknown), m_pContext(0), m_Path(), m_InputName(), m_pMemArea(0) {
}

MCLDFile::MCLDFile(llvm::StringRef pName)
  : m_Type(Unknown), m_pContext(0), m_Path(), m_InputName(pName.data()), m_pMemArea(0) {
}

MCLDFile::MCLDFile(llvm::StringRef pName,
                   const sys::fs::Path& pPath,
                   unsigned int pType)
  : m_Type(pType), m_pContext(0), m_Path(pPath), m_InputName(pName.data()), m_pMemArea(0) {
}

MCLDFile::~MCLDFile()
{
}

const std::string& MCLDFile::name() const
{
  return m_InputName;
}

void MCLDFile::open()
{
  if (0 == m_pMemArea) {
    // FIXME: create and open file
  }
}

void MCLDFile::close()
{
  if (0 != m_pMemArea) {
    // FIXME: close memory area and delete it.
    m_pMemArea = 0;
  }
}

bool MCLDFile::isOpened() const
{
  if (0 == m_pMemArea)
    return false;
  // FIXME: check more
  return true;
}

bool MCLDFile::isGood() const
{
  if (0 == m_pMemArea)
    return false;
  // FIXME: check more
  return true;
}

