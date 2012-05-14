//===- MipsRelocationFactory.cpp  -----------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <llvm/ADT/Twine.h>
#include <llvm/Support/ELF.h>
#include <llvm/Support/ErrorHandling.h>
#include <mcld/LD/Layout.h>
#include <mcld/Target/OutputRelocSection.h>
#include <mcld/Support/MsgHandling.h>

#include "MipsRelocationFactory.h"
#include "MipsRelocationFunctions.h"

using namespace mcld;

DECL_MIPS_APPLY_RELOC_FUNCS

//==========================
// MipsRelocationFactory
MipsRelocationFactory::MipsRelocationFactory(size_t pNum,
                                             MipsGNULDBackend& pParent)
  : RelocationFactory(pNum),
    m_Target(pParent),
    m_AHL(0)
{
}

void MipsRelocationFactory::applyRelocation(Relocation& pRelocation,
                                            const MCLDInfo& pLDInfo)

{
  /// the prototype of applying function
  typedef Result (*ApplyFunctionType)(Relocation&,
                                      const MCLDInfo& pLDInfo,
                                      MipsRelocationFactory&);

  // the table entry of applying functions
  struct ApplyFunctionTriple {
    ApplyFunctionType func;
    unsigned int type;
    const char* name;
  };

  // declare the table of applying functions
  static ApplyFunctionTriple apply_functions[] = {
    DECL_MIPS_APPLY_RELOC_FUNC_PTRS
  };

  Relocation::Type type = pRelocation.type();

  if (type >= sizeof(apply_functions) / sizeof(apply_functions[0])) {
    fatal(diag::unknown_relocation) << (int)type
                                    << pRelocation.symInfo()->name();
  }

  // apply the relocation
  Result result = apply_functions[type].func(pRelocation, pLDInfo, *this);

  // check result
  if (OK == result) {
    return;
  }
  if (Overflow == result) {
    error(diag::result_overflow) << apply_functions[type].name
                                 << pRelocation.symInfo()->name();
    return;
  }

  if (BadReloc == result) {
    error(diag::result_badreloc) << apply_functions[type].name
                                 << pRelocation.symInfo()->name();
    return;
  }
}

//=========================================//
// Relocation helper function              //
//=========================================//

static const char * const GP_DISP_NAME = "_gp_disp";

// Get an relocation entry in .rel.dyn and set its type to R_MIPS_REL32,
// its FragmentRef to pReloc->targetFrag() and its ResolveInfo
// to pReloc->symInfo()
static
void helper_SetRelDynEntry(Relocation& pReloc,
                   MipsRelocationFactory& pParent)
{
  // rsym - The relocation target symbol
  ResolveInfo* rsym = pReloc.symInfo();
  MipsGNULDBackend& ld_backend = pParent.getTarget();

  bool exist;
  Relocation& rel_entry =
    *ld_backend.getRelDyn().getEntry(*rsym, false, exist);

  rel_entry.setType(llvm::ELF::R_MIPS_REL32);
  rel_entry.targetRef() = pReloc.targetRef();
  rel_entry.setSymInfo(0);
}

// Find next R_MIPS_LO16 relocation paired to pReloc.
static
Relocation* helper_FindLo16Reloc(Relocation& pReloc)
{
  Relocation* reloc = static_cast<Relocation*>(pReloc.getNextNode());
  while (NULL != reloc)
  {
    if (llvm::ELF::R_MIPS_LO16 == reloc->type() &&
        reloc->symInfo() == pReloc.symInfo())
      return reloc;

    reloc = static_cast<Relocation*>(reloc->getNextNode());
  }
  return NULL;
}

// Check the symbol is _gp_disp.
static
bool helper_isGpDisp(const Relocation& pReloc)
{
  const ResolveInfo* rsym = pReloc.symInfo();
  return 0 == strcmp(GP_DISP_NAME, rsym->name());
}

static
RelocationFactory::Address helper_GetGP(MipsRelocationFactory& pParent)
{
  return pParent.getTarget().getGOT().getSection().addr() + 0x7FF0;
}

