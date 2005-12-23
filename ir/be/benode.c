/**
 * @file   benode.c
 * @date   17.05.2005
 * @author Sebastian Hack
 *
 * Backend node support.
 *
 * Copyright (C) 2005 Universitaet Karlsruhe
 * Released under the GPL
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "obst.h"
#include "set.h"
#include "pmap.h"
#include "util.h"
#include "debug.h"

#include "irop_t.h"
#include "irmode_t.h"
#include "irnode_t.h"
#include "ircons_t.h"
#include "irprintf.h"

#include "be_t.h"
#include "belive_t.h"
#include "besched_t.h"
#include "benode_t.h"

#include "beirgmod.h"

#define DBG_LEVEL 0

typedef enum _node_kind_t {
  node_kind_spill,
  node_kind_reload,
  node_kind_perm,
  node_kind_copy,
  node_kind_last
} node_kind_t;

typedef struct {
  node_kind_t kind;
  const arch_register_class_t *cls;
  ir_op *op;
  int n_pos;
  int *pos;
} be_op_t;

typedef struct {
	const be_node_factory_t *factory;
	int n_regs;
	const arch_register_t *reg[1];
} be_node_attr_t;

typedef struct {
	be_node_attr_t attr;
	ir_node *spill_ctx;
} be_spill_attr_t;

static int templ_pos_Spill[] = {
	0
};

static int templ_pos_Reload[] = {
	-1
};

static int templ_pos_Copy[] = {
	0, -1
};

static int dump_node(ir_node *irn, FILE *f, dump_reason_t reason);

static const ir_op_ops be_node_ops = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	dump_node,
	NULL
};

static be_node_attr_t *init_node_attr(ir_node *irn,
		const be_node_factory_t *fact, int n_regs)
{
	be_node_attr_t *attr = (be_node_attr_t *) &irn->attr;
	int i;

	attr->n_regs  = n_regs;
	attr->factory = fact;

	for(i = 0; i < n_regs; ++i)
		attr->reg[i] = NULL;

	return attr;
}

#define ARRSIZE(x) (sizeof(x) / sizeof(x[0]))

static int cmp_op_map(const void *a, const void *b, size_t size)
{
  const be_op_t *x = a;
  const be_op_t *y = b;

  return !(x->kind == y->kind && x->cls == y->cls);
}

static be_op_t *get_op(const be_node_factory_t *fact,
    const arch_register_class_t *cls, node_kind_t kind)
{
  be_op_t templ;

  templ.kind = kind;
  templ.cls = cls;

  return set_insert(fact->ops, &templ, sizeof(templ),
      HASH_PTR(cls) + 7 * kind);
}

ir_node *new_Spill(const be_node_factory_t *factory,
    const arch_register_class_t *cls,
    ir_graph *irg, ir_node *bl, ir_node *node_to_spill, ir_node *ctx)
{
	be_spill_attr_t *attr;
	ir_node *irn;
  ir_node *in[1];
  ir_op *op = get_op(factory, cls, node_kind_spill)->op;

  assert(op && "Spill opcode must be present for this register class");
  in[0] = node_to_spill;
	irn  = new_ir_node(NULL, irg, bl, op, mode_M, 1, in);
	attr = (be_spill_attr_t *) init_node_attr(irn, factory, 0);
	attr->spill_ctx = ctx;

  return irn;
}

ir_node *new_Reload(const be_node_factory_t *factory,
    const arch_register_class_t *cls, ir_graph *irg,
    ir_node *bl, ir_mode *mode, ir_node *spill_node)
{
  ir_node *irn, *in[1];
  ir_op *op = get_op(factory, cls, node_kind_reload)->op;

  assert(op && "Reload opcode must be present for this register class");
  // assert(is_Spill(factory, spill_node) && "Operand of Reload must be a Spill");
  in[0] = spill_node;

  irn  = new_ir_node(NULL, irg, bl, op, mode, 1, in);
	init_node_attr(irn, factory, 1);

	return irn;
}

ir_node *new_Perm(const be_node_factory_t *factory,
    const arch_register_class_t *cls,
    ir_graph *irg, ir_node *bl, int arity, ir_node **in)
{
	ir_node *irn;
  ir_op *op = get_op(factory, cls, node_kind_perm)->op;

  irn  = new_ir_node(NULL, irg, bl, op, mode_T, arity, in);
	init_node_attr(irn, factory, arity);

	return irn;
}

ir_node *new_Copy(const be_node_factory_t *factory,
    const arch_register_class_t *cls,
    ir_graph *irg, ir_node *bl, ir_node *in)
{
  ir_node *irn, *ins[1];
  ir_op *op = get_op(factory, cls, node_kind_copy)->op;

  ins[0] = in;

  irn  = new_ir_node(NULL, irg, bl, op, get_irn_mode(in), 1, ins);
	init_node_attr(irn, factory, 1);

	return irn;
}

ir_node *be_spill(
		const be_node_factory_t *factory,
		const arch_env_t *arch_env,
		ir_node *irn, ir_node *ctx)
{
  const arch_register_class_t *cls = arch_get_irn_reg_class(arch_env, irn, -1);

  ir_node *bl    = get_nodes_block(irn);
  ir_graph *irg  = get_irn_irg(bl);
  ir_node *spill = new_Spill(factory, cls, irg, bl, irn, ctx);
	ir_node *insert;

	/*
	 * search the right insertion point. a spill of a phi cannot be put
	 * directly after the phi, if there are some phis behind the one which
	 * is spilled.
	 */
	insert = sched_next(irn);
	while(is_Phi(insert) && !sched_is_end(insert))
		insert = sched_next(insert);

	sched_add_before(insert, spill);
  return spill;
}

