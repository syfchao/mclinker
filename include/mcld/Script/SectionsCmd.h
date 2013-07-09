//===- SectionsCmd.h ------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_SECTIONS_COMMAND_INTERFACE_H
#define MCLD_SECTIONS_COMMAND_INTERFACE_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

#include <mcld/Script/ScriptCommand.h>
#include <llvm/Support/DataTypes.h>
#include <vector>

namespace mcld
{

/** \class SectionsCmd
 *  \brief This class defines the interfaces to SECTIONS command.
 */

class SectionsCmd : public ScriptCommand
{
public:
  typedef std::vector<ScriptCommand*> SectionCommands;
  typedef SectionCommands::const_iterator const_iterator;
  typedef SectionCommands::iterator iterator;
  typedef SectionCommands::const_reference const_reference;
  typedef SectionCommands::reference reference;

public:
  SectionsCmd();
  ~SectionsCmd();

  const_iterator  begin() const { return m_SectionCommands.begin(); }
  iterator        begin()       { return m_SectionCommands.begin(); }
  const_iterator  end()   const { return m_SectionCommands.end(); }
  iterator        end()         { return m_SectionCommands.end(); }

  const_reference front() const { return m_SectionCommands.front(); }
  reference       front()       { return m_SectionCommands.front(); }
  const_reference back()  const { return m_SectionCommands.back(); }
  reference       back()        { return m_SectionCommands.back(); }

  size_t size() const { return m_SectionCommands.size(); }

  bool empty() const { return m_SectionCommands.empty(); }

  void dump() const;

  static bool classof(const ScriptCommand* pCmd)
  {
    return pCmd->getKind() == ScriptCommand::SECTIONS;
  }

  void activate();

  void push_back(ScriptCommand* pCommand);

private:
  SectionCommands m_SectionCommands;
};

} // namespace of mcld

#endif
