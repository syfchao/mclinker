//===- ELFReader.tcc ------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file is the template implemenation of ELFReaders
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ELFReader<32, true>
#include <cstring>
#include <vector>
#include <mcld/Fragment/FragmentLinker.h>
#include <mcld/Fragment/FillFragment.h>
#include <mcld/Fragment/RegionFragment.h>

namespace mcld {

/// constructor
ELFReader<32, true>::ELFReader(GNULDBackend& pBackend)
  : ELFReaderIF(pBackend) {
}

/// destructor
ELFReader<32, true>::~ELFReader()
{
}

/// isELF - is this a ELF file
bool ELFReader<32, true>::isELF(void* pELFHeader) const
{
  llvm::ELF::Elf32_Ehdr* hdr =
                          reinterpret_cast<llvm::ELF::Elf32_Ehdr*>(pELFHeader);
  if (0 == memcmp(llvm::ELF::ElfMagic, hdr, 4))
    return true;
  return false;
}

/// isMyEndian - is this ELF file in the same endian to me?
bool ELFReader<32, true>::isMyEndian(void* pELFHeader) const
{
  llvm::ELF::Elf32_Ehdr* hdr =
                          reinterpret_cast<llvm::ELF::Elf32_Ehdr*>(pELFHeader);

  return (hdr->e_ident[llvm::ELF::EI_DATA] == llvm::ELF::ELFDATA2LSB);
}

/// isMyMachine - is this ELF file generated for the same machine.
bool ELFReader<32, true>::isMyMachine(void* pELFHeader) const
{
  llvm::ELF::Elf32_Ehdr* hdr =
                          reinterpret_cast<llvm::ELF::Elf32_Ehdr*>(pELFHeader);

  if (llvm::sys::isLittleEndianHost())
    return (hdr->e_machine == target().machine());
  return (bswap16(hdr->e_machine) == target().machine());
}

/// fileType - return the file type
Input::Type ELFReader<32, true>::fileType(void* pELFHeader) const
{
  llvm::ELF::Elf32_Ehdr* hdr =
                          reinterpret_cast<llvm::ELF::Elf32_Ehdr*>(pELFHeader);
  uint32_t type = 0x0;
  if (llvm::sys::isLittleEndianHost())
    type = hdr->e_type;
  else
    type = bswap16(hdr->e_type);

  switch(type) {
  case llvm::ELF::ET_REL:
    return Input::Object;
  case llvm::ELF::ET_EXEC:
    return Input::Exec;
  case llvm::ELF::ET_DYN:
    return Input::DynObj;
  case llvm::ELF::ET_CORE:
    return Input::CoreFile;
  case llvm::ELF::ET_NONE:
  default:
    return Input::Unknown;
  }
}

/// readSectionHeaders - read ELF section header table and create LDSections
bool
ELFReader<32, true>::readSectionHeaders(Input& pInput, void* pELFHeader) const
{
  llvm::ELF::Elf32_Ehdr* ehdr =
                          reinterpret_cast<llvm::ELF::Elf32_Ehdr*>(pELFHeader);

  uint32_t shoff     = 0x0;
  uint16_t shentsize = 0x0;
  uint16_t shnum     = 0x0;
  uint16_t shstrtab  = 0x0;

  if (llvm::sys::isLittleEndianHost()) {
    shoff     = ehdr->e_shoff;
    shentsize = ehdr->e_shentsize;
    shnum     = ehdr->e_shnum;
    shstrtab  = ehdr->e_shstrndx;
  }
  else {
    shoff     = bswap32(ehdr->e_shoff);
    shentsize = bswap16(ehdr->e_shentsize);
    shnum     = bswap16(ehdr->e_shnum);
    shstrtab  = bswap16(ehdr->e_shstrndx);
  }

  // If the file has no section header table, e_shoff holds zero.
  if (0x0 == shoff)
    return true;

  MemoryRegion* shdr_region = pInput.memArea()->request(
                                 pInput.fileOffset() + shoff, shnum*shentsize);
  llvm::ELF::Elf32_Shdr* shdrTab =
                reinterpret_cast<llvm::ELF::Elf32_Shdr*>(shdr_region->start());

  uint32_t sh_name      = 0x0;
  uint32_t sh_type      = 0x0;
  uint32_t sh_flags     = 0x0;
  uint32_t sh_offset    = 0x0;
  uint32_t sh_size      = 0x0;
  uint32_t sh_link      = 0x0;
  uint32_t sh_info      = 0x0;
  uint32_t sh_addralign = 0x0;

  // get .shstrtab first
  llvm::ELF::Elf32_Shdr* shdr = &shdrTab[shstrtab];
  if (llvm::sys::isLittleEndianHost()) {
    sh_offset = shdr->sh_offset;
    sh_size   = shdr->sh_size;
  }
  else {
    sh_offset = bswap32(shdr->sh_offset);
    sh_size   = bswap32(shdr->sh_size);
  }

  MemoryRegion* sect_name_region = pInput.memArea()->request(
                                      pInput.fileOffset() + sh_offset, sh_size);
  const char* sect_name =
                       reinterpret_cast<const char*>(sect_name_region->start());

  LinkInfoList link_info_list;

  // create all LDSections, including first NULL section.
  for (size_t idx = 0; idx < shnum; ++idx) {
    if (llvm::sys::isLittleEndianHost()) {
      sh_name      = shdrTab[idx].sh_name;
      sh_type      = shdrTab[idx].sh_type;
      sh_flags     = shdrTab[idx].sh_flags;
      sh_offset    = shdrTab[idx].sh_offset;
      sh_size      = shdrTab[idx].sh_size;
      sh_link      = shdrTab[idx].sh_link;
      sh_info      = shdrTab[idx].sh_info;
      sh_addralign = shdrTab[idx].sh_addralign;
    }
    else {
      sh_name      = bswap32(shdrTab[idx].sh_name);
      sh_type      = bswap32(shdrTab[idx].sh_type);
      sh_flags     = bswap32(shdrTab[idx].sh_flags);
      sh_offset    = bswap32(shdrTab[idx].sh_offset);
      sh_size      = bswap32(shdrTab[idx].sh_size);
      sh_link      = bswap32(shdrTab[idx].sh_link);
      sh_info      = bswap32(shdrTab[idx].sh_info);
      sh_addralign = bswap32(shdrTab[idx].sh_addralign);
    }

    LDFileFormat::Kind kind = GetSectionKind(sh_type,
                                             sect_name+sh_name);

    LDSection* section = LDSection::Create(sect_name+sh_name,
                                           kind,
                                           sh_type,
                                           sh_flags);

    section->setSize(sh_size);
    section->setOffset(sh_offset);
    section->setInfo(sh_info);
    section->setAlign(sh_addralign);

    if (sh_link != 0x0 || sh_info != 0x0) {
      LinkInfo link_info = { section, sh_link, sh_info };
      link_info_list.push_back(link_info);
    }

    pInput.context()->appendSection(*section);
  } // end of for

  // set up InfoLink
  LinkInfoList::iterator info, infoEnd = link_info_list.end();
  for (info = link_info_list.begin(); info != infoEnd; ++info) {
    if (LDFileFormat::NamePool == info->section->kind() ||
        LDFileFormat::Group == info->section->kind() ||
        LDFileFormat::Note == info->section->kind()) {
      info->section->setLink(pInput.context()->getSection(info->sh_link));
      continue;
    }
    if (LDFileFormat::Relocation == info->section->kind()) {
      info->section->setLink(pInput.context()->getSection(info->sh_info));
      continue;
    }
  }

  pInput.memArea()->release(shdr_region);
  pInput.memArea()->release(sect_name_region);

  return true;
}

/// readSignature - read a symbol from the given Input and index in symtab
/// This is used to get the signature of a group section.
ResolveInfo* ELFReader<32, true>::readSignature(Input& pInput,
                                                LDSection& pSymTab,
                                                uint32_t pSymIdx) const
{
  LDSection* symtab = &pSymTab;
  LDSection* strtab = symtab->getLink();
  assert(NULL != symtab && NULL != strtab);

  uint32_t offset = pInput.fileOffset() + symtab->offset() +
                      sizeof(llvm::ELF::Elf32_Sym) * pSymIdx;
  MemoryRegion* symbol_region =
                pInput.memArea()->request(offset, sizeof(llvm::ELF::Elf32_Sym));
  llvm::ELF::Elf32_Sym* entry =
                reinterpret_cast<llvm::ELF::Elf32_Sym*>(symbol_region->start());

  uint32_t st_name  = 0x0;
  uint32_t st_size  = 0x0;
  uint8_t  st_info  = 0x0;
  uint8_t  st_other = 0x0;
  uint16_t st_shndx = 0x0;
  st_info  = entry->st_info;
  st_other = entry->st_other;
  if (llvm::sys::isLittleEndianHost()) {
    st_name  = entry->st_name;
    st_size  = entry->st_size;
    st_shndx = entry->st_shndx;
  }
  else {
    st_name  = bswap32(entry->st_name);
    st_size  = bswap32(entry->st_size);
    st_shndx = bswap16(entry->st_shndx);
  }

  MemoryRegion* strtab_region = pInput.memArea()->request(
                       pInput.fileOffset() + strtab->offset(), strtab->size());

  // get ld_name
  llvm::StringRef ld_name(
                    reinterpret_cast<char*>(strtab_region->start() + st_name));

  ResolveInfo* result = ResolveInfo::Create(ld_name);
  result->setSource(pInput.type() == Input::DynObj);
  result->setType(static_cast<ResolveInfo::Type>(st_info & 0xF));
  result->setDesc(getSymDesc(st_shndx, pInput));
  result->setBinding(getSymBinding((st_info >> 4), st_shndx, st_other));
  result->setVisibility(getSymVisibility(st_other));

  // release regions
  pInput.memArea()->release(symbol_region);
  pInput.memArea()->release(strtab_region);

  return result;
}

/// readDynamic - read ELF .dynamic in input dynobj
bool ELFReader<32, true>::readDynamic(Input& pInput) const
{
  assert(pInput.type() == Input::DynObj);
  const LDSection* dynamic_sect = pInput.context()->getSection(".dynamic");
  if (NULL == dynamic_sect) {
    fatal(diag::err_cannot_read_section) << ".dynamic";
  }
  const LDSection* dynstr_sect = dynamic_sect->getLink();
  if (NULL == dynstr_sect) {
    fatal(diag::err_cannot_read_section) << ".dynstr";
  }

  MemoryRegion* dynamic_region = pInput.memArea()->request(
           pInput.fileOffset() + dynamic_sect->offset(), dynamic_sect->size());

  MemoryRegion* dynstr_region = pInput.memArea()->request(
             pInput.fileOffset() + dynstr_sect->offset(), dynstr_sect->size());

  assert(NULL != dynamic_region && NULL != dynstr_region);

  const llvm::ELF::Elf32_Dyn* dynamic =
    (llvm::ELF::Elf32_Dyn*) dynamic_region->start();
  const char* dynstr = (const char*) dynstr_region->start();
  bool hasSOName = false;
  size_t numOfEntries = dynamic_sect->size() / sizeof(llvm::ELF::Elf32_Dyn);

  for (size_t idx = 0; idx < numOfEntries; ++idx) {

    llvm::ELF::Elf32_Sword d_tag = 0x0;
    llvm::ELF::Elf32_Word d_val = 0x0;

    if (llvm::sys::isLittleEndianHost()) {
      d_tag = dynamic[idx].d_tag;
      d_val = dynamic[idx].d_un.d_val;
    } else {
      d_tag = bswap32(dynamic[idx].d_tag);
      d_val = bswap32(dynamic[idx].d_un.d_val);
    }

    switch (d_tag) {
      case llvm::ELF::DT_SONAME:
        assert(d_val < dynstr_sect->size());
        pInput.setName(sys::fs::Path(dynstr + d_val).filename().native());
        hasSOName = true;
        break;
      case llvm::ELF::DT_NEEDED:
        // TODO:
        break;
      case llvm::ELF::DT_NULL:
      default:
        break;
    }
  }

  // if there is no SONAME in .dynamic, then set it from input path
  if (!hasSOName)
    pInput.setName(pInput.path().filename().native());

  pInput.memArea()->release(dynamic_region);
  pInput.memArea()->release(dynstr_region);
  return true;
}

} // namespace of mcld

