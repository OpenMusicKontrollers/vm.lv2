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
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/parameters/parameters.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#include <timely.lv2/timely.h>

#define VM_URI                "http://open-music-kontrollers.ch/lv2/vm"
#define VM_PREFIX             VM_URI"#"

#define VM__vm                VM_PREFIX"vm"
#define VM__vm_ui             VM_PREFIX"vm_ui"

#define VM__graph             VM_PREFIX"graph"

#define MAX_NPROPS 1
#define CTRL_MAX   0x8
#define CTRL_MASK  (CTRL_MAX - 1)
#define ITEMS_MAX  128
#define GRAPH_SIZE (ITEMS_MAX * sizeof(LV2_Atom_Long))

#include <props.lv2/props.h>

typedef enum _vm_status_t vm_status_t;
typedef enum _vm_opcode_enum_t vm_opcode_enum_t;
typedef enum _vm_command_enum_t vm_command_enum_t;
typedef struct _vm_command_t vm_command_t;
typedef struct _vm_api_def_t vm_api_def_t;
typedef struct _vm_api_impl_t vm_api_impl_t;
typedef struct _plugstate_t plugstate_t;

enum _vm_status_t {
	VM_STATUS_STATIC   = (0 << 0),
	VM_STATUS_HAS_TIME = (1 << 1),
	VM_STATUS_HAS_RAND = (1 << 2),
};

enum _vm_opcode_enum_t {
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
	OP_LSHIFT,
	OP_RSHIFT,
	OP_TER,
	OP_STORE,
	OP_LOAD,
	OP_RAND,
	OP_BAR_BEAT,
	OP_BAR,
	OP_BEAT,
	OP_BEAT_UNIT,
	OP_BPB,
	OP_BPM,
	OP_FRAME,
	OP_FPS,
	OP_SPEED,

	OP_MAX,
};

enum _vm_command_enum_t {
	COMMAND_NOP = 0,

	COMMAND_OPCODE,
	COMMAND_BOOL,
	COMMAND_INT,
	COMMAND_LONG,
	COMMAND_FLOAT,
	COMMAND_DOUBLE,

	COMMAND_MAX,
};

struct _vm_command_t {
	vm_command_enum_t type;

	union {
		int32_t i32;
		int64_t i64;
		float f32;
		double f64;
		vm_opcode_enum_t op;
	};
};

struct _vm_api_def_t {
	const char *uri;
	const char *mnemo;
	const char *label;
	unsigned npops;
	unsigned npushs;
};

struct _vm_api_impl_t {
	LV2_URID urid;
};

struct _plugstate_t {
	uint8_t graph [GRAPH_SIZE];
};

static const char *command_labels [COMMAND_MAX] = {
	[COMMAND_NOP]    = "",
	[COMMAND_OPCODE] = "Op Code"	,
	[COMMAND_BOOL]   = "Boolean"	,
	[COMMAND_INT]    = "Integer"	,
	[COMMAND_LONG]   = "Long"	,
	[COMMAND_FLOAT]  = "Float"	,
	[COMMAND_DOUBLE] = "Double"
};

