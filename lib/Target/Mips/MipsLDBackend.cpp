//===- MipsLDBackend.cpp --------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "Mips.h"
#include "MipsELFDynamic.h"
#include "MipsLDBackend.h"
#include "MipsRelocationFactory.h"

#include <llvm/ADT/Triple.h>
#include <llvm/Support/ELF.h>

#include <mcld/Module.h>
#include <mcld/LinkerConfig.h>
#include <mcld/MC/Attribute.h>
#include <mcld/Fragment/FillFragment.h>
#include <mcld/Fragment/FragmentLinker.h>
#include <mcld/Support/MemoryRegion.h>
#include <mcld/Support/MemoryArea.h>
#include <mcld/Support/MsgHandling.h>
#include <mcld/Support/TargetRegistry.h>
#include <mcld/Target/OutputRelocSection.h>
#include <mcld/Object/ObjectBuilder.h>

enum {
  // The original o32 abi.
  E_MIPS_ABI_O32    = 0x00001000,
  // O32 extended to work on 64 bit architectures.
  E_MIPS_ABI_O64    = 0x00002000,
  // EABI in 32 bit mode.
  E_MIPS_ABI_EABI32 = 0x00003000,
  // EABI in 64 bit mode.
  E_MIPS_ABI_EABI64 = 0x00004000
};

using namespace mcld;

//===----------------------------------------------------------------------===//
// MipsGNULDBackend
//===----------------------------------------------------------------------===//
MipsGNULDBackend::MipsGNULDBackend(const LinkerConfig& pConfig)
  : GNULDBackend(pConfig),
    m_pRelocFactory(NULL),
    m_pGOT(NULL),
    m_pRelDyn(NULL),
    m_pDynamic(NULL),
    m_pGOTSymbol(NULL),
    m_pGpDispSymbol(NULL)
{
}

MipsGNULDBackend::~MipsGNULDBackend()
{
  // NOTE: we let iplist<Relocation> destroy relocations
  // delete m_pRelocFactory;
  if (NULL != m_pGOT)
    delete m_pGOT;
  if (NULL != m_pRelDyn)
    delete m_pRelDyn;
  if (NULL != m_pDynamic)
    delete m_pDynamic;
}

void MipsGNULDBackend::initTargetSections(Module& pModule, ObjectBuilder& pBuilder)
{
  if (LinkerConfig::Object != config().codeGenType()) {
    ELFFileFormat* file_format = getOutputFormat();

    // initialize .got
    LDSection& got = file_format->getGOT();
    m_pGOT = new MipsGOT(got, *ObjectBuilder::CreateSectionData(got));

    // initialize .rel.dyn
    LDSection& reldyn = file_format->getRelDyn();
    m_pRelDyn = new OutputRelocSection(pModule,
                                       reldyn,
                                       getRelEntrySize());
  }
}

void MipsGNULDBackend::initTargetSymbols(FragmentLinker& pLinker)
{
  // Define the symbol _GLOBAL_OFFSET_TABLE_ if there is a symbol with the
  // same name in input
  m_pGOTSymbol = pLinker.defineSymbol<FragmentLinker::AsRefered, FragmentLinker::Resolve>(
                   "_GLOBAL_OFFSET_TABLE_",
                   false,
                   ResolveInfo::Object,
                   ResolveInfo::Define,
                   ResolveInfo::Local,
                   0x0,  // size
                   0x0,  // value
                   FragmentRef::Null(), // FragRef
                   ResolveInfo::Hidden);

  m_pGpDispSymbol = pLinker.defineSymbol<FragmentLinker::AsRefered, FragmentLinker::Resolve>(
                   "_gp_disp",
                   false,
                   ResolveInfo::Section,
                   ResolveInfo::Define,
                   ResolveInfo::Absolute,
                   0x0,  // size
                   0x0,  // value
                   FragmentRef::Null(), // FragRef
                   ResolveInfo::Default);

  if (NULL != m_pGpDispSymbol) {
    m_pGpDispSymbol->resolveInfo()->setReserved(ReserveGpDisp);
  }
}

bool MipsGNULDBackend::initRelocFactory(const FragmentLinker& pLinker)
{
  if (NULL == m_pRelocFactory) {
    m_pRelocFactory = new MipsRelocationFactory(1024, *this);
    m_pRelocFactory->setFragmentLinker(pLinker);
  }
  return true;
}

RelocationFactory* MipsGNULDBackend::getRelocFactory()
{
  assert(NULL != m_pRelocFactory);
  return m_pRelocFactory;
}

