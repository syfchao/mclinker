//===- MCLDDriver.h --------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MCLDDriver plays the same role as GNU collect2 to prepare all implicit
// parameters for MCLinker.
//
//===----------------------------------------------------------------------===//

#ifndef MCLD_LDDRIVER_H
#define MCLD_LDDRIVER_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

//FIXME: The implementation of MCLDDriver is currently
//       in SectLinker::doInitialization. those code should
//       be migrated here.
namespace mcld {
class MCLDDriver {

  /// linkable - check the linkability of current MCLDInfo
  ///  Check list:
  ///  - check the Attributes are not violate the constaint
  ///  - check every Input has a correct Attribute
  bool linkable() const;

  /// initMCLinker - initialize MCLinker
  ///  Connect all components in MCLinker
  bool initMCLinker();

  /// readSections - read all input section headers
  bool readSections();

  /// mergeSections - put allinput sections into output sections
  bool mergeSections();

  /// readSymbolTables - read symbol tables from the input files.
  ///  for each input file, loads its symbol table from file.
  bool readSymbolTables();

  /// mergeSymbolTables - merge the symbol tables of input files into the
  /// output's symbol table.
  bool mergeSymbolTables();

  /// addStandardSymbols - shared object and executable files need some
  /// standard symbols
  ///   @return if there are some input symbols with the same name to the
  ///   standard symbols, return false
  bool addStandardSymbols();

  /// addTargetSymbols - some targets, such as MIPS and ARM, need some
  /// target-dependent symbols
  ///   @return if there are some input symbols with the same name to the
  ///   target symbols, return false
  bool addTargetSymbols();

  /// readRelocations - read all relocation entries
  bool readRelocations();

  /// layout - linearly layout all output sections and reserve some space
  /// for GOT/PLT
  ///   Because we do not support instruction relaxing in this early version,
  ///   if there is a branch can not jump to its target, we return false
  ///   directly
  bool layout();

  /// relocate - applying relocation entries and create relocation
  /// section in the output files
  /// Create relocation section, asking TargetLDBackend to
  /// read the relocation information into RelocationEntry
  /// and push_back into the relocation section
  bool relocate();

  /// emitOutput - emit the output file.
  bool emitOutput();

private:
  TargetLDBackend &m_LDBackend;
  MCLDInfo& m_LDInfo;
};

} // end namespace mcld
#endif