ir_node *be_reload(const be_node_factory_t *factory,
				   const arch_env_t *arch_env,
				   const arch_register_class_t *cls,
				   ir_node *irn, int pos, ir_mode *mode, ir_node *spill)
{
	ir_node *reload;

  ir_node *bl   = get_nodes_block(irn);
  ir_graph *irg = get_irn_irg(bl);

	assert(is_Spill(factory, spill)
			|| (is_Phi(spill) && get_irn_mode(spill) == mode_M));

  reload = new_Reload(factory, cls, irg, bl, mode, spill);

	set_irn_n(irn, pos, reload);
  sched_add_before(irn, reload);
  return reload;
}

/**
 * If the node is a proj, reset the node to the proj's target and return
 * the proj number.
 * @param node The address of a node pointer.
 * @param def  A default value.
 * @return     If *node is a Proj, *node is set to the Proj's target and
 *             the Proj number is returned. Else *node remains the same and @p def
 *             is returned.
 */
static int redir_proj(const ir_node **node, int def)
{
  const ir_node *n = *node;

  if(is_Proj(n)) {
    *node = get_Proj_pred(n);
    def = -(get_Proj_proj(n) + 1);
  }

  return def;
}

static const arch_register_req_t *
be_node_get_irn_reg_req(const arch_irn_ops_t *_self,
		arch_register_req_t *req, const ir_node *irn, int pos)
{
	be_op_t *bo;
	const be_node_factory_t *factory =
		container_of(_self, const be_node_factory_t, irn_ops);

	if(get_irn_mode(irn) == mode_T && pos < 0)
		return NULL;

	/*
	 * were interested in an output operand, so
	 * let's resolve projs.
	 */
	if(pos < 0)
		pos = redir_proj((const ir_node **) &irn, pos);

	bo = pmap_get(factory->irn_op_map, get_irn_op(irn));

	if(bo) {
		int i;

		req->type = arch_register_req_type_normal;
		req->cls  = bo->cls;

		for(i = 0; i < bo->n_pos; ++i)
			if(pos == bo->pos[i])
				return req;
	}

	return NULL;
}

void
be_node_set_irn_reg(const arch_irn_ops_t *_self, ir_node *irn,
    const arch_register_t *reg)
{
	int pos;
	be_op_t *bo;
	be_node_attr_t *attr;
	const be_node_factory_t *factory =
		container_of(_self, const be_node_factory_t, irn_ops);

	if(get_irn_mode(irn) == mode_T && pos < 0)
		return;

	pos = redir_proj((const ir_node **) &irn, -1);
	bo = pmap_get(factory->irn_op_map, get_irn_op(irn));

	if(!bo)
		return;

	attr = (be_node_attr_t *) &irn->attr;
	attr->reg[-pos - 1] = reg;
}

const arch_register_t *
be_node_get_irn_reg(const arch_irn_ops_t *_self, const ir_node *irn)
{
	int i, pos;
	be_op_t *bo;
	const be_node_factory_t *factory =
		container_of(_self, const be_node_factory_t, irn_ops);

	if(get_irn_mode(irn) == mode_T && pos < 0)
		return NULL;

	pos = redir_proj((const ir_node **) &irn, -1);
	bo = pmap_get(factory->irn_op_map, get_irn_op(irn));

	if(!bo)
		return NULL;

	for(i = 0; i < bo->n_pos; ++i) {
		if(bo->pos[i] == pos) {
			be_node_attr_t *attr = (be_node_attr_t *) &irn->attr;
			return attr->reg[-pos - 1];
		}
	}

	return NULL;
}