void MipsGNULDBackend::scanRelocation(Relocation& pReloc,
                                      FragmentLinker& pLinker,
                                      Module& pModule,
                                      const LDSection& pSection)
{
  // rsym - The relocation target symbol
  ResolveInfo* rsym = pReloc.symInfo();
  assert(NULL != rsym && "ResolveInfo of relocation not set while scanRelocation");

  // Skip relocation against _gp_disp
  if (NULL != m_pGpDispSymbol) {
    if (pReloc.symInfo() == m_pGpDispSymbol->resolveInfo())
      return;
  }

  pReloc.updateAddend();

  if (0 == (pSection.flag() & llvm::ELF::SHF_ALLOC))
    return;

  // We test isLocal or if pInputSym is not a dynamic symbol
  // We assume -Bsymbolic to bind all symbols internaly via !rsym->isDyn()
  // Don't put undef symbols into local entries.
  if ((rsym->isLocal() || !isDynamicSymbol(*rsym) ||
      !rsym->isDyn()) && !rsym->isUndef())
    scanLocalReloc(pReloc, pLinker);
  else
    scanGlobalReloc(pReloc, pLinker);

  // check if we shoule issue undefined reference for the relocation target
  // symbol
  if (rsym->isUndef() && !rsym->isDyn() && !rsym->isWeak())
    fatal(diag::undefined_reference) << rsym->name();

  if ((rsym->reserved() & ReserveRel) != 0x0) {
    // set hasTextRelSection if needed
    checkAndSetHasTextRel(pSection);
  }
}

uint32_t MipsGNULDBackend::machine() const
{
  return llvm::ELF::EM_MIPS;
}

uint8_t MipsGNULDBackend::OSABI() const
{
  return llvm::ELF::ELFOSABI_NONE;
}

uint8_t MipsGNULDBackend::ABIVersion() const
{
  return 0;
}

uint64_t MipsGNULDBackend::flags() const
{
  // TODO: (simon) The correct flag's set depend on command line
  // arguments and flags from input .o files.
  return llvm::ELF::EF_MIPS_ARCH_32R2 |
         llvm::ELF::EF_MIPS_NOREORDER |
         llvm::ELF::EF_MIPS_PIC |
         llvm::ELF::EF_MIPS_CPIC |
         E_MIPS_ABI_O32;
}

bool MipsGNULDBackend::isLittleEndian() const
{
  // Now we support little endian (mipsel) target only.
  return true;
}

unsigned int MipsGNULDBackend::bitclass() const
{
  return 32;
}

uint64_t MipsGNULDBackend::defaultTextSegmentAddr() const
{
  return 0x80000;
}

uint64_t MipsGNULDBackend::abiPageSize() const
{
  if (config().options().maxPageSize() > 0)
    return config().options().maxPageSize();
  else
    return static_cast<uint64_t>(0x10000);
}

void MipsGNULDBackend::doPreLayout(FragmentLinker& pLinker)
{
  // set .got size
  // when building shared object, the .got section is must.
  if (LinkerConfig::Object != config().codeGenType()) {
    if (LinkerConfig::DynObj == config().codeGenType() ||
        m_pGOT->hasGOT1() ||
        NULL != m_pGOTSymbol) {
      m_pGOT->finalizeSectionSize();
      defineGOTSymbol(pLinker);
    }

    // set .rel.dyn size
    if (!m_pRelDyn->empty())
      m_pRelDyn->finalizeSectionSize();
  }
}
void MipsGNULDBackend::doPostLayout(Module& pModule,
                                    FragmentLinker& pLinker)
{
}

/// dynamic - the dynamic section of the target machine.
/// Use co-variant return type to return its own dynamic section.
MipsELFDynamic& MipsGNULDBackend::dynamic()
{
  if (NULL == m_pDynamic)
    m_pDynamic = new MipsELFDynamic(*this);

  return *m_pDynamic;
}

/// dynamic - the dynamic section of the target machine.
/// Use co-variant return type to return its own dynamic section.
const MipsELFDynamic& MipsGNULDBackend::dynamic() const
{
  assert( NULL != m_pDynamic);
  return *m_pDynamic;
}

uint64_t MipsGNULDBackend::emitSectionData(const LDSection& pSection,
                                           MemoryRegion& pRegion) const
{
  assert(pRegion.size() && "Size of MemoryRegion is zero!");

  const ELFFileFormat* file_format = getOutputFormat();

  if (&pSection == &(file_format->getGOT())) {
    assert(NULL != m_pGOT && "emitSectionData failed, m_pGOT is NULL!");
    uint64_t result = m_pGOT->emit(pRegion);
    return result;
  }

  fatal(diag::unrecognized_output_sectoin)
          << pSection.name()
          << "mclinker@googlegroups.com";
  return 0;
}
/// isGlobalGOTSymbol - return true if the symbol is the global GOT entry.
bool MipsGNULDBackend::isGlobalGOTSymbol(const LDSymbol& pSymbol) const
{
  return std::find(m_GlobalGOTSyms.begin(),
                   m_GlobalGOTSyms.end(), &pSymbol) != m_GlobalGOTSyms.end();
}

