/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       Dumps global variables and constants as gas assembler.
 * @author      Christian Wuerdig, Matthias Braun
 * @date        04.11.2005
 */
#ifndef FIRM_BE_BEGNUAS_H
#define FIRM_BE_BEGNUAS_H

#include <stdbool.h>
#include "be_types.h"
#include "beemitter.h"
#include "bedwarf.h"

typedef enum {
	GAS_SECTION_TEXT,            /**< text section - program code */
	GAS_SECTION_DATA,            /**< data section - arbitrary data */
	GAS_SECTION_RODATA,          /**< rodata section - read-only data */
	GAS_SECTION_BSS,             /**< bss section - zero initialized data */
	GAS_SECTION_CONSTRUCTORS,    /**< ctors section */
	GAS_SECTION_DESTRUCTORS,     /**< dtors section */
	GAS_SECTION_JCR,             /**< java class registry */
	GAS_SECTION_CSTRING,         /**< section for constant strings */
	GAS_SECTION_PIC_TRAMPOLINES, /**< trampolines for pic codes */
	GAS_SECTION_PIC_SYMBOLS,     /**< contains resolved pic symbols */
	GAS_SECTION_DEBUG_INFO,      /**< dwarf debug info */
	GAS_SECTION_DEBUG_ABBREV,    /**< dwarf debug abbrev */
	GAS_SECTION_DEBUG_LINE,      /**< dwarf debug line */
	GAS_SECTION_DEBUG_PUBNAMES,  /**< dwarf pub names */
	GAS_SECTION_DEBUG_FRAME,     /**< dwarf callframe infos */
	GAS_SECTION_LAST = GAS_SECTION_DEBUG_FRAME,
	GAS_SECTION_TYPE_MASK    = 0xFF,

	GAS_SECTION_FLAG_TLS     = 1 << 8,  /**< thread local flag */
	GAS_SECTION_FLAG_COMDAT  = 1 << 9
} be_gas_section_t;
ENUM_BITSET(be_gas_section_t)

typedef enum elf_variant_t {
	ELF_VARIANT_NORMAL,
	ELF_VARIANT_SPARC
} elf_variant_t;

extern bool          be_gas_emit_types;
extern elf_variant_t be_gas_elf_variant;

/**
 * the .type directive needs to specify @function, #function or %function
 * depending on the target architecture
 */
extern char                 be_gas_elf_type_char;

/**
 * Switch the current output section to the given out.
 *
 * @param section  the new output section
 */
void be_gas_emit_switch_section(be_gas_section_t section);

/**
 * emit assembler instructions necessary before starting function code
 */
void be_gas_emit_function_prolog(const ir_entity *entity,
                                 unsigned po2alignment,
                                 const parameter_dbg_info_t *paramter_infos);

void be_gas_emit_function_epilog(const ir_entity *entity);

char const *be_gas_get_private_prefix(void);

/**
 * emit ld_ident of an entity and performs additional mangling if necessary.
 * (mangling is necessary for ir_visibility_private for example).
 * Emits a block label for type_code entities.
 */
void be_gas_emit_entity(const ir_entity *entity);

/**
 * Emit (a private) symbol name for a firm block
 */
void be_gas_emit_block_name(const ir_node *block);

/**
 * Starts a basic block. Emits an assembler label "blockname:" if needs_label
 * is true, otherwise a comment with the blockname if verboseasm is enabled.
 */
void be_gas_begin_block(const ir_node *block, bool needs_label);

/**
 * emit a string (takes care of escaping special chars)
 */
void be_gas_emit_cstring(const char *string);

/**
 * emit a string literal. This function takes care of escaping special chars.
 */
void be_gas_emit_string_literal(const char *string);

/**
 * Starts emitting a compilation unit. This emits:
 *  - global assembler snippets
 *  - debug info
 */
void be_gas_begin_compilation_unit(const be_main_env_t *env);

/**
 * ends a compilation unit. This emits:
 *  - global declarations/variables
 *  - debug info
 */
void be_gas_end_compilation_unit(const be_main_env_t *env);

/**
 * Return the label prefix for labeled instructions.
 */
const char *be_gas_insn_label_prefix(void);

typedef ir_node* (*get_cfop_target_func)(const ir_node *cfop);

/**
 * Emits a jump table for switch operations
 */
void be_emit_jump_table(const ir_node *node, const ir_switch_table *table,
                        ir_entity *entity,
                        get_cfop_target_func get_cfop_target);

bool be_gas_produces_dwarf_line_info(void);

#endif