arch_irn_class_t be_node_classify(const arch_irn_ops_t *_self, const ir_node *irn)
{
  const be_node_factory_t *factory =
    container_of(_self, const be_node_factory_t, irn_ops);

  be_op_t *bo;
  int idx;

  idx = redir_proj(&irn, 0);
  bo = pmap_get(factory->irn_op_map, get_irn_op(irn));

	switch(bo->kind) {
#define XXX(a) case node_kind_ ## a: return arch_irn_class_ ## a;
		XXX(spill)
		XXX(reload)
		XXX(perm)
		XXX(copy)
#undef XXX
		default:
		return 0;
	}

  return 0;
}

arch_irn_class_t be_node_get_flags(const arch_irn_ops_t *_self, const ir_node *irn)
{
	return 0;
}

static const arch_irn_ops_t *
be_node_get_irn_ops(const arch_irn_handler_t *_self, const ir_node *irn)
{
  be_op_t *bo;
  const be_node_factory_t *factory =
    container_of(_self, const be_node_factory_t, handler);

  redir_proj(&irn, 0);
  bo = pmap_get(factory->irn_op_map, get_irn_op(irn));

  return bo ? &factory->irn_ops : NULL;
}

const arch_irn_handler_t *be_node_get_irn_handler(const be_node_factory_t *f)
{
  return &f->handler;
}

int is_Spill(const be_node_factory_t *f, const ir_node *irn)
{
  be_op_t *bo;
  bo = pmap_get(f->irn_op_map, get_irn_op(irn));
  return bo != NULL && bo->kind == node_kind_spill;
}

be_node_factory_t *be_node_factory_init(be_node_factory_t *factory, const arch_isa_t *isa)
{
  int i, j, n;

  factory->ops = new_set(cmp_op_map, 64);
  factory->irn_op_map = pmap_create();
  obstack_init(&factory->obst);

  factory->handler.get_irn_ops = be_node_get_irn_ops;

  factory->irn_ops.get_irn_reg_req = be_node_get_irn_reg_req;
  factory->irn_ops.set_irn_reg     = be_node_set_irn_reg;
  factory->irn_ops.get_irn_reg     = be_node_get_irn_reg;
  factory->irn_ops.classify        = be_node_classify;
  factory->irn_ops.get_flags       = be_node_get_flags;

  for(i = 0, n = arch_isa_get_n_reg_class(isa); i < n; ++i) {
    const arch_register_class_t *cls = arch_isa_get_reg_class(isa, i);
    be_op_t *ent;

    ent = get_op(factory, cls, node_kind_spill);
    ent->op = new_ir_op(get_next_ir_opcode(), "Spill", op_pin_state_pinned,
        0, oparity_unary, 0, sizeof(be_spill_attr_t), &be_node_ops);
    ent->n_pos = ARRSIZE(templ_pos_Spill);
    ent->pos = templ_pos_Spill;
    pmap_insert(factory->irn_op_map, ent->op, ent);

    ent = get_op(factory, cls, node_kind_reload);
    ent->op = new_ir_op(get_next_ir_opcode(), "Reload", op_pin_state_pinned, 0,
        oparity_unary, 0, sizeof(be_node_attr_t), &be_node_ops);
    ent->n_pos = ARRSIZE(templ_pos_Reload);
    ent->pos = templ_pos_Reload;
    pmap_insert(factory->irn_op_map, ent->op, ent);

    ent = get_op(factory, cls, node_kind_copy);
    ent->op = new_ir_op(get_next_ir_opcode(), "Copy", op_pin_state_pinned, 0,
        oparity_unary, 0, sizeof(be_node_attr_t), &be_node_ops);
    ent->n_pos = ARRSIZE(templ_pos_Copy);
    ent->pos = templ_pos_Copy;
    pmap_insert(factory->irn_op_map, ent->op, ent);

    ent = get_op(factory, cls, node_kind_perm);
    ent->op = new_ir_op(get_next_ir_opcode(), "Perm", op_pin_state_pinned, 0,
        oparity_variable, 0,
				sizeof(be_node_attr_t) + sizeof(arch_register_t) * cls->n_regs, &be_node_ops);
    ent->n_pos = 2 * cls->n_regs;
    ent->pos = obstack_alloc(&factory->obst, sizeof(ent->pos[0]) * ent->n_pos);
    for(j = 0; j < ent->n_pos; j += 2) {
    	int k = j / 2;
      ent->pos[j] = k;
      ent->pos[j + 1] = -(k + 1);
    }
    pmap_insert(factory->irn_op_map, ent->op, ent);

  }

  return factory;
}