/// sizeNamePools - compute the size of regular name pools
/// In ELF executable files, regular name pools are .symtab, .strtab,
/// .dynsym, .dynstr, .hash and .shstrtab.
void
MipsGNULDBackend::sizeNamePools(const Module& pModule, bool pIsStaticLink)
{
  // number of entries in symbol tables starts from 1 to hold the special entry
  // at index 0 (STN_UNDEF). See ELF Spec Book I, p1-21.
  size_t symtab = 1;
  size_t dynsym = pIsStaticLink ? 0 : 1;

  // size of string tables starts from 1 to hold the null character in their
  // first byte
  size_t strtab = 1;
  size_t dynstr = pIsStaticLink ? 0 : 1;
  size_t shstrtab = 1;
  size_t hash   = 0;

  /// compute the size of .symtab, .dynsym and .strtab
  /// @{
  Module::const_sym_iterator symbol;
  const Module::SymbolTable& symbols = pModule.getSymbolTable();
  size_t str_size = 0;
  // compute the size of symbols in Local and File category
  Module::const_sym_iterator symEnd = symbols.localEnd();
  for (symbol = symbols.localBegin(); symbol != symEnd; ++symbol) {
    str_size = (*symbol)->nameSize() + 1;
    if (!pIsStaticLink && isDynamicSymbol(**symbol)) {
      ++dynsym;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
        dynstr += str_size;
    }
    ++symtab;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtab += str_size;
  }
  // compute the size of symbols in TLS category
  symEnd = symbols.tlsEnd();
  for (symbol = symbols.tlsBegin(); symbol != symEnd; ++symbol) {
    str_size = (*symbol)->nameSize() + 1;
    if (!pIsStaticLink) {
      ++dynsym;
      if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
        dynstr += str_size;
    }
    ++symtab;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtab += str_size;
  }
  // compute the size of the reset of symbols
  symEnd = pModule.sym_end();
  for (symbol = symbols.tlsEnd(); symbol != symEnd; ++symbol) {
    str_size = (*symbol)->nameSize() + 1;
    if (!pIsStaticLink && isDynamicSymbol(**symbol)) {
      ++dynsym;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
        dynstr += str_size;
    }
    ++symtab;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtab += str_size;
  }

  ELFFileFormat* file_format = getOutputFormat();

  switch(config().codeGenType()) {
    // compute size of .dynstr and .hash
    case LinkerConfig::DynObj: {
      // soname
      if (!pIsStaticLink)
        dynstr += pModule.name().size() + 1;
    }
    /** fall through **/
    case LinkerConfig::Exec: {
      // add DT_NEED strings into .dynstr and .dynamic
      // Rules:
      //   1. ignore --no-add-needed
      //   2. force count in --no-as-needed
      //   3. judge --as-needed
      if (!pIsStaticLink) {
        Module::const_lib_iterator lib, libEnd = pModule.lib_end();
        for (lib = pModule.lib_begin(); lib != libEnd; ++lib) {
          // --add-needed
          if ((*lib)->attribute()->isAddNeeded()) {
            // --no-as-needed
            if (!(*lib)->attribute()->isAsNeeded()) {
              dynstr += (*lib)->name().size() + 1;
              dynamic().reserveNeedEntry();
            }
            // --as-needed
            else if ((*lib)->isNeeded()) {
              dynstr += (*lib)->name().size() + 1;
              dynamic().reserveNeedEntry();
            }
          }
        }

        // compute .hash
        // Both Elf32_Word and Elf64_Word are 4 bytes
        hash = (2 + getHashBucketCount(dynsym, false) + dynsym) *
               sizeof(llvm::ELF::Elf32_Word);
      }

      // set size
      if (32 == bitclass())
        file_format->getDynSymTab().setSize(dynsym*sizeof(llvm::ELF::Elf32_Sym));
      else
        file_format->getDynSymTab().setSize(dynsym*sizeof(llvm::ELF::Elf64_Sym));
      file_format->getDynStrTab().setSize(dynstr);
      file_format->getHashTab().setSize(hash);

    }
    /* fall through */
    case LinkerConfig::Object: {
      if (32 == bitclass())
        file_format->getSymTab().setSize(symtab*sizeof(llvm::ELF::Elf32_Sym));
      else
        file_format->getSymTab().setSize(symtab*sizeof(llvm::ELF::Elf64_Sym));
      file_format->getStrTab().setSize(strtab);
      break;
    }
  } // end of switch
  /// @}

  /// reserve fixed entries in the .dynamic section.
  /// @{
  if (LinkerConfig::DynObj == config().codeGenType() ||
      LinkerConfig::Exec == config().codeGenType()) {
    // Because some entries in .dynamic section need information of .dynsym,
    // .dynstr, .symtab, .strtab and .hash, we can not reserve non-DT_NEEDED
    // entries until we get the size of the sections mentioned above
    dynamic().reserveEntries(config(), *file_format);
    file_format->getDynamic().setSize(dynamic().numOfBytes());
  }
  /// @}

  /// compute the size of .shstrtab section.
  /// @{
  Module::const_iterator sect, sectEnd = pModule.end();
  for (sect = pModule.begin(); sect != sectEnd; ++sect) {
    // StackNote sections will always be in output!
    if (0 != (*sect)->size() || LDFileFormat::StackNote == (*sect)->kind()) {
      shstrtab += ((*sect)->name().size() + 1);
    }
  }
  shstrtab += (strlen(".shstrtab") + 1);
  file_format->getShStrTab().setSize(shstrtab);
  /// @}
}