static const vm_api_def_t vm_api_def [OP_MAX] = {
	[OP_NOP]  = {
		.uri    = VM_PREFIX"opNop",
		.label  = "",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 0
	},
	[OP_CTRL] = {
		.uri    = VM_PREFIX"opInput",
		.label  = "input",
		.mnemo  = NULL,
		.npops  = 1,
		.npushs = 1
	},
	[OP_PUSH] = {
		.uri    = VM_PREFIX"opPush",
		.label  = "Push top",
		.mnemo  = "push",
		.npops  = 1,
		.npushs = 2
	},
	[OP_ADD]  = {
		.uri    = VM_PREFIX"opAdd",
		.label  = "Add",
		.mnemo  = "+",
		.npops  = 2,
		.npushs = 1
	},
	[OP_SUB]  = {
		.uri    = VM_PREFIX"opSub",
		.label  = "Subtract",
		.mnemo  = "-",
		.npops  = 2,
		.npushs = 1
	},
	[OP_MUL]  = {
		.uri    = VM_PREFIX"opMul",
		.label  = "Multiply",
		.mnemo  = "*",
		.npops  = 2,
		.npushs = 1
	},
	[OP_DIV]  = {
		.uri    = VM_PREFIX"opDiv",
		.label  = "Divide",
		.mnemo  = "/",
		.npops  = 2,
		.npushs = 1
	},
	[OP_NEG]  = {
		.uri    = VM_PREFIX"opNeg",
		.label  = "Negate",
		.mnemo  = "neg",
		.npops  = 1,
		.npushs = 1
	},
	[OP_ABS]  = {
		.uri    = VM_PREFIX"opAbs",
		.label  = "Absolute",
		.mnemo  = "abs",
		.npops  = 1,
		.npushs = 1
	},
	[OP_POW]  = {
		.uri    = VM_PREFIX"opPow",
		.label  = "Power",
		.mnemo  = "^",
		.npops  = 2,
		.npushs = 1
	},
	[OP_SQRT]  = {
		.uri    = VM_PREFIX"opSqrt",
		.label  = "Square root",
		.mnemo  = "sqrt",
		.npops  = 1,
		.npushs = 1
	},
	[OP_MOD]  = {
		.uri    = VM_PREFIX"opMod",
		.label  = "Modulo",
		.mnemo  = "%",
		.npops  = 2,
		.npushs = 1
	},
	[OP_EXP]  = {
		.uri    = VM_PREFIX"opExp",
		.label  = "Exponential",
		.mnemo  = "exp",
		.npops  = 1,
		.npushs = 1
	},
	[OP_EXP_2]  = {
		.uri    = VM_PREFIX"opExp2",
		.label  = "Exponential base 2",
		.mnemo  = "exp2",
		.npops  = 1,
		.npushs = 1
	},
	[OP_LOG]  = {
		.uri    = VM_PREFIX"opLog",
		.label  = "Logarithm",
		.mnemo  = "log",
		.npops  = 1,
		.npushs = 1
	},
	[OP_LOG_2]  = {
		.uri    = VM_PREFIX"opLog2",
		.label  = "Logarithm base 2",
		.mnemo  = "log2",
		.npops  = 1,
		.npushs = 1
	},
	[OP_LOG_10]  = {
		.uri    = VM_PREFIX"opLog10",
		.label  = "Logarithm base 10",
		.mnemo  = "log10",
		.npops  = 1,
		.npushs = 1
	},
	[OP_SIN]  = {
		.uri    = VM_PREFIX"opSin",
		.label  = "Sinus",
		.mnemo  = "sin",
		.npops  = 1,
		.npushs = 1
	},
	[OP_COS]  = {
		.uri    = VM_PREFIX"opCos",
		.label  = "Cosinus",
		.mnemo  = "cos",
		.npops  = 1,
		.npushs = 1
	},
	[OP_SWAP]  = {
		.uri    = VM_PREFIX"opSwap",
		.label  = "Swap",
		.mnemo  = "swap",
		.npops  = 2,
		.npushs = 2
	},
	[OP_PI]  = {
		.uri    = VM_PREFIX"opPi",
		.label  = "Pi",
		.mnemo  = "pi",
		.npops  = 0,
		.npushs = 1
	},
	[OP_EQ]  = {
		.uri    = VM_PREFIX"opEq",
		.label  = "Equal",
		.mnemo  = "==",
		.npops  = 2,
		.npushs = 1
	},
	[OP_LT]  = {
		.uri    = VM_PREFIX"opLt",
		.label  = "Less than",
		.mnemo  = "<",
		.npops  = 2,
		.npushs = 1
	},
	[OP_GT]  = {
		.uri    = VM_PREFIX"opGt",
		.label  = "Greater than",
		.mnemo  = ">",
		.npops  = 2,
		.npushs = 1
	},
	[OP_LE]  = {
		.uri    = VM_PREFIX"opLe",
		.label  = "Less or equal",
		.mnemo  = "<=",
		.npops  = 2,
		.npushs = 1
	},
	[OP_GE]  = {
		.uri    = VM_PREFIX"opGe",
		.label  = "Greater or equal",
		.mnemo  = ">=",
		.npops  = 2,
		.npushs = 1
	},
	[OP_AND]  = {
		.uri    = VM_PREFIX"opAnd",
		.label  = "And",
		.mnemo  = "&&",
		.npops  = 2,
		.npushs = 1
	},
	[OP_OR]  = {
		.uri    = VM_PREFIX"opOr",
		.label  = "Or",
		.mnemo  = "||",
		.npops  = 2,
		.npushs = 1
	},
	[OP_NOT]  = {
		.uri    = VM_PREFIX"opNot",
		.label  = "Not",
		.mnemo  = "!",
		.npops  = 1,
		.npushs = 1
	},
	[OP_BAND]  = {
		.uri    = VM_PREFIX"opBAnd",
		.label  = "Bitwise and",
		.mnemo  = "&",
		.npops  = 2,
		.npushs = 1
	},
	[OP_BOR]  = {
		.uri    = VM_PREFIX"opBOr",
		.label  = "Bitwise or",
		.mnemo  = "|",
		.npops  = 2,
		.npushs = 1
	},
	[OP_BNOT]  = {
		.uri    = VM_PREFIX"opBNot",
		.label  = "Bitwise not",
		.mnemo  = "~",
		.npops  = 1,
		.npushs = 1
	},
	[OP_LSHIFT]  = {
		.uri    = VM_PREFIX"opLShift",
		.label  = "Left shift",
		.mnemo  = "<<",
		.npops  = 2,
		.npushs = 1
	},
	[OP_RSHIFT]  = {
		.uri    = VM_PREFIX"opRShift",
		.label  = "Right shift",
		.mnemo  = ">>",
		.npops  = 2,
		.npushs = 1
	},
	[OP_TER]  = {
		.uri    = VM_PREFIX"opTernary",
		.label  = "Ternary operator",
		.mnemo  = "?",
		.npops  = 3,
		.npushs = 1
	},
	[OP_STORE]  = {
		.uri    = VM_PREFIX"opStore",
		.label  = "Store in register",
		.mnemo  = "store",
		.npops  = 2,
		.npushs = 0
	},
	[OP_LOAD]  = {
		.uri    = VM_PREFIX"opLoad",
		.label  = "Load from register",
		.mnemo  = "load",
		.npops  = 1,
		.npushs = 1
	},
	[OP_RAND]  = {
		.uri    = VM_PREFIX"opRand",
		.label  = "Random number",
		.mnemo  = "rand",
		.npops  = 0,
		.npushs = 1
	},
	[OP_BAR_BEAT]  = {
		.uri    = LV2_TIME__barBeat,
		.label  = "time:barBeat",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_BAR]  = {
		.uri    = LV2_TIME__bar,
		.label  = "time:bar",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_BEAT]  = {
		.uri    = LV2_TIME__beat,
		.label  = "time:beat",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_BEAT_UNIT]  = {
		.uri    = LV2_TIME__beatUnit,
		.label  = "time:beatUnit",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_BPB]  = {
		.uri    = LV2_TIME__beatsPerBar,
		.label  = "time:beatsPerBar",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_BPM]  = {
		.uri    = LV2_TIME__beatsPerMinute,
		.label  = "time:beatsPerMinute",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_FRAME]  = {
		.uri    = LV2_TIME__frame,
		.label  = "time:frame",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_FPS]  = {
		.uri    = LV2_TIME__framesPerSecond,
		.label  = "time:framesPerSecond",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
	[OP_SPEED]  = {
		.uri    = LV2_TIME__speed,
		.label  = "time:speed",
		.mnemo  = NULL,
		.npops  = 0,
		.npushs = 1
	},
};

static inline void
vm_api_init(vm_api_impl_t *impl, LV2_URID_Map *map)
{
	for(unsigned op = 0; op < OP_MAX; op++)
	{
		impl[op].urid = map->map(map->handle, vm_api_def[op].uri);
	}
}

static inline vm_opcode_enum_t
vm_api_unmap(vm_api_impl_t *impl, LV2_URID urid)
{
	for(unsigned op = 0; op < OP_MAX; op++)
	{
		if(impl[op].urid == urid)
			return op;
	}

	return OP_NOP;
}

static inline LV2_URID
vm_api_map(vm_api_impl_t *impl, vm_opcode_enum_t op)
{
	return impl[op].urid;
}

static inline LV2_Atom_Forge_Ref
vm_serialize(vm_api_impl_t *impl, LV2_Atom_Forge *forge, const vm_command_t *cmds)
{
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_tuple(forge, &frame);

	for(unsigned i = 0; i < ITEMS_MAX; i++)
	{
		const vm_command_t *cmd = &cmds[i];
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
				const LV2_URID otype = vm_api_map(impl, cmd->op);
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

static inline vm_status_t
vm_deserialize(vm_api_impl_t *impl, LV2_Atom_Forge *forge,
	vm_command_t *cmds, uint32_t size, const LV2_Atom *body)
{
	vm_command_t *cmd = cmds;
	memset(cmds, 0x0, sizeof(vm_command_t)*ITEMS_MAX);

	vm_status_t state = VM_STATUS_STATIC;

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
			cmd->op = vm_api_unmap(impl, ((const LV2_Atom_URID *)item)->body);

			if(  (cmd->op == OP_BAR_BEAT)
				|| (cmd->op == OP_BAR)
				|| (cmd->op == OP_BEAT)
				|| (cmd->op == OP_BEAT_UNIT)
				|| (cmd->op == OP_BPB)
				|| (cmd->op == OP_BPM)
				|| (cmd->op == OP_FRAME)
				//|| (cmd->op == OP_FPS) // is constant
				|| (cmd->op == OP_SPEED) )
			{
				state |= VM_STATUS_HAS_TIME;
			}
			else if(cmd->op == OP_RAND)
			{
				state |= VM_STATUS_HAS_RAND;
			}
		}
		else
		{
			break;
		}

		cmd += 1;

		if(cmd >= cmds + ITEMS_MAX)
			break;
	}

	return state;
}

#endif // _VM_LV2_H