static int dump_node(ir_node *irn, FILE *f, dump_reason_t reason)
{
	be_node_attr_t *attr = (be_node_attr_t *) &irn->attr;
  be_op_t *bo          = pmap_get(attr->factory->irn_op_map, get_irn_op(irn));

	int i;

	switch(reason) {
		case dump_node_opcode_txt:
			fprintf(f, get_op_name(bo->op));
			break;
		case dump_node_mode_txt:
			fprintf(f, get_mode_name(get_irn_mode(irn)));
			break;
		case dump_node_nodeattr_txt:
			fprintf(f, "%s ", bo->cls->name);
			break;
		case dump_node_info_txt:
			for(i = 0; i < attr->n_regs; ++i) {
				const arch_register_t *reg = attr->reg[i];
				fprintf(f, "reg #%d: %s\n", i, reg ? reg->name : "n/a");
			}

			if(bo->kind == node_kind_spill) {
				be_spill_attr_t *a = (be_spill_attr_t *) attr;
				ir_fprintf(f, "spill context: %+F\n", a->spill_ctx);
			}
			break;
	}

	return 1;
}

ir_node *insert_Perm_after(const be_main_env_t *env,
						   const arch_register_class_t *cls,
						   dom_front_info_t *dom_front,
						   ir_node *pos)
{
  const arch_env_t *arch_env  = env->arch_env;
  ir_node *bl                 = is_Block(pos) ? pos : get_nodes_block(pos);
  ir_graph *irg               = get_irn_irg(bl);
  pset *live                  = pset_new_ptr_default();
  firm_dbg_module_t *dbg      = firm_dbg_register("firm.be.node");

  irn_live_t *li;
  ir_node *curr, *irn, *perm, **nodes;
  int i, n;

  firm_dbg_set_mask(dbg, DBG_LEVEL);
  DBG((dbg, LEVEL_1, "Insert Perm after: %+F\n", pos));


	live_foreach(bl, li) {
		ir_node *irn = (ir_node *) li->irn;
		if(live_is_end(li) && arch_irn_has_reg_class(arch_env, irn, -1, cls))
			pset_insert_ptr(live, irn);
	}

  sched_foreach_reverse(bl, irn) {
    ir_node *x;

	/*
	 * If we encounter the node we want to insert the Perm after,
	 * exit immediately, so that this node is still live
	 */
    if(irn == pos)
      break;

    DBG((dbg, LEVEL_1, "%+F\n", irn));
    for(x = pset_first(live); x; x = pset_next(live))
      DBG((dbg, LEVEL_1, "\tlive: %+F\n", x));

    if(arch_irn_has_reg_class(arch_env, irn, -1, cls))
      pset_remove_ptr(live, irn);

    for(i = 0, n = get_irn_arity(irn); i < n; ++i) {
      ir_node *op = get_irn_n(irn, i);

      if(arch_irn_has_reg_class(arch_env, op, -1, cls))
        pset_insert_ptr(live, op);
    }
  }

  n = pset_count(live);
  nodes = malloc(n * sizeof(nodes[0]));

  DBG((dbg, LEVEL_1, "live:\n"));
  for(irn = pset_first(live), i = 0; irn; irn = pset_next(live), i++) {
  	DBG((dbg, LEVEL_1, "\t%+F\n", irn));
    nodes[i] = irn;
  }

  perm = new_Perm(env->node_factory, cls, irg, bl, n, nodes);
  sched_add_after(pos, perm);
  free(nodes);

  curr = perm;
  for(i = 0; i < n; ++i) {
    ir_node *copies[1];
    ir_node *perm_op = get_irn_n(perm, i);
	const arch_register_t *reg = arch_get_irn_register(arch_env, perm_op);

    ir_mode *mode = get_irn_mode(perm_op);
    ir_node *proj = new_r_Proj(irg, bl, perm, mode, i);
    arch_set_irn_register(arch_env, proj, reg);

    sched_add_after(curr, proj);
    curr = proj;

    copies[0] = proj;
    be_introduce_copies(dom_front, perm_op, array_size(copies), copies);
  }
  return perm;
}