/// emitSymbol32 - emit an ELF32 symbol
void MipsGNULDBackend::emitSymbol32(llvm::ELF::Elf32_Sym& pSym,
                                    LDSymbol& pSymbol,
                                    char* pStrtab,
                                    size_t pStrtabsize,
                                    size_t pSymtabIdx)
{
   // FIXME: check the endian between host and target
   // write out symbol
    if (ResolveInfo::Section != pSymbol.type() ||
          &pSymbol == m_pGpDispSymbol) {
     pSym.st_name  = pStrtabsize;
     strcpy((pStrtab + pStrtabsize), pSymbol.name());
   }
   else {
     pSym.st_name  = 0;
   }
   pSym.st_value = pSymbol.value();
   pSym.st_size  = getSymbolSize(pSymbol);
   pSym.st_info  = getSymbolInfo(pSymbol);
   pSym.st_other = pSymbol.visibility();
   pSym.st_shndx = getSymbolShndx(pSymbol);
}

/// emitNamePools - emit dynamic name pools - .dyntab, .dynstr, .hash
///
/// the size of these tables should be computed before layout
/// layout should computes the start offset of these tables
void MipsGNULDBackend::emitDynNamePools(const Module& pModule,
                                        MemoryArea& pOutput)
{
  ELFFileFormat* file_format = getOutputFormat();
  if (!file_format->hasDynSymTab() ||
      !file_format->hasDynStrTab() ||
      !file_format->hasHashTab()   ||
      !file_format->hasDynamic())
    return;

  LDSection& symtab_sect = file_format->getDynSymTab();
  LDSection& strtab_sect = file_format->getDynStrTab();
  LDSection& hash_sect   = file_format->getHashTab();
  LDSection& dyn_sect    = file_format->getDynamic();

  MemoryRegion* symtab_region = pOutput.request(symtab_sect.offset(),
                                                symtab_sect.size());
  MemoryRegion* strtab_region = pOutput.request(strtab_sect.offset(),
                                                strtab_sect.size());
  MemoryRegion* hash_region   = pOutput.request(hash_sect.offset(),
                                                hash_sect.size());
  MemoryRegion* dyn_region    = pOutput.request(dyn_sect.offset(),
                                                dyn_sect.size());

  // set up symtab_region
  llvm::ELF::Elf32_Sym* symtab32 = NULL;
  symtab32 = (llvm::ELF::Elf32_Sym*)symtab_region->start();

  symtab32[0].st_name  = 0;
  symtab32[0].st_value = 0;
  symtab32[0].st_size  = 0;
  symtab32[0].st_info  = 0;
  symtab32[0].st_other = 0;
  symtab32[0].st_shndx = 0;

  // set up strtab_region
  char* strtab = (char*)strtab_region->start();
  strtab[0] = '\0';

  bool sym_exist = false;
  HashTableType::entry_type* entry = 0;

  // add index 0 symbol into SymIndexMap
  entry = m_pSymIndexMap->insert(NULL, sym_exist);
  entry->setValue(0);

  size_t symtabIdx = 1;
  size_t strtabsize = 1;

  // emit of .dynsym, and .dynstr except GOT entries
  Module::const_sym_iterator symbol;
  const Module::SymbolTable& symbols = pModule.getSymbolTable();
  // emit symbol in File and Local category if it's dynamic symbol
  Module::const_sym_iterator symEnd = symbols.localEnd();
  for (symbol = symbols.localBegin(); symbol != symEnd; ++symbol) {
    if (!isDynamicSymbol(**symbol))
      continue;

    if (isGlobalGOTSymbol(**symbol))
      continue;

    emitSymbol32(symtab32[symtabIdx], **symbol, strtab, strtabsize,
                   symtabIdx);

    // maintain output's symbol and index map
    entry = m_pSymIndexMap->insert(*symbol, sym_exist);
    entry->setValue(symtabIdx);
    // sum up counters
    ++symtabIdx;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtabsize += (*symbol)->nameSize() + 1;
  }

  // emit symbols in TLS category, all symbols in TLS category shold be emitited
  // directly, except GOT entries
  symEnd = symbols.tlsEnd();
  for (symbol = symbols.tlsBegin(); symbol != symEnd; ++symbol) {
    if (isGlobalGOTSymbol(**symbol))
      continue;

    emitSymbol32(symtab32[symtabIdx], **symbol, strtab, strtabsize,
                   symtabIdx);

    // maintain output's symbol and index map
    entry = m_pSymIndexMap->insert(*symbol, sym_exist);
    entry->setValue(symtabIdx);
    // sum up counters
    ++symtabIdx;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtabsize += (*symbol)->nameSize() + 1;
  }

  // emit the reset of the symbols if the symbol is dynamic symbol
  symEnd = pModule.sym_end();
  for (symbol = symbols.tlsEnd(); symbol != symEnd; ++symbol) {
    if (!isDynamicSymbol(**symbol))
      continue;

    if (isGlobalGOTSymbol(**symbol))
      continue;

    emitSymbol32(symtab32[symtabIdx], **symbol, strtab, strtabsize,
                   symtabIdx);

    // maintain output's symbol and index map
    entry = m_pSymIndexMap->insert(*symbol, sym_exist);
    entry->setValue(symtabIdx);
    // sum up counters
    ++symtabIdx;
    if (ResolveInfo::Section != (*symbol)->type() || *symbol == m_pGpDispSymbol)
      strtabsize += (*symbol)->nameSize() + 1;
  }

  // emit global GOT
  for (std::vector<LDSymbol*>::const_iterator symbol = m_GlobalGOTSyms.begin(),
       symbol_end = m_GlobalGOTSyms.end();
       symbol != symbol_end; ++symbol) {

    // Make sure this golbal GOT entry is a dynamic symbol.
    // If not, something is wrong earlier when putting this symbol into
    //  global GOT.
    if (!isDynamicSymbol(**symbol))
      fatal(diag::mips_got_symbol) << (*symbol)->name();

    emitSymbol32(symtab32[symtabIdx], **symbol, strtab, strtabsize,
                   symtabIdx);
    // maintain output's symbol and index map
    entry = m_pSymIndexMap->insert(*symbol, sym_exist);
    entry->setValue(symtabIdx);

    // sum up counters
    ++symtabIdx;
    if (ResolveInfo::Section != (*symbol)->type())
      strtabsize += (*symbol)->nameSize() + 1;
  }

  // emit DT_NEED
  // add DT_NEED strings into .dynstr
  // Rules:
  //   1. ignore --no-add-needed
  //   2. force count in --no-as-needed
  //   3. judge --as-needed
  ELFDynamic::iterator dt_need = dynamic().needBegin();
  Module::const_lib_iterator lib, libEnd = pModule.lib_end();
  for (lib = pModule.lib_begin(); lib != libEnd; ++lib) {
    // --add-needed
    if ((*lib)->attribute()->isAddNeeded()) {
      // --no-as-needed
      if (!(*lib)->attribute()->isAsNeeded()) {
        strcpy((strtab + strtabsize), (*lib)->name().c_str());
        (*dt_need)->setValue(llvm::ELF::DT_NEEDED, strtabsize);
        strtabsize += (*lib)->name().size() + 1;
        ++dt_need;
      }
      // --as-needed
      else if ((*lib)->isNeeded()) {
        strcpy((strtab + strtabsize), (*lib)->name().c_str());
        (*dt_need)->setValue(llvm::ELF::DT_NEEDED, strtabsize);
        strtabsize += (*lib)->name().size() + 1;
        ++dt_need;
      }
    }
  } // for

  // emit soname
  // initialize value of ELF .dynamic section
  if (LinkerConfig::DynObj == config().codeGenType())
    dynamic().applySoname(strtabsize);
  dynamic().applyEntries(config(), *file_format);
  dynamic().emit(dyn_sect, *dyn_region);

  strcpy((strtab + strtabsize), pModule.name().c_str());
  strtabsize += pModule.name().size() + 1;

  // emit hash table
  // FIXME: this verion only emit SVR4 hash section.
  //        Please add GNU new hash section

  // both 32 and 64 bits hash table use 32-bit entry
  // set up hash_region
  uint32_t* word_array = (uint32_t*)hash_region->start();
  uint32_t& nbucket = word_array[0];
  uint32_t& nchain  = word_array[1];

  nbucket = getHashBucketCount(symtabIdx, false);
  nchain  = symtabIdx;

  uint32_t* bucket = (word_array + 2);
  uint32_t* chain  = (bucket + nbucket);

  // initialize bucket
  bzero((void*)bucket, nbucket);

  StringHash<ELF> hash_func;

  for (size_t sym_idx=0; sym_idx < symtabIdx; ++sym_idx) {
    llvm::StringRef name(strtab + symtab32[sym_idx].st_name);
    size_t bucket_pos = hash_func(name) % nbucket;
    chain[sym_idx] = bucket[bucket_pos];
    bucket[bucket_pos] = sym_idx;
  }

}

