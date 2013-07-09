//===- ScriptCommand.h ----------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_SCRIPT_COMMAND_INTERFACE_H
#define MCLD_SCRIPT_COMMAND_INTERFACE_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

namespace mcld
{

/** \class ScriptCommand
 *  \brief This class defines the interfaces to a script command.
 */

class ScriptCommand
{
public:
  enum Kind {
    ENTRY,
    OUTPUT_FORMAT,
    GROUP,
    OUTPUT,
    SEARCH_DIR,
    OUTPUT_ARCH,
    ASSERT,
    ASSIGNMENT,
    SECTIONS,
    OUTPUT_SECT_DESC,
    INPUT_SECT_DESC
  };

protected:
  ScriptCommand(Kind pKind)
    : m_Kind(pKind)
  {}

public:
  virtual ~ScriptCommand()
  {}

  virtual void dump() const = 0;

  virtual void activate() = 0;

  Kind getKind() const { return m_Kind; }

private:
  Kind m_Kind;
};

} // namespace of mcld

#endif