static
GOTEntry& helper_GetGOTEntry(Relocation& pReloc,
                             MipsRelocationFactory& pParent,
                             bool& pExist, int32_t value)
{
  // rsym - The relocation target symbol
  ResolveInfo* rsym = pReloc.symInfo();
  MipsGNULDBackend& ld_backend = pParent.getTarget();
  MipsGOT& got = ld_backend.getGOT();

  GOTEntry& got_entry = *got.getEntry(*rsym, pExist);

  if (pExist)
    return got_entry;

  // If we first get this GOT entry, we should initialize it.
  if (!(got.isLocal(rsym) && rsym->type() == ResolveInfo::Section)) {
    if (rsym->reserved() & MipsGNULDBackend::ReserveGot) {
      got_entry.setContent(pReloc.symValue());
    }
    else {
      fatal(diag::reserve_entry_number_mismatch) << "GOT";
    }
  }

  return got_entry;
}

static
RelocationFactory::Address helper_GetGOTOffset(Relocation& pReloc,
                                               MipsRelocationFactory& pParent)
{
  bool exist;
  GOTEntry& got_entry = helper_GetGOTEntry(pReloc, pParent, exist, 0);
  return pParent.getLayout().getOutputOffset(got_entry) - 0x7FF0;
}

static
int32_t helper_CalcAHL(const Relocation& pHiReloc, const Relocation& pLoReloc)
{
  assert((pHiReloc.type() == llvm::ELF::R_MIPS_HI16 ||
          pHiReloc.type() == llvm::ELF::R_MIPS_GOT16) &&
         pLoReloc.type() == llvm::ELF::R_MIPS_LO16 &&
         "Incorrect type of relocation for AHL calculation");

  // Note the addend is section symbol offset here
  assert (pHiReloc.addend() == pLoReloc.addend());

  int32_t AHI = pHiReloc.target();
  int32_t ALO = pLoReloc.target();
  int32_t AHL = ((AHI & 0xFFFF) << 16) + (int16_t)(ALO & 0xFFFF) + pLoReloc.addend();
  return AHL;
}

static
void helper_DynRel(Relocation& pReloc,
                   MipsRelocationFactory& pParent)
{
  ResolveInfo* rsym = pReloc.symInfo();
  MipsGNULDBackend& ld_backend = pParent.getTarget();
  MipsGOT& got = ld_backend.getGOT();

  bool exist;
  Relocation& rel_entry =
    *ld_backend.getRelDyn().getEntry(*rsym, false, exist);

  rel_entry.setType(llvm::ELF::R_MIPS_REL32);
  rel_entry.targetRef() = pReloc.targetRef();

  RelocationFactory::DWord A = pReloc.target() + pReloc.addend();
  RelocationFactory::DWord S = pReloc.symValue();

  if (got.isLocal(rsym)) {
    rel_entry.setSymInfo(NULL);
    pReloc.target() = A + S;
  }
  else {
    rel_entry.setSymInfo(rsym);
    // Don't add symbol value that will be resolved by the dynamic linker
    pReloc.target() = A;
  }
}

//=========================================//
// Relocation functions implementation     //
//=========================================//

// R_MIPS_NONE and those unsupported/deprecated relocation type
static
MipsRelocationFactory::Result none(Relocation& pReloc,
                                   const MCLDInfo& pLDInfo,
                                   MipsRelocationFactory& pParent)
{
  return MipsRelocationFactory::OK;
}

// R_MIPS_32: S + A
static
MipsRelocationFactory::Result abs32(Relocation& pReloc,
                                    const MCLDInfo& pLDInfo,
                                    MipsRelocationFactory& pParent)
{
  ResolveInfo* rsym = pReloc.symInfo();

  if (rsym->reserved() & MipsGNULDBackend::ReserveRel) {
    helper_DynRel(pReloc, pParent);

    return MipsRelocationFactory::OK;
  }

  RelocationFactory::DWord A = pReloc.target() + pReloc.addend();
  RelocationFactory::DWord S = pReloc.symValue();

  pReloc.target() = (S + A);

  return MipsRelocationFactory::OK;
}