MipsGOT& MipsGNULDBackend::getGOT()
{
  assert(NULL != m_pGOT);
  return *m_pGOT;
}

const MipsGOT& MipsGNULDBackend::getGOT() const
{
  assert(NULL != m_pGOT);
  return *m_pGOT;
}

OutputRelocSection& MipsGNULDBackend::getRelDyn()
{
  assert(NULL != m_pRelDyn);
  return *m_pRelDyn;
}

const OutputRelocSection& MipsGNULDBackend::getRelDyn() const
{
  assert(NULL != m_pRelDyn);
  return *m_pRelDyn;
}

unsigned int
MipsGNULDBackend::getTargetSectionOrder(const LDSection& pSectHdr) const
{
  const ELFFileFormat* file_format = getOutputFormat();

  if (&pSectHdr == &file_format->getGOT())
    return SHO_DATA;

  return SHO_UNDEFINED;
}

/// finalizeSymbol - finalize the symbol value
bool MipsGNULDBackend::finalizeTargetSymbols(FragmentLinker& pLinker)
{
  if (NULL != m_pGpDispSymbol)
    m_pGpDispSymbol->setValue(m_pGOT->addr() + 0x7FF0);
  return true;
}

/// allocateCommonSymbols - allocate common symbols in the corresponding
/// sections. This is called at pre-layout stage.
/// @refer Google gold linker: common.cc: 214
/// FIXME: Mips needs to allocate small common symbol
bool MipsGNULDBackend::allocateCommonSymbols(Module& pModule)
{
  SymbolCategory& symbol_list = pModule.getSymbolTable();

  if (symbol_list.emptyCommons() && symbol_list.emptyLocals())
    return true;

  SymbolCategory::iterator com_sym, com_end;

  // FIXME: If the order of common symbols is defined, then sort common symbols
  // std::sort(com_sym, com_end, some kind of order);

  // get corresponding BSS LDSection
  ELFFileFormat* file_format = getOutputFormat();
  LDSection& bss_sect = file_format->getBSS();
  LDSection& tbss_sect = file_format->getTBSS();

  // get or create corresponding BSS SectionData
  SectionData* bss_sect_data = NULL;
  if (bss_sect.hasSectionData())
    bss_sect_data = bss_sect.getSectionData();
  else
    bss_sect_data = ObjectBuilder::CreateSectionData(bss_sect);

  SectionData* tbss_sect_data = NULL;
  if (tbss_sect.hasSectionData())
    tbss_sect_data = tbss_sect.getSectionData();
  else
    tbss_sect_data = ObjectBuilder::CreateSectionData(tbss_sect);

  // remember original BSS size
  uint64_t bss_offset  = bss_sect.size();
  uint64_t tbss_offset = tbss_sect.size();

  // allocate all local common symbols
  com_end = symbol_list.localEnd();

  for (com_sym = symbol_list.localBegin(); com_sym != com_end; ++com_sym) {
    if (ResolveInfo::Common == (*com_sym)->desc()) {
      // We have to reset the description of the symbol here. When doing
      // incremental linking, the output relocatable object may have common
      // symbols. Therefore, we can not treat common symbols as normal symbols
      // when emitting the regular name pools. We must change the symbols'
      // description here.
      (*com_sym)->resolveInfo()->setDesc(ResolveInfo::Define);
      Fragment* frag = new FillFragment(0x0, 1, (*com_sym)->size());
      (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));

      if (ResolveInfo::ThreadLocal == (*com_sym)->type()) {
        // allocate TLS common symbol in tbss section
        tbss_offset += ObjectBuilder::AppendFragment(*frag,
                                                     *tbss_sect_data,
                                                     (*com_sym)->value());
      }
      // FIXME: how to identify small and large common symbols?
      else {
        bss_offset += ObjectBuilder::AppendFragment(*frag,
                                                    *bss_sect_data,
                                                    (*com_sym)->value());
      }
    }
  }

  // allocate all global common symbols
  com_end = symbol_list.commonEnd();
  for (com_sym = symbol_list.commonBegin(); com_sym != com_end; ++com_sym) {
    // We have to reset the description of the symbol here. When doing
    // incremental linking, the output relocatable object may have common
    // symbols. Therefore, we can not treat common symbols as normal symbols
    // when emitting the regular name pools. We must change the symbols'
    // description here.
    (*com_sym)->resolveInfo()->setDesc(ResolveInfo::Define);
    Fragment* frag = new FillFragment(0x0, 1, (*com_sym)->size());
    (*com_sym)->setFragmentRef(FragmentRef::Create(*frag, 0));

    if (ResolveInfo::ThreadLocal == (*com_sym)->type()) {
      // allocate TLS common symbol in tbss section
      tbss_offset += ObjectBuilder::AppendFragment(*frag,
                                                   *tbss_sect_data,
                                                   (*com_sym)->value());
    }
    // FIXME: how to identify small and large common symbols?
    else {
      bss_offset += ObjectBuilder::AppendFragment(*frag,
                                                  *bss_sect_data,
                                                  (*com_sym)->value());
    }
  }

  bss_sect.setSize(bss_offset);
  tbss_sect.setSize(tbss_offset);
  symbol_list.changeCommonsToGlobal();
  return true;
}

