/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       Implements several optimizations for ARM.
 * @author      Michael Beck
 */
#include "irgmod.h"
#include "ircons.h"
#include "iredges.h"
#include "panic.h"

#include "benode.h"
#include "bepeephole.h"
#include "besched.h"

#include "arm_optimize.h"
#include "gen_arm_regalloc_if.h"
#include "gen_arm_new_nodes.h"
#include "arm_nodes_attr.h"
#include "arm_new_nodes.h"

static uint32_t arm_ror(uint32_t v, uint32_t ror)
{
	return (v << (32 - ror)) | (v >> ror);
}

/**
 * Returns non.zero if the given offset can be directly encoded into an ARM
 * instruction.
 */
static bool allowed_arm_immediate(int offset, arm_vals *result)
{
	arm_gen_vals_from_word(offset, result);
	return result->ops <= 1;
}

/**
 * Fix an IncSP node if the offset gets too big
 */
static void peephole_be_IncSP(ir_node *node)
{
	/* first optimize incsp->incsp combinations */
	node = be_peephole_IncSP_IncSP(node);

	int offset = be_get_IncSP_offset(node);
	/* can be transformed into Add OR Sub */
	int sign = 1;
	if (offset < 0) {
		sign = -1;
		offset = -offset;
	}
	arm_vals v;
	if (allowed_arm_immediate(offset, &v))
		return;

	be_set_IncSP_offset(node, sign * arm_ror(v.values[0], v.rors[0]));

	ir_node *first = node;
	ir_node *block = get_nodes_block(node);
	for (unsigned cnt = 1; cnt < v.ops; ++cnt) {
		int      value = sign * arm_ror(v.values[cnt], v.rors[cnt]);
		ir_node *incsp = be_new_IncSP(&arm_registers[REG_SP], block, node,
		                             value, 1);
		sched_add_after(node, incsp);
		node = incsp;
	}

	/* reattach IncSP users */
	edges_reroute_except(first, node, sched_next(first));
}

/**
 * creates the address by Adds
 */
static ir_node *gen_ptr_add(ir_node *node, ir_node *frame, const arm_vals *v)
{
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_node  *block = get_nodes_block(node);
	ir_node  *ptr   = new_bd_arm_Add_imm(dbgi, block, frame, v->values[0],
	                                     v->rors[0]);
	arch_set_irn_register(ptr, &arm_registers[REG_R12]);
	sched_add_before(node, ptr);

	for (unsigned cnt = 1; cnt < v->ops; ++cnt) {
		ir_node *next = new_bd_arm_Add_imm(dbgi, block, ptr, v->values[cnt],
		                                   v->rors[cnt]);
		arch_set_irn_register(next, &arm_registers[REG_R12]);
		sched_add_before(node, next);
		ptr = next;
	}
	return ptr;
}

/**
* creates the address by Subs
*/
static ir_node *gen_ptr_sub(ir_node *node, ir_node *frame, const arm_vals *v)
{
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_node  *block = get_nodes_block(node);
	ir_node  *ptr   = new_bd_arm_Sub_imm(dbgi, block, frame, v->values[0],
	                                     v->rors[0]);
	arch_set_irn_register(ptr, &arm_registers[REG_R12]);
	sched_add_before(node, ptr);

	for (unsigned cnt = 1; cnt < v->ops; ++cnt) {
		ir_node *next = new_bd_arm_Sub_imm(dbgi, block, ptr, v->values[cnt],
		                                   v->rors[cnt]);
		arch_set_irn_register(next, &arm_registers[REG_R12]);
		sched_add_before(node, next);
		ptr = next;
	}
	return ptr;
}

/** fix frame addresses which are too big */
static void peephole_arm_FrameAddr(ir_node *node)
{
	arm_Address_attr_t *attr   = get_arm_Address_attr(node);
	int                 offset = attr->fp_offset;
	arm_vals            v;
	if (allowed_arm_immediate(offset, &v))
		return;

	ir_node *base = get_irn_n(node, n_arm_FrameAddr_base);
	/* TODO: suboptimal */
	ir_node *ptr = gen_ptr_add(node, base, &v);

	attr->fp_offset = 0;
	set_irn_n(node, n_arm_FrameAddr_base, ptr);
}

/**
 * Fix stackpointer relative stores if the offset gets too big
 */
static void peephole_arm_Str_Ldr(ir_node *node)
{
	arm_load_store_attr_t *attr    = get_arm_load_store_attr(node);
	const int              offset  = attr->offset;
	arm_vals               v;
	if (allowed_arm_immediate(offset, &v))
		return;

	/* we should only have too big offsets for frame entities */
	if (!attr->is_frame_entity) {
		fprintf(stderr,
		        "POSSIBLE ARM BACKEND PROBLEM: offset in Store too big\n");
	}
	bool use_add = offset >= 0;

	ir_node *ptr;
	if (is_arm_Str(node)) {
		ptr = get_irn_n(node, n_arm_Str_ptr);
	} else {
		assert(is_arm_Ldr(node));
		ptr = get_irn_n(node, n_arm_Ldr_ptr);
	}

	if (use_add) {
		ptr = gen_ptr_add(node, ptr, &v);
	} else {
		ptr = gen_ptr_sub(node, ptr, &v);
	}

	/* TODO: sub-optimal, the last offset could probably be left inside the
	   store */
	if (is_arm_Str(node)) {
		set_irn_n(node, n_arm_Str_ptr, ptr);
	} else {
		assert(is_arm_Ldr(node));
		set_irn_n(node, n_arm_Ldr_ptr, ptr);
	}
	attr->offset = 0;
}

/**
 * Register a peephole optimization function.
 */
static void register_peephole_optimization(ir_op *op, peephole_opt_func func)
{
	assert(op->ops.generic == NULL);
	op->ops.generic = (op_func)func;
}

/* Perform peephole-optimizations. */
void arm_peephole_optimization(ir_graph *irg)
{
	/* register peephole optimizations */
	ir_clear_opcodes_generic_func();
	register_peephole_optimization(op_be_IncSP,      peephole_be_IncSP);
	register_peephole_optimization(op_arm_Str,       peephole_arm_Str_Ldr);
	register_peephole_optimization(op_arm_Ldr,       peephole_arm_Str_Ldr);
	register_peephole_optimization(op_arm_FrameAddr, peephole_arm_FrameAddr);

	be_peephole_opt(irg);
}