// R_MIPS_HI16:
//   local/external: ((AHL + S) - (short)(AHL + S)) >> 16
//   _gp_disp      : ((AHL + GP - P) - (short)(AHL + GP - P)) >> 16
static
MipsRelocationFactory::Result hi16(Relocation& pReloc,
                                   const MCLDInfo& pLDInfo,
                                   MipsRelocationFactory& pParent)
{
  Relocation* lo_reloc = helper_FindLo16Reloc(pReloc);
  assert(NULL != lo_reloc && "There is no paired R_MIPS_LO16 for R_MIPS_HI16");

  int32_t AHL = helper_CalcAHL(pReloc, *lo_reloc);
  int32_t res = 0;

  pParent.setAHL(AHL);

  if (helper_isGpDisp(pReloc)) {
    int32_t P = pReloc.place(pParent.getLayout());
    int32_t GP = helper_GetGP(pParent);
    res = ((AHL + GP - P) - (int16_t)(AHL + GP - P)) >> 16;
  }
  else {
    int32_t S = pReloc.symValue();
    res = ((AHL + S) - (int16_t)(AHL + S)) >> 16;
  }

  pReloc.target() &= 0xFFFF0000;
  pReloc.target() |= (res & 0xFFFF);

  return MipsRelocationFactory::OK;
}

// R_MIPS_LO16:
//   local/external: AHL + S
//   _gp_disp      : AHL + GP - P + 4
static
MipsRelocationFactory::Result lo16(Relocation& pReloc,
                                   const MCLDInfo& pLDInfo,
                                   MipsRelocationFactory& pParent)
{
  int32_t res = 0;

  if (helper_isGpDisp(pReloc)) {
    int32_t P = pReloc.place(pParent.getLayout());
    int32_t GP = helper_GetGP(pParent);
    int32_t AHL = pParent.getAHL();
    res = AHL + GP - P + 4;
  }
  else {
    int32_t S = pReloc.symValue();
    // The previous AHL may be for other hi/lo pairs.
    // We need to calcuate the lo part now.  It is easy.
    // Remember to add the section offset to ALO.
    int32_t ALO = (pReloc.target() & 0xFFFF) + pReloc.addend();
    res = ALO + S;
  }

  pReloc.target() &= 0xFFFF0000;
  pReloc.target() |= (res & 0xFFFF);

  return MipsRelocationFactory::OK;
}

// R_MIPS_GOT16:
//   local   : G (calculate AHL and put high 16 bit to GOT)
//   external: G
static
MipsRelocationFactory::Result got16(Relocation& pReloc,
                                    const MCLDInfo& pLDInfo,
                                    MipsRelocationFactory& pParent)
{
  ResolveInfo* rsym = pReloc.symInfo();
  RelocationFactory::Address G = 0;

  if (rsym->isLocal()) {
    Relocation* lo_reloc = helper_FindLo16Reloc(pReloc);
    assert(NULL != lo_reloc && "There is no paired R_MIPS_LO16 for R_MIPS_GOT16");

    int32_t AHL = helper_CalcAHL(pReloc, *lo_reloc);
    int32_t S = pReloc.symValue();

    pParent.setAHL(AHL);

    int32_t res = (AHL + S + 0x8000) & 0xFFFF0000;
    bool exist;
    GOTEntry& got_entry = helper_GetGOTEntry(pReloc, pParent, exist, res);

    got_entry.setContent(res);
    G = pParent.getLayout().getOutputOffset(got_entry) - 0x7FF0;
  }
  else {
    G = helper_GetGOTOffset(pReloc, pParent);
  }

  pReloc.target() &= 0xFFFF0000;
  pReloc.target() |= (G & 0xFFFF);

  return MipsRelocationFactory::OK;
}

// R_MIPS_CALL16: G
static
MipsRelocationFactory::Result call16(Relocation& pReloc,
                                     const MCLDInfo& pLDInfo,
                                     MipsRelocationFactory& pParent)
{
  RelocationFactory::Address G = helper_GetGOTOffset(pReloc, pParent);

  pReloc.target() &= 0xFFFF0000;
  pReloc.target() |= (G & 0xFFFF);

  return MipsRelocationFactory::OK;
}

// R_MIPS_GPREL32: A + S + GP0 - GP
static
MipsRelocationFactory::Result gprel32(Relocation& pReloc,
                                      const MCLDInfo& pLDInfo,
                                      MipsRelocationFactory& pParent)
{
  // Remember to add the section offset to A.
  int32_t A = pReloc.target() + pReloc.addend();
  int32_t S = pReloc.symValue();
  int32_t GP = helper_GetGP(pParent);

  // llvm does not emits SHT_MIPS_REGINFO section.
  // Assume that GP0 is zero.
  pReloc.target() = (A + S - GP) & 0xFFFFFFFF;

  return MipsRelocationFactory::OK;
}