void MipsGNULDBackend::scanLocalReloc(Relocation& pReloc,
                                      FragmentLinker& pLinker)
{
  ResolveInfo* rsym = pReloc.symInfo();

  switch (pReloc.type()){
    case llvm::ELF::R_MIPS_NONE:
    case llvm::ELF::R_MIPS_16:
      break;
    case llvm::ELF::R_MIPS_32:
      if (LinkerConfig::DynObj == config().codeGenType()) {
        // TODO: (simon) The gold linker does not create an entry in .rel.dyn
        // section if the symbol section flags contains SHF_EXECINSTR.
        // 1. Find the reason of this condition.
        // 2. Check this condition here.
        m_pRelDyn->reserveEntry(*m_pRelocFactory);
        rsym->setReserved(rsym->reserved() | ReserveRel);

        // Remeber this rsym is a local GOT entry (as if it needs an entry).
        // Actually we don't allocate an GOT entry.
        m_pGOT->setLocal(rsym);
      }
      break;
    case llvm::ELF::R_MIPS_REL32:
    case llvm::ELF::R_MIPS_26:
    case llvm::ELF::R_MIPS_HI16:
    case llvm::ELF::R_MIPS_LO16:
    case llvm::ELF::R_MIPS_PC16:
    case llvm::ELF::R_MIPS_SHIFT5:
    case llvm::ELF::R_MIPS_SHIFT6:
    case llvm::ELF::R_MIPS_64:
    case llvm::ELF::R_MIPS_GOT_PAGE:
    case llvm::ELF::R_MIPS_GOT_OFST:
    case llvm::ELF::R_MIPS_SUB:
    case llvm::ELF::R_MIPS_INSERT_A:
    case llvm::ELF::R_MIPS_INSERT_B:
    case llvm::ELF::R_MIPS_DELETE:
    case llvm::ELF::R_MIPS_HIGHER:
    case llvm::ELF::R_MIPS_HIGHEST:
    case llvm::ELF::R_MIPS_SCN_DISP:
    case llvm::ELF::R_MIPS_REL16:
    case llvm::ELF::R_MIPS_ADD_IMMEDIATE:
    case llvm::ELF::R_MIPS_PJUMP:
    case llvm::ELF::R_MIPS_RELGOT:
    case llvm::ELF::R_MIPS_JALR:
    case llvm::ELF::R_MIPS_GLOB_DAT:
    case llvm::ELF::R_MIPS_COPY:
    case llvm::ELF::R_MIPS_JUMP_SLOT:
      break;
    case llvm::ELF::R_MIPS_GOT16:
    case llvm::ELF::R_MIPS_CALL16:
      // For got16 section based relocations, we need to reserve got entries.
      if (rsym->type() == ResolveInfo::Section) {
        m_pGOT->reserveLocalEntry();
        // Remeber this rsym is a local GOT entry
        m_pGOT->setLocal(rsym);
        return;
      }

      if (!(rsym->reserved() & MipsGNULDBackend::ReserveGot)) {
        m_pGOT->reserveLocalEntry();
        rsym->setReserved(rsym->reserved() | ReserveGot);
        // Remeber this rsym is a local GOT entry
        m_pGOT->setLocal(rsym);
      }
      break;
    case llvm::ELF::R_MIPS_GPREL32:
    case llvm::ELF::R_MIPS_GPREL16:
    case llvm::ELF::R_MIPS_LITERAL:
      break;
    case llvm::ELF::R_MIPS_GOT_DISP:
    case llvm::ELF::R_MIPS_GOT_HI16:
    case llvm::ELF::R_MIPS_CALL_HI16:
    case llvm::ELF::R_MIPS_GOT_LO16:
    case llvm::ELF::R_MIPS_CALL_LO16:
      break;
    case llvm::ELF::R_MIPS_TLS_DTPMOD32:
    case llvm::ELF::R_MIPS_TLS_DTPREL32:
    case llvm::ELF::R_MIPS_TLS_DTPMOD64:
    case llvm::ELF::R_MIPS_TLS_DTPREL64:
    case llvm::ELF::R_MIPS_TLS_GD:
    case llvm::ELF::R_MIPS_TLS_LDM:
    case llvm::ELF::R_MIPS_TLS_DTPREL_HI16:
    case llvm::ELF::R_MIPS_TLS_DTPREL_LO16:
    case llvm::ELF::R_MIPS_TLS_GOTTPREL:
    case llvm::ELF::R_MIPS_TLS_TPREL32:
    case llvm::ELF::R_MIPS_TLS_TPREL64:
    case llvm::ELF::R_MIPS_TLS_TPREL_HI16:
    case llvm::ELF::R_MIPS_TLS_TPREL_LO16:
      break;
    default:
      fatal(diag::unknown_relocation) << (int)pReloc.type()
                                      << pReloc.symInfo()->name();
  }
}

