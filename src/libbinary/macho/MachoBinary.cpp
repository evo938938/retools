/*
 * MachoBinary.cpp
 *
 *  Created on: Mar 22, 2015
 *      Author: anon
 */

#include "macho/MachoBinary.h"
#include "debug.h"

#include <cassert>
#include <string>
#include <cstring>
#include <vector>
#include <queue>

using namespace std;

static int64_t read_sleb128(const uint8_t*& p, const uint8_t* end) {
	int64_t result = 0;
	int bit = 0;
	uint8_t byte;
	do {
		byte = *p++;
		result |= ((byte & 0x7f) << bit);
		bit += 7;
	} while (byte & 0x80);

	if ((byte & 0x40) != 0)
		result |= (-1LL) << bit;
	return result;
}

static uint64_t read_uleb128(const uint8_t *&p, const uint8_t *end) {
	uint64_t result = 0;
	int bit = 0;
	do {

		uint64_t slice = *p & 0x7f;

		result |= (slice << bit);
		bit += 7;
	} while (*p++ & 0x80);

	return result;
}

static uintptr_t read_terminal_size(const uint8_t *&p, const uint8_t *end) {
	uintptr_t terminal_size = *p++;
	if (terminal_size > 127) {
		--p;
		terminal_size = read_uleb128(p, end);
	}

	return terminal_size;
}

string LoadCommandName(unsigned cmd) {
    switch (cmd) {
        case LC_SEGMENT:
            return "LC_SEGMENT";
        case LC_SYMTAB:
            return "LC_SYMTAB";
        case LC_SYMSEG:
            return "LC_SYMSEG";
        case LC_THREAD:
            return "LC_THREAD";
        case LC_UNIXTHREAD:
            return "LC_UNIXTHREAD";
        case LC_LOADFVMLIB:
            return "LC_LOADFVMLIB";
        case LC_IDFVMLIB:
            return "LC_IDFVMLIB";
        case LC_IDENT:
            return "LC_IDENT";
        case LC_FVMFILE:
            return "LC_FVMFILE";
        case LC_PREPAGE:
            return "LC_PREPAGE";
        case LC_DYSYMTAB:
            return "LC_DYSYMTAB";
        case LC_LOAD_DYLIB:
            return "LC_LOAD_DYLIB";
        case LC_ID_DYLIB:
            return "LC_ID_DYLIB";
        case LC_LOAD_DYLINKER:
            return "LC_LOAD_DYLINKER";
        case LC_ID_DYLINKER:
            return "LC_ID_DYLINKER";
        case LC_PREBOUND_DYLIB:
            return "LC_PREBOUND_DYLIB";
        case LC_ROUTINES:
            return "LC_ROUTINES";
        case LC_SUB_FRAMEWORK:
            return "LC_SUB_FRAMEWORK";
        case LC_SUB_UMBRELLA:
            return "LC_SUB_UMBRELLA";
        case LC_SUB_CLIENT:
            return "LC_SUB_CLIENT";
        case LC_SUB_LIBRARY:
            return "LC_SUB_LIBRARY";
        case LC_TWOLEVEL_HINTS:
            return "LC_TWOLEVEL_HINTS";
        case LC_PREBIND_CKSUM:
            return "LC_PREBIND_CKSUM";
        case LC_LOAD_WEAK_DYLIB:
            return "LC_LOAD_WEAK_DYLIB";
        case LC_SEGMENT_64:
            return "LC_SEGMENT_64";
        case LC_ROUTINES_64:
            return "LC_ROUTINES_64";
        case LC_UUID:
            return "LC_UUID";
        case LC_RPATH:
            return "LC_RPATH";
        case LC_CODE_SIGNATURE:
            return "LC_CODE_SIGNATURE";
        case LC_SEGMENT_SPLIT_INFO:
            return "LC_SEGMENT_SPLIT_INFO";
        case LC_REEXPORT_DYLIB:
            return "LC_REEXPORT_DYLIB";
        case LC_LAZY_LOAD_DYLIB:
            return "LC_LAZY_LOAD_DYLIB";
        case LC_ENCRYPTION_INFO:
            return "LC_ENCRYPTION_INFO";
        case LC_DYLD_INFO:
            return "LC_DYLD_INFO";
        case LC_DYLD_INFO_ONLY:
            return "LC_DYLD_INFO_ONLY";
        case LC_LOAD_UPWARD_DYLIB:
            return "LC_LOAD_UPWARD_DYLIB";
        case LC_VERSION_MIN_MACOSX:
            return "LC_VERSION_MIN_MACOSX";
        case LC_VERSION_MIN_IPHONEOS:
            return "LC_VERSION_MIN_IPHONEOS";
        case LC_FUNCTION_STARTS:
            return "LC_FUNCTION_STARTS";
        case LC_DYLD_ENVIRONMENT:
            return "LC_DYLD_ENVIRONMENT";
        case LC_MAIN:
            return "LC_MAIN";
        case LC_DATA_IN_CODE:
            return "LC_DATA_IN_CODE";
        case LC_SOURCE_VERSION:
            return "LC_SOURCE_VERSION";
        case LC_DYLIB_CODE_SIGN_DRS:
            return "LC_DYLIB_CODE_SIGN_DRS";
        case LC_ENCRYPTION_INFO_64:
            return "LC_ENCRYPTION_INFO_64";
        case LC_LINKER_OPTION:
            return "LC_LINKER_OPTION";
        case LC_LINKER_OPTIMIZATION_HINT:
            return "LC_LINKER_OPTIMIZATION_HINT";
        default:
            return "LC_UNKNOWN";
    }
}

bool MachoBinary::init() {
    m_symbol_table = nullptr;
    m_string_table = nullptr;

    struct mach_header *tmp_header = m_data->offset<struct mach_header>(0);
    if (!tmp_header)
        return false;

    // Parse the address space size and the endianness.
    switch (tmp_header->magic) {
        case MH_MAGIC:
            m_endianness = BinaryEndianness::LITTLE;
            m_address_space_size = AddressSpaceSize::BINARY_32;
            break;

        case MH_CIGAM:
            m_endianness = BinaryEndianness::BIG;
            m_address_space_size = AddressSpaceSize::BINARY_32;
            break;

        case MH_MAGIC_64:
            m_endianness = BinaryEndianness::LITTLE;
            m_address_space_size = AddressSpaceSize::BINARY_64;
            break;

        case MH_CIGAM_64:
            m_endianness = BinaryEndianness::BIG;
            m_address_space_size = AddressSpaceSize::BINARY_64;
            break;

        default:
            LOG_ERR("Invalid mach-o magic number 0x%.8x", tmp_header->magic);
            return false;
    }

    // Set the binary format.
    m_binary_format = BinaryFormat::MACHO;

    // Read the header.
    if (is32()) {
        m_header.header_32 = *m_data->offset<struct mach_header>(0);
    } else {
        m_header.header_64 = *m_data->offset<struct mach_header_64>(0);
    }

    // Get the kind of mach-o file.
    switch (filetype()) {
        case MH_OBJECT:
            m_binary_type = BinaryType::Object;
            break;
        case MH_CORE:
            m_binary_type = BinaryType::Core;
            break;
        case MH_EXECUTE:
            m_binary_type = BinaryType::Executable;
            break;
        case MH_DYLIB:
        case MH_BUNDLE: // XXX: Verify that this is a library.
            m_binary_type = BinaryType::Library;
            break;
        default:
            LOG_ERR("Unknown mach-o file type 0x%.8x", filetype());
            return false;
    }

    // Get the CPU type.
    switch (cputype()) {
        case CPU_TYPE_X86:
            m_binary_arch = BinaryArch::X86;
            break;
        case CPU_TYPE_X86_64:
            m_binary_arch = BinaryArch::X86_64;
            break;
        case CPU_TYPE_ARM:
            m_binary_arch = BinaryArch::ARM;
            break;
        case CPU_TYPE_ARM64:
            m_binary_arch = BinaryArch::ARM64;
            break;
        default:
            LOG_ERR("Unknown mach-o CPU type 0x%.8x", cputype());
            return false;
    }

    // Load information from the load commands.
    if (!parse_load_commands()) {
        return false;
    }

    return true;
}

