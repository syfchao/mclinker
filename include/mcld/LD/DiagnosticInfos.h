//===- DiagnosticInfo.h ---------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_DIAGNOSTIC_INFORMATION_H
#define MCLD_DIAGNOSTIC_INFORMATION_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

namespace mcld {

namespace diag {
  enum ID {
#define DIAG(ENUM, CLASS, ADDRMSG, LINEMSG) ENUM,
#include "mcld/LD/DiagCommonKinds.inc"
#include "mcld/LD/DiagSymbolResolutions.inc"
#include "mcld/LD/DiagRelocations.inc"
#undef DIAG
    NUM_OF_BUILDIN_DIAGNOSTIC_INFO
  };
} // namespace of diag

class MCLDInfo;
class DiagnosticEngine;

/** \class DiagnosticInfos
 *  \brief DiagnosticInfos caches run-time information of DiagnosticInfo.
 */
class DiagnosticInfos
{
public:
  DiagnosticInfos(const MCLDInfo& pLDInfo);

  ~DiagnosticInfos();

  bool process(DiagnosticEngine& pEngine) const;

private:
  const MCLDInfo& m_LDInfo;
};

} // namespace of mcld

#endif