void MipsGNULDBackend::scanGlobalReloc(Relocation& pReloc,
                                       FragmentLinker& pLinker)
{
  ResolveInfo* rsym = pReloc.symInfo();

  switch (pReloc.type()){
    case llvm::ELF::R_MIPS_NONE:
    case llvm::ELF::R_MIPS_INSERT_A:
    case llvm::ELF::R_MIPS_INSERT_B:
    case llvm::ELF::R_MIPS_DELETE:
    case llvm::ELF::R_MIPS_TLS_DTPMOD64:
    case llvm::ELF::R_MIPS_TLS_DTPREL64:
    case llvm::ELF::R_MIPS_REL16:
    case llvm::ELF::R_MIPS_ADD_IMMEDIATE:
    case llvm::ELF::R_MIPS_PJUMP:
    case llvm::ELF::R_MIPS_RELGOT:
    case llvm::ELF::R_MIPS_TLS_TPREL64:
      break;
    case llvm::ELF::R_MIPS_32:
    case llvm::ELF::R_MIPS_64:
    case llvm::ELF::R_MIPS_HI16:
    case llvm::ELF::R_MIPS_LO16:
      if (symbolNeedsDynRel(pLinker, *rsym, false, true)) {
        m_pRelDyn->reserveEntry(*m_pRelocFactory);
        rsym->setReserved(rsym->reserved() | ReserveRel);

        // Remeber this rsym is a global GOT entry (as if it needs an entry).
        // Actually we don't allocate an GOT entry.
        m_pGOT->setGlobal(rsym);
      }
      break;
    case llvm::ELF::R_MIPS_GOT16:
    case llvm::ELF::R_MIPS_CALL16:
    case llvm::ELF::R_MIPS_GOT_DISP:
    case llvm::ELF::R_MIPS_GOT_HI16:
    case llvm::ELF::R_MIPS_CALL_HI16:
    case llvm::ELF::R_MIPS_GOT_LO16:
    case llvm::ELF::R_MIPS_CALL_LO16:
    case llvm::ELF::R_MIPS_GOT_PAGE:
    case llvm::ELF::R_MIPS_GOT_OFST:
      if (!(rsym->reserved() & MipsGNULDBackend::ReserveGot)) {
        m_pGOT->reserveGlobalEntry();
        rsym->setReserved(rsym->reserved() | ReserveGot);
        m_GlobalGOTSyms.push_back(rsym->outSymbol());
        // Remeber this rsym is a global GOT entry
        m_pGOT->setGlobal(rsym);
      }
      break;
    case llvm::ELF::R_MIPS_LITERAL:
    case llvm::ELF::R_MIPS_GPREL32:
      fatal(diag::invalid_global_relocation) << (int)pReloc.type()
                                             << pReloc.symInfo()->name();
      break;
    case llvm::ELF::R_MIPS_GPREL16:
      break;
    case llvm::ELF::R_MIPS_26:
    case llvm::ELF::R_MIPS_PC16:
      break;
    case llvm::ELF::R_MIPS_16:
    case llvm::ELF::R_MIPS_SHIFT5:
    case llvm::ELF::R_MIPS_SHIFT6:
    case llvm::ELF::R_MIPS_SUB:
    case llvm::ELF::R_MIPS_HIGHER:
    case llvm::ELF::R_MIPS_HIGHEST:
    case llvm::ELF::R_MIPS_SCN_DISP:
      break;
    case llvm::ELF::R_MIPS_TLS_DTPREL32:
    case llvm::ELF::R_MIPS_TLS_GD:
    case llvm::ELF::R_MIPS_TLS_LDM:
    case llvm::ELF::R_MIPS_TLS_DTPREL_HI16:
    case llvm::ELF::R_MIPS_TLS_DTPREL_LO16:
    case llvm::ELF::R_MIPS_TLS_GOTTPREL:
    case llvm::ELF::R_MIPS_TLS_TPREL32:
    case llvm::ELF::R_MIPS_TLS_TPREL_HI16:
    case llvm::ELF::R_MIPS_TLS_TPREL_LO16:
      break;
    case llvm::ELF::R_MIPS_REL32:
      break;
    case llvm::ELF::R_MIPS_JALR:
      break;
    case llvm::ELF::R_MIPS_COPY:
    case llvm::ELF::R_MIPS_GLOB_DAT:
    case llvm::ELF::R_MIPS_JUMP_SLOT:
      fatal(diag::dynamic_relocation) << (int)pReloc.type();
      break;
    default:
      fatal(diag::unknown_relocation) << (int)pReloc.type()
                                      << pReloc.symInfo()->name();
  }
}

