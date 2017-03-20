/*
 * Copyright (c) 2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _VM_LV2_H
#define _VM_LV2_H

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define VM_URI               "http://open-music-kontrollers.ch/lv2/vm"
#define VM_PREFIX            VM_URI"#"

#define VM__vm               VM_PREFIX"vm"
#define VM__vm_ui            VM_PREFIX"vm_ui"

#define VM__graph            VM_PREFIX"graph"

#define VM__opNop            VM_PREFIX"opNop"
#define VM__opControl        VM_PREFIX"opControl"
#define VM__opPush           VM_PREFIX"opPush"
#define VM__opAdd            VM_PREFIX"opAdd"
#define VM__opSub            VM_PREFIX"opSub"
#define VM__opMul            VM_PREFIX"opMul"
#define VM__opDiv            VM_PREFIX"opDiv"
#define VM__opNeg            VM_PREFIX"opNeg"
#define VM__opAbs            VM_PREFIX"opAbs"
#define VM__opPow            VM_PREFIX"opPow"
#define VM__opSqrt           VM_PREFIX"opSqrt"
#define VM__opMod            VM_PREFIX"opMod"
#define VM__opExp            VM_PREFIX"opExp"
#define VM__opExp2           VM_PREFIX"opExp2"
#define VM__opLog            VM_PREFIX"opLog"
#define VM__opLog2           VM_PREFIX"opLog2"
#define VM__opLog10          VM_PREFIX"opLog10"
#define VM__opSin            VM_PREFIX"opSin"
#define VM__opCos            VM_PREFIX"opCos"
#define VM__opSwap           VM_PREFIX"opSwap"
#define VM__opFrame          VM_PREFIX"opFrame"
#define VM__opSampleRate     VM_PREFIX"opSampleRate"
#define VM__opPi             VM_PREFIX"opPi"
#define VM__opEq             VM_PREFIX"opEq"
#define VM__opLt             VM_PREFIX"opLt"
#define VM__opGt             VM_PREFIX"opGt"
#define VM__opLe             VM_PREFIX"opLe"
#define VM__opGe             VM_PREFIX"opGe"
#define VM__opAnd            VM_PREFIX"opAnd"
#define VM__opOr             VM_PREFIX"opOr"
#define VM__opNot            VM_PREFIX"opNot"
#define VM__opBAnd           VM_PREFIX"opBAnd"
#define VM__opBOr            VM_PREFIX"opBOr"
#define VM__opBNot           VM_PREFIX"opBNot"
#define VM__opTernary        VM_PREFIX"opTernary"
#define VM__opStore          VM_PREFIX"opStore"
#define VM__opLoad           VM_PREFIX"opLoad"

#define MAX_NPROPS 1
#define CTRL_MAX   0x8
#define CTRL_MASK  (CTRL_MAX - 1)
#define ITEMS_MAX  128
#define GRAPH_SIZE (ITEMS_MAX * sizeof(LV2_Atom_Long))

#include <props.lv2/props.h>

typedef enum _opcode_enum_t opcode_enum_t;
typedef enum _command_enum_t command_enum_t;
typedef struct _opcode_t opcode_t;
typedef struct _command_t command_t;
typedef struct _plugstate_t plugstate_t;

enum _opcode_enum_t{
	OP_NOP = 0,

	OP_CTRL,
	OP_PUSH,
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_NEG,
	OP_ABS,
	OP_POW,
	OP_SQRT,
	OP_MOD,
	OP_EXP,
	OP_EXP_2,
	OP_LOG,
	OP_LOG_2,
	OP_LOG_10,
	OP_SIN,
	OP_COS,
	OP_SWAP,
	OP_FRAME,
	OP_SRATE,
	OP_PI,
	OP_EQ,
	OP_LT,
	OP_GT,
	OP_LE,
	OP_GE,
	OP_AND,
	OP_OR,
	OP_NOT,
	OP_BAND,
	OP_BOR,
	OP_BNOT,
	OP_TER,
	OP_STORE,
	OP_LOAD,

	OP_MAX,
};

enum _command_enum_t {
	COMMAND_NOP = 0,

	COMMAND_OPCODE,
	COMMAND_BOOL,
	COMMAND_INT,
	COMMAND_LONG,
	COMMAND_FLOAT,
	COMMAND_DOUBLE,

	COMMAND_MAX,
};

struct _opcode_t {
	LV2_URID op [OP_MAX];
};

struct _command_t {
	command_enum_t type;

	union {
		int32_t i32;
		int64_t i64;
		float f32;
		double f64;
		opcode_enum_t op;
	};
};

struct _plugstate_t {
	uint8_t graph [GRAPH_SIZE];
};

static const char *opcode_uris [OP_MAX] = {
	[OP_NOP]    = VM__opNop,
	[OP_CTRL]   = VM__opControl,
	[OP_PUSH]   = VM__opPush,
	[OP_ADD]    = VM__opAdd,
	[OP_SUB]    = VM__opSub,
	[OP_MUL]    = VM__opMul,
	[OP_DIV]    = VM__opDiv,
	[OP_NEG]    = VM__opNeg,
	[OP_ABS]    = VM__opAbs,
	[OP_POW]    = VM__opPow,
	[OP_SQRT]   = VM__opSqrt,
	[OP_MOD]    = VM__opMod,
	[OP_EXP]    = VM__opExp,
	[OP_EXP_2]  = VM__opExp2,
	[OP_LOG]    = VM__opLog,
	[OP_LOG_2]  = VM__opLog2,
	[OP_LOG_10] = VM__opLog10,
	[OP_SIN]    = VM__opSin,
	[OP_COS]    = VM__opCos,
	[OP_SWAP]   = VM__opSwap,
	[OP_FRAME]  = VM__opFrame,
	[OP_SRATE]  = VM__opSampleRate,
	[OP_PI]     = VM__opPi,
	[OP_EQ]     = VM__opEq,
	[OP_LT]     = VM__opLt,
	[OP_GT]     = VM__opGt,
	[OP_LE]     = VM__opLe,
	[OP_GE]     = VM__opGe,
	[OP_AND]    = VM__opAnd,
	[OP_OR]     = VM__opOr,
	[OP_NOT]    = VM__opNot,
	[OP_BAND]   = VM__opBAnd,
	[OP_BOR]    = VM__opBOr,
	[OP_BNOT]   = VM__opBNot,
	[OP_TER]    = VM__opTernary,
	[OP_STORE]  = VM__opStore,
	[OP_LOAD]   = VM__opLoad,
};

static inline void
vm_opcode_init(opcode_t *opcode, LV2_URID_Map *map)
{
	for(unsigned i = 0; i < OP_MAX; i++)
		opcode->op[i] = map->map(map->handle, opcode_uris[i]);
}

static inline opcode_enum_t
vm_opcode_unmap(opcode_t *opcode, LV2_URID otype)
{
	for(unsigned i = 0; i < OP_MAX; i++)
	{
		if(otype == opcode->op[i])
			return i;
	}

	return OP_NOP;
}

static inline LV2_URID
vm_opcode_map(opcode_t *opcode, opcode_enum_t op)
{
	return opcode->op[op];
}

static inline LV2_Atom_Forge_Ref
vm_serialize(opcode_t *opcode, LV2_Atom_Forge *forge, const command_t *cmds)
{
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_tuple(forge, &frame);

	for(unsigned i = 0; i < ITEMS_MAX; i++)
	{
		const command_t *cmd = &cmds[i];
		bool terminate = false;

		switch(cmd->type)
		{
			case COMMAND_BOOL:
			{
				if(ref)
					ref = lv2_atom_forge_bool(forge, cmd->i32);
			} break;
			case COMMAND_INT:
			{
				if(ref)
					ref = lv2_atom_forge_int(forge, cmd->i32);
			} break;
			case COMMAND_LONG:
			{
				if(ref)
					ref = lv2_atom_forge_long(forge, cmd->i64);
			} break;
			case COMMAND_FLOAT:
			{
				if(ref)
					ref = lv2_atom_forge_float(forge, cmd->f32);
			} break;
			case COMMAND_DOUBLE:
			{
				if(ref)
					ref = lv2_atom_forge_double(forge, cmd->f64);
			} break;
			case COMMAND_OPCODE:
			{
				const LV2_URID otype = vm_opcode_map(opcode, cmd->op);
				if(ref && otype)
					ref = lv2_atom_forge_urid(forge, otype);
			} break;
			case COMMAND_NOP:
			{
				terminate = true;
			} break;
			case COMMAND_MAX:
				break;
		}

		if(terminate)
			break;
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);

	return ref;
}

static inline bool
vm_deserialize(opcode_t *opcode, LV2_Atom_Forge *forge,
	command_t *cmds, uint32_t size, const LV2_Atom *body)
{
	command_t *cmd = cmds;
	memset(cmds, 0x0, sizeof(command_t)*ITEMS_MAX);

	bool is_dynamic = false;

	LV2_ATOM_TUPLE_BODY_FOREACH(body, size, item)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)item;

		if(item->type == forge->Bool)
		{
			cmd->type = COMMAND_BOOL;
			cmd->i32 = ((const LV2_Atom_Bool *)item)->body;
		}
		else if(item->type == forge->Int)
		{
			cmd->type = COMMAND_INT;
			cmd->i32 = ((const LV2_Atom_Int *)item)->body;
		}
		else if(item->type == forge->Long)
		{
			cmd->type = COMMAND_LONG;
			cmd->i64 = ((const LV2_Atom_Long *)item)->body;
		}
		else if(item->type == forge->Float)
		{
			cmd->type = COMMAND_FLOAT;
			cmd->f32 = ((const LV2_Atom_Float *)item)->body;
		}
		else if(item->type == forge->Double)
		{
			cmd->type = COMMAND_DOUBLE;
			cmd->f64 = ((const LV2_Atom_Double *)item)->body;
		}
		else if(item->type == forge->URID)
		{
			cmd->type = COMMAND_OPCODE;
			cmd->op = vm_opcode_unmap(opcode, ((const LV2_Atom_URID *)item)->body);

			if(cmd->op == OP_FRAME)
				is_dynamic = true;
		}
		else
		{
			break;
		}

		cmd += 1;

		if(cmd >= cmds + ITEMS_MAX)
			break;
	}

	return is_dynamic;
}

#endif // _VM_LV2_H