struct load_command *MachoBinary::get_load_command(unsigned idx) {
    // The first load command is past the mach-o header.
    struct load_command *lc = m_data->offset<struct load_command>(mach_header_size());
    if (!lc || idx >= ncmds())
        return nullptr;

    // Skip all the load commands up to the one we want.
    for (unsigned i = 0; i < idx; ++i) {
        // Get the next load command.
        lc = m_data->pointer<struct load_command>(reinterpret_cast<char *>(lc) + lc->cmdsize);
        if (!lc)
            return nullptr;
    }

    return lc;
}

bool MachoBinary::parse_load_commands() {
    // Get the section size align mask.
    unsigned align_mask = is32() ? 3 : 7;

    // For each load command.
    for (unsigned i = 0; i < ncmds(); ++i) {
        // Get the 'i'th load command.
        struct load_command *cur_lc = get_load_command(i);
        if (!cur_lc) {
            LOG_ERR("Could not get command %d", i);
            break;
        }

        // Check the size of the load command.
        if ((cur_lc->cmdsize & align_mask) != 0) {
            LOG_WARN("Load command %u has an unaligned size, skipping", i);
            continue;
        }

        LOG_DEBUG("Parsing command (%s) %d of %d", LoadCommandName(cur_lc->cmd).c_str(), i, ncmds());

        switch (cur_lc->cmd) {
            case LC_DATA_IN_CODE:
                // Table of data start addresses inside code segments.
                if (!parse_data_in_code(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_FUNCTION_STARTS:
                // Compressed table of function start addresses.
                if (!parse_function_starts(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_ROUTINES:
                // Describes the location of the shared library initialization function.
                if (!parse_routines_32(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_ROUTINES_64:
                // Describes the location of the shared library initialization function.
                if (!parse_routines_64(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_SEGMENT:
                // Defines a segment of this file to be mapped into the address space.
                if (!parse_segment<segment_command, section>(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_SYMTAB:
                // Specifies the symbol table for this file.
                if (!parse_symtab(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_SEGMENT_64:
                // Defines a 64-bit segment of this file to be mapped into the address space.
                if (!parse_segment<segment_command_64, section_64>(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_DYSYMTAB:
                // Specifies additional symbol table information used by the dynamic linker.
                if (!parse_dysymtab(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_THREAD:
                // Defines the initial thread state of the main thread of the process but does not allocate a stack.
                if (!parse_thread(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_ID_DYLIB:
                // For a shared library, this segments identifies the the name of the library.
                if (!parse_id_dylib(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;

            case LC_LAZY_LOAD_DYLIB:
            case LC_LOAD_DYLIB:         // Regular dynamic library.
            case LC_LOAD_WEAK_DYLIB:    // Dynamic library that may be missing.
            case LC_LOAD_UPWARD_DYLIB:  // Used for handling mutually dependent libraries.
            case LC_REEXPORT_DYLIB:     // This is worth looking. Used to replace pre-existing library.
                // Defines the name of a dynamic shared library that this file links against.
                if (!parse_dylib(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_MAIN:
                // Replacement for LC_UNIXTHREAD.
                if (!parse_main(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_UNIXTHREAD:
                // Defines the initial thread state of the main thread of the process and allocates a stack.
                if (!parse_unixthread(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_DYLD_INFO:
            case LC_DYLD_INFO_ONLY:
                // Compressed dyld information. This somehow indicates that the mach-o file is compressed.
                if (!parse_dyld_info(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

                break;
            case LC_ENCRYPTION_INFO:
                if (!parse_encryption_info_32(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

            	break;
            case LC_ENCRYPTION_INFO_64:
                if (!parse_encryption_info_64(cur_lc)) {
                    LOG_WARN("Could not parse the load command, skipping");
                    continue;
                }

            	break;
            default:
                LOG_INFO("Load command `%s` is not supported", LoadCommandName(cur_lc->cmd).c_str());
                break;
        }
    }

    return true;
}

bool MachoBinary::parse_data_in_code(struct load_command *lc) {
    struct linkedit_data_command *cmd = m_data->pointer<struct linkedit_data_command>(lc);
    if (!cmd)
        return false;

    // The data in code information gives information about data inside a code segment.
    struct data_in_code_entry *data = m_data->offset<struct data_in_code_entry>(cmd->dataoff, cmd->datasize);

    // Get the number of entries.
    unsigned n = cmd->datasize / sizeof(*data);
    for (unsigned i = 0; i < n; ++i) {
        switch (data[i].kind) {
            case DICE_KIND_DATA:
                LOG_DEBUG("DICE_KIND_DATA:offset=%u length=%u\n", data[i].offset, data[i].length);
                break;
            case DICE_KIND_JUMP_TABLE8:
                LOG_DEBUG("DICE_KIND_JUMP_TABLE8:offset=%u length=%u\n", data[i].offset, data[i].length);
                break;
            case DICE_KIND_JUMP_TABLE16:
                LOG_DEBUG("DICE_KIND_JUMP_TABLE16:offset=%u length=%u\n", data[i].offset, data[i].length);
                break;
            case DICE_KIND_JUMP_TABLE32:
                LOG_DEBUG("DICE_KIND_JUMP_TABLE32:offset=%u length=%u\n", data[i].offset, data[i].length);
                break;
            case DICE_KIND_ABS_JUMP_TABLE32:
                LOG_DEBUG("DICE_KIND_ABS_JUMP_TABLE32:offset=%u length=%u\n", data[i].offset, data[i].length);
                break;
            default:
                break;
        }
    }

    return true;
}

bool MachoBinary::parse_function_starts(struct load_command *lc) {
    struct linkedit_data_command *cmd = m_data->pointer<struct linkedit_data_command>(lc);
    if (!cmd)
        return false;

    const uint8_t *infoStart = m_data->offset<const uint8_t>(cmd->dataoff, cmd->datasize);
    if (!infoStart)
        return false;

    const uint8_t *infoEnd = &infoStart[cmd->datasize];

    uint64_t address = 0;
    for (const uint8_t* p = infoStart; (*p != 0) && (p < infoEnd);) {
        uint64_t delta = 0;
        uint32_t shift = 0;
        bool more = true;
        do {
            uint8_t byte = *p++;
            delta |= ((byte & 0x7F) << shift);
            shift += 7;

            if (byte < 0x80) {
                address += delta;
                //printFunctionStartLine(address);
                LOG_DEBUG("address = %p", (void * ) address);
                more = false;
            }
        } while (more);
    }

    return true;
}

bool MachoBinary::parse_routines_32(struct load_command *lc) {
    struct routines_command * cmd = m_data->pointer<struct routines_command>(lc);
    LOG_DEBUG("init_address = 0x%.8x init_module = 0x%.8x", cmd->init_address, cmd->init_module);
    return true;
}

bool MachoBinary::parse_routines_64(struct load_command *lc) {
    struct routines_command_64 * cmd = m_data->pointer<struct routines_command_64>(lc);
    LOG_DEBUG("init_address = 0x%.16llx init_module = 0x%.16llx", cmd->init_address, cmd->init_module);
    return true;
}

template<typename Section_t> bool MachoBinary::parse_section(Section_t *lc) {
    uint32_t section_type = lc->flags & SECTION_TYPE;
    uint32_t section_usr_attr = lc->flags & SECTION_ATTRIBUTES_USR;
    uint32_t section_sys_attr = lc->flags & SECTION_ATTRIBUTES_SYS;

    add_section<Section_t>(lc);

    LOG_DEBUG("name%16s:%-16s addr=%p size=0x%.16llx offset=0x%.8x align=0x%.8x reloff=0x%.8x nreloc=0x%.8x flags=0x%.8x",
    		lc->segname, lc->sectname, (void * ) lc->addr, lc->size, lc->offset,
			lc->align, lc->reloff, lc->nreloc, lc->flags);

    switch (section_type) {
        case S_REGULAR:
            LOG_DEBUG("S_REGULAR");
            break;

        case S_ZEROFILL:
            LOG_DEBUG("S_ZEROFILL");
            break;

        case S_CSTRING_LITERALS:
            LOG_DEBUG("S_CSTRING_LITERALS");
            parse_cstring_literals_section<Section_t>(lc);
            break;

        case S_4BYTE_LITERALS:
            LOG_DEBUG("S_4BYTE_LITERALS");
            parse_4byte_literals<Section_t>(lc);
            break;

        case S_8BYTE_LITERALS:
            LOG_DEBUG("S_8BYTE_LITERALS");
            parse_8byte_literals<Section_t>(lc);
            break;

        case S_16BYTE_LITERALS:
            LOG_DEBUG("S_16BYTE_LITERALS");
            parse_16byte_literals<Section_t>(lc);
            break;

        case S_LITERAL_POINTERS:
            LOG_DEBUG("S_LITERAL_POINTERS");
            parse_literal_pointers<Section_t>(lc);
            break;

        case S_MOD_INIT_FUNC_POINTERS:
            LOG_DEBUG("S_MOD_INIT_FUNC_POINTERS");
            parse_mod_init_func_pointers<Section_t>(lc);
            break;

        case S_MOD_TERM_FUNC_POINTERS:
            LOG_DEBUG("S_MOD_TERM_FUNC_POINTERS");
            parse_mod_term_func_pointers<Section_t>(lc);
            break;

        case S_NON_LAZY_SYMBOL_POINTERS:
            LOG_DEBUG("S_NON_LAZY_SYMBOL_POINTERS");
            break;

        case S_LAZY_SYMBOL_POINTERS:
            LOG_DEBUG("S_LAZY_SYMBOL_POINTERS");
            break;

        case S_SYMBOL_STUBS:
            LOG_DEBUG("S_SYMBOL_STUBS");
            break;

        case S_COALESCED:
            LOG_DEBUG("S_COALESCED");
            break;

        case S_GB_ZEROFILL:
            LOG_DEBUG("S_GB_ZEROFILL");
            break;

        case S_INTERPOSING:
            LOG_DEBUG("S_INTERPOSING");
            break;

        case S_DTRACE_DOF:
            LOG_DEBUG("S_DTRACE_DOF");
            break;

        case S_LAZY_DYLIB_SYMBOL_POINTERS:
            LOG_DEBUG("S_LAZY_DYLIB_SYMBOL_POINTERS");
            break;

        case S_THREAD_LOCAL_REGULAR:
            LOG_DEBUG("S_THREAD_LOCAL_REGULAR");
            break;

        case S_THREAD_LOCAL_ZEROFILL:
            LOG_DEBUG("S_THREAD_LOCAL_ZEROFILL");
            break;

        case S_THREAD_LOCAL_VARIABLES:
            LOG_DEBUG("S_THREAD_LOCAL_VARIABLES");
            break;

        case S_THREAD_LOCAL_VARIABLE_POINTERS:
            LOG_DEBUG("S_THREAD_LOCAL_VARIABLE_POINTERS");
            break;

        case S_THREAD_LOCAL_INIT_FUNCTION_POINTERS:
            LOG_DEBUG("S_THREAD_LOCAL_INIT_FUNCTION_POINTERS");
            break;

        default:
            LOG_WARN("Unknown section type 0x%.8x, ignoring", section_type);
            break;
    }

	if (section_usr_attr & S_ATTR_DEBUG) {
		LOG_DEBUG("S_ATTR_DEBUG");
	}

	if (section_usr_attr & S_ATTR_LIVE_SUPPORT) {
		LOG_DEBUG("S_ATTR_LIVE_SUPPORT");
	}

	if (section_usr_attr & S_ATTR_NO_DEAD_STRIP) {
		LOG_DEBUG("S_ATTR_NO_DEAD_STRIP");
	}

	if (section_usr_attr & S_ATTR_NO_TOC) {
		LOG_DEBUG("S_ATTR_NO_TOC");
	}

	if (section_usr_attr & S_ATTR_PURE_INSTRUCTIONS) {
		LOG_DEBUG("S_ATTR_PURE_INSTRUCTIONS");
	}

	if (section_usr_attr & S_ATTR_SELF_MODIFYING_CODE) {
		LOG_DEBUG("S_ATTR_SELF_MODIFYING_CODE");
	}

	if (section_usr_attr & S_ATTR_STRIP_STATIC_SYMS) {
		LOG_DEBUG("S_ATTR_STRIP_STATIC_SYMS");
	}

	if (section_sys_attr & S_ATTR_SOME_INSTRUCTIONS) {
		LOG_DEBUG("S_ATTR_SOME_INSTRUCTIONS");
	}

	if (section_sys_attr & S_ATTR_EXT_RELOC) {
		LOG_DEBUG("S_ATTR_EXT_RELOC");
	}
	if (section_sys_attr & S_ATTR_LOC_RELOC) {
		LOG_DEBUG("S_ATTR_LOC_RELOC");
	}

    return true;
}

template <typename Segment_t, typename Section_t> bool MachoBinary::parse_segment(struct load_command *lc) {
	Segment_t *cmd = m_data->pointer<Segment_t>(lc);

	add_segment<Segment_t>(cmd);

    LOG_DEBUG("name = %-16s | base = 0x%.16llx | size = 0x%.16llx", cmd->segname, cmd->vmaddr, cmd->vmsize);

    if (string(cmd->segname) == SEG_TEXT) {
    	LOG_DEBUG("m_base_address = %p", (void *) m_base_address);
    	m_base_address = cmd->vmaddr;
    }

    // Get a pointer to the first section.
    Section_t *cur_section = m_data->pointer<Section_t>(cmd + 1);

    // Parse each of the segments sections.
    for (unsigned i = 0; i < cmd->nsects; ++i) {
        // Check if the data does not go beyond our loaded memory.
        if (!m_data->valid(cur_section)) {
            LOG_ERR("Error, the current section (%u) goes beyond the mapped file", i);
            break;
        }

        LOG_DEBUG("Parsing section %d of %d", i, cmd->nsects);

        // Parse the section.
        if (!parse_section<Section_t>(cur_section)) {
            LOG_ERR("Error, could not parse section %u of %u, skipping", i, cmd->nsects);
            continue;
        }

        cur_section++;
    }

    return true;
}

template<typename Section_t> bool MachoBinary::parse_cstring_literals_section(Section_t *lc) {
	auto start = m_data->offset<const char>(lc->offset, lc->size);
	if (!start) {
		return false;
	}

	const char *end = start + lc->size;
	const char *cur_byte = start;
	const char *cur_string = cur_byte;

	while(cur_byte < end) {
		if (!*cur_byte) {
			LOG_DEBUG("String: %s", cur_string);
			cur_string = ++cur_byte;
			continue;
		}

		cur_byte++;
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_4byte_literals(Section_t *lc) {
	if (auto start = m_data->offset<uint32_t>(lc->offset, lc->size)) {
		for(unsigned i = 0; i < lc->size / sizeof(uint32_t); ++i) {
			LOG_DEBUG("Four byte literal: 0x%.8x", start[i]);
		}
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_8byte_literals(Section_t *lc) {
	if (auto start = m_data->offset<uint64_t>(lc->offset, lc->size)) {
		for(unsigned i = 0; i < lc->size / sizeof(uint64_t); ++i) {
			LOG_DEBUG("Eight byte literal: 0x%.16llx", start[i]);
		}
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_16byte_literals(Section_t *lc) {
	if (auto start = m_data->offset<uint32_t>(lc->offset, lc->size)) {
		for(unsigned i = 0; i < lc->size / sizeof(uint32_t); i += 4) {
			LOG_DEBUG("Sixteen byte literal: 0x%.8x 0x%.8x 0x%.8x 0x%.8x",
					start[i], start[i + 1], start[i + 2], start[i + 3]);
		}
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_literal_pointers(Section_t *lc) {
	if (auto start = m_data->offset<uint64_t>(lc->offset, lc->size)) {
		for(unsigned i = 0; i < lc->size / sizeof(uint64_t); ++i) {
			LOG_DEBUG("POINTER: 0x%.16llx", start[i]);
		}
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_mod_init_func_pointers(Section_t *lc) {
	for (uint64_t initializer = lc->addr; initializer < lc->addr + lc->size; initializer += pointer_size()) {
		LOG_DEBUG("  Initializer at: %p", (void *) (initializer + m_base_address));
	}

	return true;
}

template<typename Section_t> bool MachoBinary::parse_mod_term_func_pointers(Section_t *lc) {
    for (uint64_t terminator = lc->addr; terminator < lc->addr + lc->size; terminator += pointer_size()) {
		LOG_DEBUG("  Terminator at: %p", (void *) (terminator + m_base_address));
	}

	return true;
}

bool MachoBinary::parse_generic_symbol(struct nlist_64 *symbol) {
    if (symbol->n_type & N_PEXT) {
        // Private external symbol.
        LOG_DEBUG("N_PEXT");
    }

    if (symbol->n_type & N_EXT) {
        // External symbol.
        LOG_DEBUG("N_EXT");
    }

    switch (symbol->n_type & N_TYPE) {
        case N_UNDF:
            LOG_DEBUG("N_UNDF");
            break;
        case N_ABS:
            LOG_DEBUG("N_ABS");
            break;
        case N_SECT:
            LOG_DEBUG("N_SECT");
            break;
        case N_PBUD:
            LOG_DEBUG("N_PBUD");
            break;
        case N_INDR:
            LOG_DEBUG("N_INDR");
            break;
        default:
            LOG_ERR("Unknown symbol type %u, ignoring", symbol->n_type & N_TYPE);
            break;
    }

    // Get the description for symbols of type N_UNDF only.
    switch (symbol->n_desc & REFERENCE_TYPE) {
        case REFERENCE_FLAG_DEFINED:
            LOG_DEBUG("REFERENCE_FLAG_DEFINED");
            break;
        case REFERENCE_FLAG_PRIVATE_DEFINED:
            LOG_DEBUG("REFERENCE_FLAG_PRIVATE_DEFINED");
            break;
        case REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY:
            LOG_DEBUG("REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY");
            break;
        case REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY:
            LOG_DEBUG("REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY");
            break;
        case REFERENCE_FLAG_UNDEFINED_LAZY:
            LOG_DEBUG("REFERENCE_FLAG_UNDEFINED_LAZY");
            break;
        case REFERENCE_FLAG_UNDEFINED_NON_LAZY:
            LOG_DEBUG("REFERENCE_FLAG_UNDEFINED_NON_LAZY");
            break;
        default:
            LOG_ERR("Unknown reference type 0x%.2x, ignoring", symbol->n_desc & REFERENCE_TYPE);
            break;
    }

    LOG_DEBUG("symbol->n_desc = 0x%.2x", symbol->n_desc);
    LOG_DEBUG("symbol->n_desc = 0x%.2x", symbol->n_desc & REFERENCE_TYPE);

    if (symbol->n_desc & REFERENCED_DYNAMICALLY)
        LOG_DEBUG("REFERENCED_DYNAMICALLY");

    if (filetype() == MH_OBJECT && (symbol->n_desc & N_NO_DEAD_STRIP))
        LOG_DEBUG("N_NO_DEAD_STRIP");

    if (filetype() != MH_OBJECT && (symbol->n_desc & N_DESC_DISCARDED))
        LOG_DEBUG("N_DESC_DISCARDED");

    if (symbol->n_desc & N_WEAK_REF)
        LOG_DEBUG("N_WEAK_REF");

    if (symbol->n_desc & N_WEAK_DEF)
        LOG_DEBUG("N_WEAK_DEF");

    if (symbol->n_desc & N_REF_TO_WEAK)
        LOG_DEBUG("N_REF_TO_WEAK");

    if (symbol->n_desc & N_ARM_THUMB_DEF)
        LOG_DEBUG("N_ARM_THUMB_DEF");

    if (symbol->n_desc & N_SYMBOL_RESOLVER)
        LOG_DEBUG("N_SYMBOL_RESOLVER");

    if (symbol->n_desc & N_ALT_ENTRY)
        LOG_DEBUG("N_ALT_ENTRY");

    return true;
}

bool MachoBinary::parse_stab_symbol(struct nlist_64 *symbol) {
    switch (symbol->n_type & N_STAB) {
        /* Labeled as NO_SECT in stab.h */
        case N_GSYM:
        case N_FNAME:
        case N_RSYM:
        case N_SSYM:
        case N_LSYM:
        case N_BINCL:
        case N_PARAMS:
        case N_VERSION:
        case N_OLEVEL:
        case N_PSYM:
        case N_EINCL:
        case N_EXCL:
        case N_BCOMM:
        case N_LENG:
        case N_OPT:
        case N_OSO:
            // sym->is_absolute = 1;
            break;
            /* Labeled as n_sect in stab.h */
        case N_FUN:
        case N_STSYM:
        case N_LCSYM:
        case N_BNSYM:
        case N_SLINE:
        case N_ENSYM:
        case N_SO:
        case N_SOL:
        case N_ENTRY:
        case N_ECOMM:
        case N_ECOML:
            /* These are labeled as NO_SECT in stab.h, but they are actually
             * section-based on OS X.  We must mark them as such so they get
             * relocated.
             */
        case N_RBRAC:
        case N_LBRAC:
            // sym->is_section = 1;
            break;
        default:
            break;
    }
    return true;
}

bool MachoBinary::parse_symtab(struct load_command *lc) {
    struct symtab_command *cmd = m_data->pointer<struct symtab_command>(lc);

    // Save a reference to the symbol table.
    m_symbol_table_size = cmd->nsyms;
    m_symbol_table = m_data->offset<struct nlist_64>(cmd->symoff, cmd->nsyms * sizeof(struct nlist_64));
    if (!m_symbol_table) {
        LOG_ERR("Symbol table is outside the binary mapped file");
        return false;
    }

    // Save a reference to the string table.
    m_string_table_size = cmd->strsize;
    m_string_table = m_data->offset<char>(cmd->stroff, cmd->strsize);
    if (!m_string_table) {
        LOG_ERR("Symbol string table is outside the binary mapped file (offset=%u, size=%u)", cmd->stroff, cmd->strsize);
        return false;
    }

    for (unsigned i = 0; i < cmd->nsyms; ++i) {
        // Get the symbol name.
        unsigned idx = m_symbol_table[i].n_un.n_strx;
        if (idx >= m_string_table_size) {
            LOG_ERR("Symbol index (%u) is outside the string table.", idx);
            continue;
        }

        LOG_DEBUG("symbol->name = %s", idx ? &m_string_table[idx] : "(null)");

        // Get the section index.
        LOG_DEBUG("symbol->n_sect = 0x%.2x", m_symbol_table[i].n_sect);

        // Get the symbol value.
        LOG_DEBUG("symbol->n_value = 0x%.16llx", m_symbol_table[i].n_value);

        if (m_symbol_table[i].n_type & N_STAB) {
            parse_stab_symbol(&m_symbol_table[i]);
        } else {
            parse_generic_symbol(&m_symbol_table[i]);
        }
    }

    return true;
}

bool MachoBinary::parse_dysymtab(struct load_command *lc) {
    // Symbols used by the dynamic linker.
    // This is an additional segment that requires a prior symtab load command.
    struct dysymtab_command *cmd = m_data->pointer<struct dysymtab_command>(lc);

    // Verify that we have string and symbolic information.
    if (!m_symbol_table || !m_string_table) {
        LOG_ERR("Impossible to parse LC_DYSYMTAB without a LC_SYMTAB entry.");
        return false;
    }

    // List local symbols.
    for (unsigned i = cmd->ilocalsym; i < cmd->nlocalsym; ++i) {
        if (i >= m_symbol_table_size) {
            LOG_ERR("Symbol table entry %u is outside the binary mapped file", i);
            break;
        }

        unsigned idx = m_symbol_table[i].n_un.n_strx;
        LOG_DEBUG("Local symbol:");
        LOG_DEBUG("  symbol->name    = %s", idx ? &m_string_table[idx] : "(null)");
        LOG_DEBUG("  symbol->n_sect  = 0x%.2x", m_symbol_table[i].n_sect);
        LOG_DEBUG("  symbol->n_value = 0x%.16llx\n", m_symbol_table[i].n_value);
    }

    // External defined symbols.
    for (unsigned i = cmd->iextdefsym; i < cmd->nextdefsym; ++i) {
        if (i >= m_symbol_table_size) {
            LOG_ERR("Symbol table entry %u is outside the binary mapped file", i);
            break;
        }

        unsigned idx = m_symbol_table[i].n_un.n_strx;
        LOG_DEBUG("External defined symbol:");
        LOG_DEBUG("  symbol->name    = %s", idx ? &m_string_table[idx] : "(null)");
        LOG_DEBUG("  symbol->n_sect  = 0x%.2x", m_symbol_table[i].n_sect);
        LOG_DEBUG("  symbol->n_value = 0x%.16llx\n", m_symbol_table[i].n_value);
    }

    // External undefined symbols.
    for (unsigned i = cmd->iundefsym; i < cmd->nundefsym; ++i) {
        if (i >= m_symbol_table_size) {
            LOG_ERR("Symbol table entry %u is outside the binary mapped file", i);
            break;
        }

        unsigned idx = m_symbol_table[i].n_un.n_strx;
        LOG_DEBUG("External undefined symbol:");
        LOG_DEBUG("  symbol->name    = %s", idx ? &m_string_table[idx] : "(null)");
        LOG_DEBUG("  symbol->n_sect  = 0x%.2x", m_symbol_table[i].n_sect);
        LOG_DEBUG("  symbol->n_value = 0x%.16llx\n", m_symbol_table[i].n_value);
    }

    uint32_t tocoff; /* file offset to table of contents */
    uint32_t ntoc; /* number of entries in table of contents */

    LOG_DEBUG("tocoff       = 0x%.8x ntoc        = 0x%.8x modtaboff      = 0x%.8x nmodtab       = 0x%.8x", cmd->tocoff, cmd->ntoc,
            cmd->modtaboff, cmd->nmodtab);

    LOG_DEBUG("extrefsymoff = 0x%.8x nextrefsyms = 0x%.8x indirectsymoff = 0x%.8x nindirectsyms = 0x%.8x", cmd->extrefsymoff,
            cmd->nextrefsyms, cmd->indirectsymoff, cmd->nindirectsyms);

    LOG_DEBUG("extreloff    = 0x%.8x nextrel     = 0x%.8x locreloff      = 0x%.8x nlocrel       = 0x%.8x ", cmd->extreloff, cmd->nextrel,
            cmd->locreloff, cmd->nlocrel);

    return true;
}

bool MachoBinary::parse_thread(struct load_command *lc) {
    struct thread_command *cmd = m_data->pointer<struct thread_command>(lc);

    // Skip to the contents.
    uint32_t *contents = m_data->pointer<uint32_t>(cmd + 1);
    assert(contents == reinterpret_cast<uint32_t*>(cmd + 1));

    // After the thread_command we will find two uint32_t's.
    uint32_t flavor = contents[0];
    uint32_t count = contents[1];

    LOG_DEBUG("flavor = 0x%.8x count = 0x%.8x", flavor, count);

    // After these we will have the arch specific thread information.
    return true;
}

bool MachoBinary::parse_id_dylib(struct load_command *lc) {
    struct dylib_command *cmd = m_data->pointer<struct dylib_command>(lc);

    // Get the name of the this library.
    char *name = m_data->pointer<char>(reinterpret_cast<char *>(cmd) + cmd->dylib.name.offset);

    LOG_DEBUG("Current library: name=%-40s tstamp=0x%.8x ver=0x%.8x compat=0x%.8x", name, cmd->dylib.timestamp, cmd->dylib.current_version,
            cmd->dylib.compatibility_version);

    return true;
}

bool MachoBinary::parse_dylib(struct load_command *lc) {
    struct dylib_command *cmd = m_data->pointer<struct dylib_command>(lc);

    // Get the name of the imported library.
    std::string name = m_data->pointer<char>(reinterpret_cast<char *>(cmd) + cmd->dylib.name.offset);

    LOG_DEBUG("Imported library: name=%-40s tstamp=0x%.8x ver=0x%.8x compat=0x%.8x", name.c_str(), cmd->dylib.timestamp, cmd->dylib.current_version,
            cmd->dylib.compatibility_version);

    std::string base_filename = name;
    if (auto idx = name.find_last_of("/\\")) {
    	base_filename = name.substr(idx + 1);
    }

    m_imported_libs.push_back(base_filename);

    return true;
}

bool MachoBinary::parse_main(struct load_command *lc) {
    struct entry_point_command *cmd = m_data->pointer<struct entry_point_command>(lc);

    LOG_DEBUG("entryoff=0x%.16llx stacksize=0x%.16llx", cmd->entryoff, cmd->stacksize);

    return true;
}

bool MachoBinary::parse_unixthread(struct load_command *lc) {
    struct thread_command *cmd = m_data->pointer<struct thread_command>(lc);

    // Skip to the contents.
    uint32_t *contents = m_data->pointer<uint32_t>(cmd + 1);
    assert(contents == reinterpret_cast<uint32_t*>(cmd + 1));

    // After the thread_command we will find two uint32_t's.
    uint32_t flavor = contents[0];
    uint32_t count = contents[1];

    LOG_DEBUG("flavor = 0x%.8x count = 0x%.8x", flavor, count);

    // After these we will have the arch specific thread information.
    return true;
}

bool MachoBinary::parse_encryption_info_32(struct load_command *lc) {
	// This commands identify a range of the file that is encrypted.
	struct encryption_info_command *cmd = m_data->pointer<struct encryption_info_command>(lc);
	LOG_DEBUG("cryptoff = 0x%.8x cryptsize = 0x%.8x cryptid = 0x%.8x", cmd->cryptoff, cmd->cryptsize, cmd->cryptid);
	return true;
}

bool MachoBinary::parse_encryption_info_64(struct load_command *lc) {
	// This commands identify a range of the file that is encrypted.
	struct encryption_info_command_64 *cmd = m_data->pointer<struct encryption_info_command_64>(lc);
	LOG_DEBUG("cryptoff = 0x%.8x cryptsize = 0x%.8x cryptid = 0x%.8x", cmd->cryptoff, cmd->cryptsize, cmd->cryptid);
	return true;
}

template<> void MachoBinary::add_segment<segment_command>(segment_command *cmd) {
	m_segments_32.push_back(*cmd);
};

template<> void MachoBinary::add_segment<segment_command_64>(segment_command_64 *cmd) {
	m_segments_64.push_back(*cmd);
};

template<> void MachoBinary::add_section<section>(section *cmd) {
	m_sections_32.push_back(*cmd);
};

template<> void MachoBinary::add_section<section_64>(section_64 *cmd) {
	m_sections_64.push_back(*cmd);
};

std::string MachoBinary::segment_name(unsigned index) {
	if (is64()) {
		return (index < m_segments_64.size()) ?  m_segments_64[index].segname : "invalid";
	}

	return (index < m_segments_32.size()) ?  m_segments_32[index].segname : "invalid";
}

std::string MachoBinary::section_name(unsigned index, uint64_t address) {
	if (is64()) {
		for(auto section : m_sections_64) {
			if (address >= section.addr && address < (section.addr + section.size)) {
				return section.sectname;
			}
		}
	} else {
		for(auto section : m_sections_32) {
			if (address >= section.addr && address < (section.addr + section.size)) {
				return section.sectname;
			}
		}
	}

	return "invalid";
}

uint64_t MachoBinary::segment_address(unsigned index) {
	if (is64()) {
		return (index < m_segments_64.size()) ?  m_segments_64[index].vmaddr : 0;
	}

	return (index < m_segments_32.size()) ?  m_segments_32[index].vmaddr : 0;
}


bool MachoBinary::parse_dyld_info_exports(const uint8_t *export_start, const uint8_t *export_end) {
    struct Node;
    struct Edge {
    	Node *next;
    	string label;
    };

    struct Node {
    	vector<Edge *> m_children;
    	unsigned m_terminal_size;
    	const uint8_t *m_data;
    	uintptr_t m_offset;
    };

    // Start from offset zero.
	Node *init = new Node();
	init->m_offset = 0;

	// Setup the initial node.
    queue<Node *> working_set;
	working_set.push(init);

	const uint8_t* cur_byte = export_start;

	// Process all the nodes.
	while (!working_set.empty() && cur_byte < export_end) {
		// Get a Node from the queue.
		Node *cur_node = working_set.front();
		working_set.pop();

		// Get a pointer to the data.
		cur_byte = export_start + cur_node->m_offset;
		cur_node->m_data = cur_byte;

		// Read the terminal size.
		cur_node->m_terminal_size = read_terminal_size(cur_byte, export_end);

		// Skip the symbol properties to get to the children.
		cur_byte += cur_node->m_terminal_size;

		uint8_t child_count = *cur_byte++;
		for(unsigned i = 0; i < child_count; i++) {
			// Current child label.
			const char *edge_label = (const char *) cur_byte;

			// Skip the node label.
			cur_byte += strlen(edge_label) + 1;

			// Get the offset of the node.
			uintptr_t node_offset = read_uleb128(cur_byte, export_end);

			Node *new_node = new Node();
			new_node->m_offset = node_offset;

			Edge *new_edge = new Edge();
			new_edge->next = new_node;
			new_edge->label = edge_label;

			cur_node->m_children.push_back(new_edge);
			working_set.push(new_node);
		}
	}

	function<void(Node *, vector<string> &vec)> dfs_printer = [&dfs_printer](Node *node, vector<string> &vec) {
		if (node->m_terminal_size) {
			string joined;
			for(const auto &el : vec) {
				joined += el;
			}

			LOG_DEBUG("label = %s", joined.c_str());
		}

		for(Edge *edge : node->m_children) {
			vec.push_back(edge->label);
			dfs_printer(edge->next, vec);
			vec.pop_back();
		}
	};

	vector<string> vec;
	dfs_printer(init, vec);

	#define EXPORT_SYMBOL_FLAGS_KIND_MASK				0x03
	#define EXPORT_SYMBOL_FLAGS_KIND_REGULAR			0x00
	#define EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL		0x01
	#define EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION			0x04
	#define EXPORT_SYMBOL_FLAGS_REEXPORT				0x08
	#define EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER		0x10

	return true;
}

std::string rebaseTypeName(uint8_t type) {
	switch (type) {
	case REBASE_TYPE_POINTER:
		return "pointer";
	case REBASE_TYPE_TEXT_ABSOLUTE32:
		return "text abs32";
	case REBASE_TYPE_TEXT_PCREL32:
		return "text rel32";
	}

	return "!!unknown!!";
}

std::string bindTypeName(uint8_t type) {
	switch (type) {
	case BIND_TYPE_POINTER:
		return "pointer";
	case BIND_TYPE_TEXT_ABSOLUTE32:
		return "text abs32";
	case BIND_TYPE_TEXT_PCREL32:
		return "text rel32";
	}
	return "!!unknown!!";
}

bool MachoBinary::parse_dyld_info_rebase(const uint8_t *start, const uint8_t *end) {
	auto p = start;
	auto done = false;

	uint8_t type = 0;
	uint8_t seg_index = 0;
	uint64_t seg_offset = 0;
	int64_t addend = 0;
	uint32_t count;
	uint32_t skip;
	uint64_t seg_addr = 0;
	std::string seg_name = "??", sec_name = "???";
	std::string type_name = "??";
	uintptr_t address = 0;

	printf("rebase information (from compressed dyld info):\n");
	printf("segment section          address             type\n");

	while(!done && p < end) {
		uint8_t imm = *p & REBASE_IMMEDIATE_MASK;
		uint8_t opcode = *p & REBASE_OPCODE_MASK;
		p++;

		switch (opcode) {
		case REBASE_OPCODE_DONE:
			done = true;
			break;

		case REBASE_OPCODE_SET_TYPE_IMM:
			type = imm;
			type_name = rebaseTypeName(type);
			break;

		case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			seg_index = imm;
			seg_offset = read_uleb128(p, end);
			seg_addr = segment_address(seg_index);
			seg_name = segment_name(seg_index);
			break;

		case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
			seg_offset += imm * pointer_size();
			break;

		case REBASE_OPCODE_ADD_ADDR_ULEB:
			seg_offset += read_uleb128(p, end);
			break;

		case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
			for (int i=0; i < imm; ++i) {
				sec_name = section_name(seg_index, seg_addr + seg_offset);
				printf("%-7s %-16s 0x%08llX  %s REBASE_OPCODE_DO_REBASE_IMM_TIMES\n",
						seg_name.c_str(), sec_name.c_str(), seg_addr + seg_offset, type_name.c_str());
				seg_offset += pointer_size();
			}
			break;

		case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
			sec_name = section_name(seg_index, seg_addr + seg_offset);
			printf("%-7s %-16s 0x%08llX  %s REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB\n",
					seg_name.c_str(), sec_name.c_str(), seg_addr + seg_offset, type_name.c_str());
			seg_offset += read_uleb128(p, end) + pointer_size();
			break;

		case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
			count = read_uleb128(p, end);
			for (uint32_t i = 0; i < count; ++i) {
				sec_name = section_name(seg_index, seg_addr + seg_offset);
				printf("%-7s %-16s 0x%08llX  %s REBASE_OPCODE_DO_REBASE_ULEB_TIMES\n",
						seg_name.c_str(), sec_name.c_str(), seg_addr + seg_offset, type_name.c_str());
				seg_offset += pointer_size();
			}
			break;

		case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
			count = read_uleb128(p, end);
			skip = read_uleb128(p, end);
			for (uint32_t i = 0; i < count; ++i) {
				sec_name = section_name(seg_index, seg_addr + seg_offset);
				printf("%-7s %-16s 0x%08llX  %s REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB\n",
						seg_name.c_str(), sec_name.c_str(), seg_addr + seg_offset, type_name.c_str());
				seg_offset += skip + pointer_size();
			}
			break;

		default:
			LOG_ERR("Invalid rebase opcode! (%.2x)", opcode);
			break;
		}
	}

	return true;
}

std::string MachoBinary::ordinal_name(int libraryOrdinal) {
	switch (libraryOrdinal) {
	case BIND_SPECIAL_DYLIB_SELF:
		return "this-image";
	case BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE:
		return "main-executable";
	case BIND_SPECIAL_DYLIB_FLAT_LOOKUP:
		return "flat-namespace";
	}

	if (libraryOrdinal < BIND_SPECIAL_DYLIB_FLAT_LOOKUP
			|| libraryOrdinal > m_imported_libs.size())
		return "invalid";

	return m_imported_libs[libraryOrdinal - 1];
}


bool MachoBinary::parse_dyld_info_binding(const uint8_t *start, const uint8_t *end) {
	printf("bind information:\n");
	printf("segment section          address        type    addend dylib            symbol\n");
	const uint8_t* p = start;


	uint8_t type = 0;
	uint8_t segIndex = 0;
	uint64_t segOffset = 0;
	std::string symbolName = "";
	std::string fromDylib = "??";
	int libraryOrdinal = 0;
	int64_t addend = 0;
	uint32_t count;
	uint32_t skip;
	uint64_t segStartAddr = 0;
	std::string segName = "??";
	std::string typeName = "??";
	std::string weak_import = "";
	bool done = false;

	while (!done && (p < end)) {
		uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
		uint8_t opcode = *p & BIND_OPCODE_MASK;
		++p;

		switch (opcode) {
			case BIND_OPCODE_DONE:
				done = true;
				break;

			case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
				libraryOrdinal = immediate;
				fromDylib = ordinal_name(libraryOrdinal);
				break;

			case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
				libraryOrdinal = read_uleb128(p, end);
				fromDylib = ordinal_name(libraryOrdinal);
				break;

			case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
				// the special ordinals are negative numbers
				if ( immediate == 0 )
					libraryOrdinal = 0;
				else {
					int8_t signExtended = BIND_OPCODE_MASK | immediate;
					libraryOrdinal = signExtended;
				}
				fromDylib = ordinal_name(libraryOrdinal);
				break;
			case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
				symbolName = (char*)p;
				while (*p != '\0')
					++p;
				++p;
				if ( (immediate & BIND_SYMBOL_FLAGS_WEAK_IMPORT) != 0 )
					weak_import = " (weak import)";
				else
					weak_import = "";
				break;
			case BIND_OPCODE_SET_TYPE_IMM:
				type = immediate;
				typeName = bindTypeName(type);
				break;
			case BIND_OPCODE_SET_ADDEND_SLEB:
				addend = read_sleb128(p, end);
				break;
			case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
				segIndex = immediate;
				segStartAddr = segment_address(segIndex);
				segName = segment_name(segIndex);
				segOffset = read_uleb128(p, end);
				break;
			case BIND_OPCODE_ADD_ADDR_ULEB:
				segOffset += read_uleb128(p, end);
				break;
			case BIND_OPCODE_DO_BIND:
				printf("%-7s %-16s 0x%08llX %10s  %5lld %-16s %s%s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						fromDylib.c_str(),
						symbolName.c_str(),
						weak_import.c_str()
				);

				segOffset += pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
				printf("%-7s %-16s 0x%08llX %10s  %5lld %-16s %s%s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						fromDylib.c_str(),
						symbolName.c_str(),
						weak_import.c_str()
				);

				segOffset += read_uleb128(p, end) + pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
				printf("%-7s %-16s 0x%08llX %10s  %5lld %-16s %s%s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						fromDylib.c_str(),
						symbolName.c_str(),
						weak_import.c_str()
				);

				segOffset += immediate*pointer_size() + pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
				count = read_uleb128(p, end);
				skip = read_uleb128(p, end);
				for (uint32_t i=0; i < count; ++i) {
					printf("%-7s %-16s 0x%08llX %10s  %5lld %-16s %s%s\n",
							segName.c_str(),
							section_name(segIndex, segStartAddr+segOffset).c_str(),
							segStartAddr+segOffset,
							typeName.c_str(),
							addend,
							fromDylib.c_str(),
							symbolName.c_str(),
							weak_import.c_str()
					);

					segOffset += skip + pointer_size();
				}
				break;
			default:
				LOG_ERR("bad bind opcode %d", *p);
		}
	}

	return true;
}

bool MachoBinary::parse_dyld_info_weak_binding(const uint8_t *start, const uint8_t *end) {
	printf("weak binding information:\n");
	printf("segment section          address       type     addend symbol\n");
	const uint8_t* p = start;

	uint8_t type = 0;
	uint8_t segIndex = 0;
	uint64_t segOffset = 0;
	std::string symbolName = "";;
	int64_t addend = 0;
	uint32_t count;
	uint32_t skip;
	uint64_t segStartAddr = 0;
	std::string segName = "??";
	std::string typeName = "??";
	bool done = false;
	while ( !done && (p < end) ) {
		uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
		uint8_t opcode = *p & BIND_OPCODE_MASK;
		++p;
		switch (opcode) {
			case BIND_OPCODE_DONE:
				done = true;
				break;
			case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
				symbolName = (char*)p;
				while (*p != '\0')
					++p;
				++p;
				if ( (immediate & BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION) != 0 )
					printf("                                       strong          %s\n", symbolName.c_str());
				break;
			case BIND_OPCODE_SET_TYPE_IMM:
				type = immediate;
				typeName = bindTypeName(type);
				break;
			case BIND_OPCODE_SET_ADDEND_SLEB:
				addend = read_sleb128(p, end);
				break;
			case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
				segIndex = immediate;
				segStartAddr = segment_address(segIndex);
				segName = segment_name(segIndex);
				segOffset = read_uleb128(p, end);
				break;
			case BIND_OPCODE_ADD_ADDR_ULEB:
				segOffset += read_uleb128(p, end);
				break;
			case BIND_OPCODE_DO_BIND:
				printf("%-7s %-16s 0x%08llX %10s   %5lld %s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						symbolName.c_str()
				);

				segOffset += pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
				printf("%-7s %-16s 0x%08llX %10s   %5lld %s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						symbolName.c_str()
				);

				segOffset += read_uleb128(p, end) + pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
				printf("%-7s %-16s 0x%08llX %10s   %5lld %s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						typeName.c_str(),
						addend,
						symbolName.c_str()
				);

				segOffset += immediate*pointer_size() + pointer_size();
				break;
			case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
				count = read_uleb128(p, end);
				skip = read_uleb128(p, end);
				for (uint32_t i=0; i < count; ++i) {
					printf("%-7s %-16s 0x%08llX %10s   %5lld %s\n",
							segName.c_str(),
							section_name(segIndex, segStartAddr+segOffset).c_str(),
							segStartAddr+segOffset,
							typeName.c_str(),
							addend,
							symbolName.c_str()
					);

					segOffset += skip + pointer_size();
				}
				break;
			default:
				LOG_ERR("unknown weak bind opcode %d", *p);
		}
	}

	return true;
}

bool MachoBinary::parse_dyld_info_lazy_binding(const uint8_t *start, const uint8_t *end) {
	printf("lazy binding information (from lazy_bind part of dyld info):\n");
	printf("segment section          address    index  dylib            symbol\n");

	uint8_t type = BIND_TYPE_POINTER;
	uint8_t segIndex = 0;
	uint64_t segOffset = 0;
	std::string symbolName = "";;
	std::string fromDylib = "??";
	int libraryOrdinal = 0;
	int64_t addend = 0;
	uint32_t lazy_offset = 0;
	uint64_t segStartAddr = 0;
	std::string segName = "??";
	std::string typeName = "??";
	std::string weak_import = "";
	for (const uint8_t* p=start; p < end; ) {
		uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
		uint8_t opcode = *p & BIND_OPCODE_MASK;
		++p;
		switch (opcode) {
			case BIND_OPCODE_DONE:
				lazy_offset = p-start;
				break;
			case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
				libraryOrdinal = immediate;
				fromDylib = ordinal_name(libraryOrdinal);
				break;
			case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
				libraryOrdinal = read_uleb128(p, end);
				fromDylib = ordinal_name(libraryOrdinal);
				break;
			case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
				// the special ordinals are negative numbers
				if ( immediate == 0 )
					libraryOrdinal = 0;
				else {
					int8_t signExtended = BIND_OPCODE_MASK | immediate;
					libraryOrdinal = signExtended;
				}
				fromDylib = ordinal_name(libraryOrdinal);
				break;
			case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
				symbolName = (char*)p;
				while (*p != '\0')
					++p;
				++p;
				if ( (immediate & BIND_SYMBOL_FLAGS_WEAK_IMPORT) != 0 )
					weak_import = " (weak import)";
				else
					weak_import = "";
				break;
			case BIND_OPCODE_SET_TYPE_IMM:
				type = immediate;
				typeName = bindTypeName(type);
				break;
			case BIND_OPCODE_SET_ADDEND_SLEB:
				addend = read_sleb128(p, end);
				break;
			case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
				segIndex = immediate;
				segStartAddr = segment_address(segIndex);
				segName = segment_name(segIndex);
				segOffset = read_uleb128(p, end);
				break;
			case BIND_OPCODE_ADD_ADDR_ULEB:
				segOffset += read_uleb128(p, end);
				break;
			case BIND_OPCODE_DO_BIND:
				printf("%-7s %-16s 0x%08llX 0x%04X %-16s %s%s\n",
						segName.c_str(),
						section_name(segIndex, segStartAddr+segOffset).c_str(),
						segStartAddr+segOffset,
						lazy_offset,
						fromDylib.c_str(),
						symbolName.c_str(),
						weak_import.c_str()
				);

				segOffset += pointer_size();
				break;
			default:
				LOG_ERR("bad lazy bind opcode %d", *p);
		}
	}

	return true;
}

bool MachoBinary::parse_dyld_info(struct load_command *lc) {
    struct dyld_info_command *cmd = m_data->pointer<struct dyld_info_command>(lc);

    LOG_DEBUG("Rebase information: rebase_off = 0x%.8x rebase_size = 0x%.8x", cmd->rebase_off, cmd->rebase_size);
    LOG_DEBUG("Binding information: bind_off = 0x%.8x bind_size = 0x%.8x", cmd->bind_off, cmd->bind_size);
    LOG_DEBUG("Weak binding information: weak_bind_off = 0x%.8x weak_bind_size = 0x%.8x", cmd->weak_bind_off, cmd->weak_bind_size);
    LOG_DEBUG("Lazy binding information: lazy_bind_off = 0x%.8x lazy_bind_size = 0x%.8x", cmd->lazy_bind_off, cmd->lazy_bind_size);
    LOG_DEBUG("Export information: export_off = 0x%.8x export_size = 0x%.8x", cmd->export_off, cmd->export_size);

    // Parse rebase information.
    if (auto start = m_data->offset<const uint8_t>(cmd->rebase_off, cmd->rebase_size)) {
        auto end = start + cmd->rebase_size;
        parse_dyld_info_rebase(start, end);
    }

    // Parse binding information.
    if (auto start = m_data->offset<const uint8_t>(cmd->bind_off, cmd->bind_size)) {
        auto end = start + cmd->bind_size;
        parse_dyld_info_binding(start, end);
    }

    // Parse weak binding information.
    if (auto start = m_data->offset<const uint8_t>(cmd->weak_bind_off, cmd->weak_bind_size)) {
        auto end = start + cmd->weak_bind_size;
        parse_dyld_info_weak_binding(start, end);
    }

    // Parse lazy binding information.
    if (auto start = m_data->offset<const uint8_t>(cmd->lazy_bind_off, cmd->lazy_bind_size)) {
        auto end = start + cmd->lazy_bind_size;
        parse_dyld_info_lazy_binding(start, end);
    }

    // Parse the exports information.
    if (auto start = m_data->offset<const uint8_t>(cmd->export_off, cmd->export_size)) {
        auto end = start + cmd->export_size;
        parse_dyld_info_exports(start, end);
    }

    return true;
}