void MipsGNULDBackend::defineGOTSymbol(FragmentLinker& pLinker)
{
  // define symbol _GLOBAL_OFFSET_TABLE_
  if ( m_pGOTSymbol != NULL ) {
    pLinker.defineSymbol<FragmentLinker::Force, FragmentLinker::Unresolve>(
                     "_GLOBAL_OFFSET_TABLE_",
                     false,
                     ResolveInfo::Object,
                     ResolveInfo::Define,
                     ResolveInfo::Local,
                     0x0, // size
                     0x0, // value
                     FragmentRef::Create(*(m_pGOT->begin()), 0x0),
                     ResolveInfo::Hidden);
  }
  else {
    m_pGOTSymbol = pLinker.defineSymbol<FragmentLinker::Force, FragmentLinker::Resolve>(
                     "_GLOBAL_OFFSET_TABLE_",
                     false,
                     ResolveInfo::Object,
                     ResolveInfo::Define,
                     ResolveInfo::Local,
                     0x0, // size
                     0x0, // value
                     FragmentRef::Create(*(m_pGOT->begin()), 0x0),
                     ResolveInfo::Hidden);
  }
}

/// doCreateProgramHdrs - backend can implement this function to create the
/// target-dependent segments
void MipsGNULDBackend::doCreateProgramHdrs(Module& pModule,
                                           const FragmentLinker& pLinker)
{
  // TODO
}

//===----------------------------------------------------------------------===//
/// createMipsLDBackend - the help funtion to create corresponding MipsLDBackend
///
static TargetLDBackend* createMipsLDBackend(const llvm::Target& pTarget,
                                            const LinkerConfig& pConfig)
{
  if (pConfig.triple().isOSDarwin()) {
    assert(0 && "MachO linker is not supported yet");
  }
  if (pConfig.triple().isOSWindows()) {
    assert(0 && "COFF linker is not supported yet");
  }
  return new MipsGNULDBackend(pConfig);
}

//===----------------------------------------------------------------------===//
// Force static initialization.
//===----------------------------------------------------------------------===//
extern "C" void LLVMInitializeMipsLDBackend() {
  // Register the linker backend
  mcld::TargetRegistry::RegisterTargetLDBackend(mcld::TheMipselTarget,
                                                createMipsLDBackend);
}

