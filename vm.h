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

#define VM_URI                "http://open-music-kontrollers.ch/lv2/vm"
#define VM_PREFIX             VM_URI"#"

#define VM__vm_ui             VM_PREFIX"vm_ui"

#define VM__graph             VM_PREFIX"graph"
#define VM__sourceFilter      VM_PREFIX"sourceFilter"
#define VM__destinationFilter VM_PREFIX"destinationFilter"

#define MAX_NPROPS 3

#define CTRL_MAX   0x8
#define CTRL_MASK  (CTRL_MAX - 1)

#define ITEMS_MAX  128
#define ITEMS_MASK (ITEMS_MAX - 1)
#define GRAPH_SIZE (ITEMS_MAX * sizeof(LV2_Atom_Long))
#define FILTER_SIZE 0x1000 // 4K

#define VM_MIN -1.f
#define VM_MAX 1.f
#define VM_STP 0.01f
#define VM_RNG (VM_MAX - VM_MIN)
#define VM_VIS (VM_RNG * 1.1f)

#include <props.lv2/props.h>

typedef enum _vm_plug_enum_t vm_plug_enum_t;
typedef enum _vm_status_t vm_status_t;
typedef enum _vm_opcode_enum_t vm_opcode_enum_t;
typedef enum _vm_command_enum_t vm_command_enum_t;
typedef enum _vm_filter_enum_t vm_filter_enum_t;
typedef struct _vm_command_t vm_command_t;
typedef struct _vm_api_def_t vm_api_def_t;
typedef struct _vm_api_impl_t vm_api_impl_t;
typedef struct _vm_filter_t vm_filter_t;
typedef struct _plugstate_t plugstate_t;

enum _vm_plug_enum_t {
	VM_PLUG_CONTROL = 0,
	VM_PLUG_CV,
	VM_PLUG_AUDIO,
	VM_PLUG_ATOM,
	VM_PLUG_MIDI
};

enum _vm_status_t {
	VM_STATUS_STATIC   = (0 << 0),
	VM_STATUS_HAS_TIME = (1 << 1),
	VM_STATUS_HAS_RAND = (1 << 2),
};

enum _vm_opcode_enum_t {
	OP_NOP = 0,

	OP_CTRL,
	OP_PUSH,
	OP_POP,
	OP_SWAP,
	OP_STORE,
	OP_LOAD,
	OP_BREAK,
	OP_GOTO,

	OP_RAND,

	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_POW,

	OP_NEG,
	OP_ABS,
	OP_SQRT,
	OP_CBRT,

	OP_FLOOR,
	OP_CEIL,
	OP_ROUND,
	OP_RINT,
	OP_TRUNC,
	OP_MODF,

	OP_EXP,
	OP_EXP_2,
	OP_LD_EXP,
	OP_FR_EXP,
	OP_LOG,
	OP_LOG_2,
	OP_LOG_10,

	OP_PI,
	OP_SIN,
	OP_COS,
	OP_TAN,
	OP_ASIN,
	OP_ACOS,
	OP_ATAN,
	OP_ATAN2,
	OP_SINH,
	OP_COSH,
	OP_TANH,
	OP_ASINH,
	OP_ACOSH,
	OP_ATANH,

	OP_EQ,
	OP_LT,
	OP_GT,
	OP_LE,
	OP_GE,
	OP_TER,
	OP_MINI,
	OP_MAXI,

	OP_AND,
	OP_OR,
	OP_NOT,

	OP_BAND,
	OP_BOR,
	OP_BNOT,
	OP_LSHIFT,
	OP_RSHIFT,

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
	COMMAND_FLOAT,

	COMMAND_MAX,
};

enum _vm_filter_enum_t {
	FILTER_CONTROLLER = 0,
};

struct _vm_command_t {
	vm_command_enum_t type;

	union {
		int32_t i32;
		float f32;
		vm_opcode_enum_t op;
	};
};

struct _vm_api_def_t {
	const char *uri;
	const char *label;
	const char *mnemo;
	char key;
	unsigned npops;
	unsigned npushs;
};

struct _vm_api_impl_t {
	LV2_URID urid;
};

struct _vm_filter_t {
	vm_filter_enum_t type;
	uint8_t channel;
	uint8_t value;
};

struct _plugstate_t {
	uint8_t graph [GRAPH_SIZE];
	uint8_t sourceFilter [FILTER_SIZE];
	uint8_t destinationFilter [FILTER_SIZE];
};

static const char *command_labels [COMMAND_MAX] = {
	[COMMAND_NOP]      = "",
	[COMMAND_OPCODE]   = "Operation",
	[COMMAND_BOOL]     = "Boolean",
	[COMMAND_INT]      = "Integer",
	[COMMAND_FLOAT]    = "Float",
};

static const vm_api_def_t vm_api_def [OP_MAX] = {
	[OP_NOP]  = {
		.uri    = VM_PREFIX"opNop",
		.label  = "",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 0
	},

	[OP_CTRL] = {
		.uri    = VM_PREFIX"opInput",
		.label  = "input",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_PUSH] = {
		.uri    = VM_PREFIX"opPush",
		.label  = "Push topmost value",
		.mnemo  = "push",
		.key    = '\0',
		.npops  = 1,
		.npushs = 2
	},
	[OP_POP] = {
		.uri    = VM_PREFIX"opPop",
		.label  = "Pop topmost value",
		.mnemo  = "pop",
		.key    = '\0',
		.npops  = 1,
		.npushs = 0
	},
	[OP_SWAP]  = {
		.uri    = VM_PREFIX"opSwap",
		.label  = "Swap 2 topmost values",
		.mnemo  = "swap",
		.key    = '\0',
		.npops  = 2,
		.npushs = 2
	},
	[OP_STORE]  = {
		.uri    = VM_PREFIX"opStore",
		.label  = "Store in register",
		.mnemo  = "store",
		.key    = '[',
		.npops  = 2,
		.npushs = 0
	},
	[OP_LOAD]  = {
		.uri    = VM_PREFIX"opLoad",
		.label  = "Load from register",
		.mnemo  = "load",
		.key    = ']',
		.npops  = 1,
		.npushs = 1
	},
	[OP_BREAK]  = {
		.uri    = VM_PREFIX"opBreak",
		.label  = "Break program execution if top of stack is true",
		.mnemo  = "break",
		.key    = '\0',
		.npops  = 1,
		.npushs = 0
	},
	[OP_GOTO]  = {
		.uri    = VM_PREFIX"opGoto",
		.label  = "Goto given operation",
		.mnemo  = "goto",
		.key    = '\0',
		.npops  = 2,
		.npushs = 0
	},

	[OP_RAND]  = {
		.uri    = VM_PREFIX"opRand",
		.label  = "Generate random number",
		.mnemo  = "rand",
		.key    = 'r',
		.npops  = 0,
		.npushs = 1
	},

	[OP_ADD]  = {
		.uri    = VM_PREFIX"opAdd",
		.label  = "Add",
		.mnemo  = "+",
		.key    = '+',
		.npops  = 2,
		.npushs = 1
	},
	[OP_SUB]  = {
		.uri    = VM_PREFIX"opSub",
		.label  = "Subtract",
		.mnemo  = "-",
		.key    = '-',
		.npops  = 2,
		.npushs = 1
	},
	[OP_MUL]  = {
		.uri    = VM_PREFIX"opMul",
		.label  = "Multiply",
		.mnemo  = "*",
		.key    = '*',
		.npops  = 2,
		.npushs = 1
	},
	[OP_DIV]  = {
		.uri    = VM_PREFIX"opDiv",
		.label  = "Divide",
		.mnemo  = "/",
		.key    = '/',
		.npops  = 2,
		.npushs = 1
	},
	[OP_MOD]  = {
		.uri    = VM_PREFIX"opMod",
		.label  = "Modulo",
		.mnemo  = "%",
		.key    = '%',
		.npops  = 2,
		.npushs = 1
	},
	[OP_POW]  = {
		.uri    = VM_PREFIX"opPow",
		.label  = "Power",
		.mnemo  = "^",
		.key    = '^',
		.npops  = 2,
		.npushs = 1
	},

	[OP_NEG]  = {
		.uri    = VM_PREFIX"opNeg",
		.label  = "Negate",
		.mnemo  = "neg",
		.key    = 'n',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ABS]  = {
		.uri    = VM_PREFIX"opAbs",
		.label  = "Absolute",
		.mnemo  = "abs",
		.key    = 'a',
		.npops  = 1,
		.npushs = 1
	},
	[OP_SQRT]  = {
		.uri    = VM_PREFIX"opSqrt",
		.label  = "Square root",
		.mnemo  = "sqrt",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_CBRT]  = {
		.uri    = VM_PREFIX"opCbrt",
		.label  = "Cubic root",
		.mnemo  = "cbrt",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},

	[OP_FLOOR]  = {
		.uri    = VM_PREFIX"opFloor",
		.label  = "Floor",
		.mnemo  = "floor",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_CEIL]  = {
		.uri    = VM_PREFIX"opCeil",
		.label  = "Ceiling",
		.mnemo  = "ceil",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ROUND]  = {
		.uri    = VM_PREFIX"opRound",
		.label  = "Round",
		.mnemo  = "round",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_RINT]  = {
		.uri    = VM_PREFIX"opRint",
		.label  = "Rint",
		.mnemo  = "rint",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_TRUNC]  = {
		.uri    = VM_PREFIX"opTrunc",
		.label  = "Truncate",
		.mnemo  = "trunc",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_MODF]  = {
		.uri    = VM_PREFIX"opModF",
		.label  = "Break number into integer and fractional parts",
		.mnemo  = "modf",
		.key    = '\0',
		.npops  = 1,
		.npushs = 2
	},

	[OP_EXP]  = {
		.uri    = VM_PREFIX"opExp",
		.label  = "Exponential",
		.mnemo  = "exp",
		.key    = 'e',
		.npops  = 1,
		.npushs = 1
	},
	[OP_EXP_2]  = {
		.uri    = VM_PREFIX"opExp2",
		.label  = "Exponential base 2",
		.mnemo  = "exp2",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_LD_EXP]  = {
		.uri    = VM_PREFIX"opLDExp",
		.label  = "Multiply number by 2 raised to a power",
		.mnemo  = "ldexp",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_FR_EXP]  = {
		.uri    = VM_PREFIX"opFRExp",
		.label  = "Break number into significand and power of 2",
		.mnemo  = "frexp",
		.key    = '\0',
		.npops  = 1,
		.npushs = 2
	},
	[OP_LOG]  = {
		.uri    = VM_PREFIX"opLog",
		.label  = "Logarithm",
		.mnemo  = "log",
		.key    = 'l',
		.npops  = 1,
		.npushs = 1
	},
	[OP_LOG_2]  = {
		.uri    = VM_PREFIX"opLog2",
		.label  = "Logarithm base 2",
		.mnemo  = "log2",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_LOG_10]  = {
		.uri    = VM_PREFIX"opLog10",
		.label  = "Logarithm base 10",
		.mnemo  = "log10",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},

	[OP_PI]  = {
		.uri    = VM_PREFIX"opPi",
		.label  = "Pi",
		.mnemo  = "pi",
		.key    = 'p',
		.npops  = 0,
		.npushs = 1
	},
	[OP_SIN]  = {
		.uri    = VM_PREFIX"opSin",
		.label  = "Sinus",
		.mnemo  = "sin",
		.key    = 's',
		.npops  = 1,
		.npushs = 1
	},
	[OP_COS]  = {
		.uri    = VM_PREFIX"opCos",
		.label  = "Cosinus",
		.mnemo  = "cos",
		.key    = 'c',
		.npops  = 1,
		.npushs = 1
	},
	[OP_TAN]  = {
		.uri    = VM_PREFIX"opTan",
		.label  = "Tangens",
		.mnemo  = "tan",
		.key    = 't',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ASIN]  = {
		.uri    = VM_PREFIX"opASin",
		.label  = "Arcus Sinus",
		.mnemo  = "asin",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ACOS]  = {
		.uri    = VM_PREFIX"opACos",
		.label  = "Arcus Cosinus",
		.mnemo  = "acos",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ATAN]  = {
		.uri    = VM_PREFIX"opATan",
		.label  = "Arcus Tangens",
		.mnemo  = "atan",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ATAN2]  = {
		.uri    = VM_PREFIX"opATan2",
		.label  = "Arcus Tangens using quadrants",
		.mnemo  = "atan2",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_SINH]  = {
		.uri    = VM_PREFIX"opSinH",
		.label  = "Sinus Hyperbolicus",
		.mnemo  = "sinh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_COSH]  = {
		.uri    = VM_PREFIX"opCosH",
		.label  = "Cosinus Hyperbolicus",
		.mnemo  = "cosh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_TANH]  = {
		.uri    = VM_PREFIX"opTanH",
		.label  = "Tangens Hyperbolicus",
		.mnemo  = "tanh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ASINH]  = {
		.uri    = VM_PREFIX"opASinH",
		.label  = "Arcus Sinus Hyperbolicus",
		.mnemo  = "asinh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ACOSH]  = {
		.uri    = VM_PREFIX"opACosH",
		.label  = "Arcus Cosinus Hyperbolicus",
		.mnemo  = "acosh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},
	[OP_ATANH]  = {
		.uri    = VM_PREFIX"opATanH",
		.label  = "Arcus Tangens Hyperbolicus",
		.mnemo  = "atanh",
		.key    = '\0',
		.npops  = 1,
		.npushs = 1
	},

	[OP_EQ]  = {
		.uri    = VM_PREFIX"opEq",
		.label  = "Equal",
		.mnemo  = "==",
		.key    = '=',
		.npops  = 2,
		.npushs = 1
	},
	[OP_LT]  = {
		.uri    = VM_PREFIX"opLt",
		.label  = "Less than",
		.mnemo  = "<",
		.key    = '<',
		.npops  = 2,
		.npushs = 1
	},
	[OP_GT]  = {
		.uri    = VM_PREFIX"opGt",
		.label  = "Greater than",
		.mnemo  = ">",
		.key    = '>',
		.npops  = 2,
		.npushs = 1
	},
	[OP_LE]  = {
		.uri    = VM_PREFIX"opLe",
		.label  = "Less or equal",
		.mnemo  = "<=",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_GE]  = {
		.uri    = VM_PREFIX"opGe",
		.label  = "Greater or equal",
		.mnemo  = ">=",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_TER]  = {
		.uri    = VM_PREFIX"opTernary",
		.label  = "Ternary operator",
		.mnemo  = "?",
		.key    = '?',
		.npops  = 3,
		.npushs = 1
	},
	[OP_MINI]  = {
		.uri    = VM_PREFIX"opMin",
		.label  = "Minimum",
		.mnemo  = "min",
		.key    = '{',
		.npops  = 2,
		.npushs = 1
	},
	[OP_MAXI]  = {
		.uri    = VM_PREFIX"opMax",
		.label  = "Maximum",
		.mnemo  = "max",
		.key    = '}',
		.npops  = 2,
		.npushs = 1
	},

	[OP_AND]  = {
		.uri    = VM_PREFIX"opAnd",
		.label  = "And",
		.mnemo  = "&&",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_OR]  = {
		.uri    = VM_PREFIX"opOr",
		.label  = "Or",
		.mnemo  = "||",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_NOT]  = {
		.uri    = VM_PREFIX"opNot",
		.label  = "Not",
		.mnemo  = "!",
		.key    = '!',
		.npops  = 1,
		.npushs = 1
	},

	[OP_BAND]  = {
		.uri    = VM_PREFIX"opBAnd",
		.label  = "Bitwise and",
		.mnemo  = "&",
		.key    = '&',
		.npops  = 2,
		.npushs = 1
	},
	[OP_BOR]  = {
		.uri    = VM_PREFIX"opBOr",
		.label  = "Bitwise or",
		.mnemo  = "|",
		.key    = '|',
		.npops  = 2,
		.npushs = 1
	},
	[OP_BNOT]  = {
		.uri    = VM_PREFIX"opBNot",
		.label  = "Bitwise not",
		.mnemo  = "~",
		.key    = '~',
		.npops  = 1,
		.npushs = 1
	},
	[OP_LSHIFT]  = {
		.uri    = VM_PREFIX"opLShift",
		.label  = "Left shift",
		.mnemo  = "<<",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},
	[OP_RSHIFT]  = {
		.uri    = VM_PREFIX"opRShift",
		.label  = "Right shift",
		.mnemo  = ">>",
		.key    = '\0',
		.npops  = 2,
		.npushs = 1
	},

	[OP_BAR_BEAT]  = {
		.uri    = LV2_TIME__barBeat,
		.label  = "time:barBeat",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_BAR]  = {
		.uri    = LV2_TIME__bar,
		.label  = "time:bar",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_BEAT]  = {
		.uri    = LV2_TIME__beat,
		.label  = "time:beat",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_BEAT_UNIT]  = {
		.uri    = LV2_TIME__beatUnit,
		.label  = "time:beatUnit",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_BPB]  = {
		.uri    = LV2_TIME__beatsPerBar,
		.label  = "time:beatsPerBar",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_BPM]  = {
		.uri    = LV2_TIME__beatsPerMinute,
		.label  = "time:beatsPerMinute",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_FRAME]  = {
		.uri    = LV2_TIME__frame,
		.label  = "time:frame",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_FPS]  = {
		.uri    = LV2_TIME__framesPerSecond,
		.label  = "time:framesPerSecond",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
	[OP_SPEED]  = {
		.uri    = LV2_TIME__speed,
		.label  = "time:speed",
		.mnemo  = NULL,
		.key    = '\0',
		.npops  = 0,
		.npushs = 1
	},
};

static vm_plug_enum_t
vm_plug_type(const char *plugin_uri)
{
	if(!strcmp(plugin_uri, VM_PREFIX"control"))
		return VM_PLUG_CONTROL;
	else if(!strcmp(plugin_uri, VM_PREFIX"cv"))
		return VM_PLUG_CV;
	else if(!strcmp(plugin_uri, VM_PREFIX"audio"))
		return VM_PLUG_AUDIO;
	else if(!strcmp(plugin_uri, VM_PREFIX"atom"))
		return VM_PLUG_ATOM;
	else if(!strcmp(plugin_uri, VM_PREFIX"midi"))
		return VM_PLUG_MIDI;

	return VM_PLUG_CONTROL;
}

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
			case COMMAND_FLOAT:
			{
				if(ref)
					ref = lv2_atom_forge_float(forge, cmd->f32);
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
			cmd->type = COMMAND_INT;
			cmd->i32 = ((const LV2_Atom_Long *)item)->body;
		}
		else if(item->type == forge->Float)
		{
			cmd->type = COMMAND_FLOAT;
			cmd->f32 = ((const LV2_Atom_Float *)item)->body;
		}
		else if(item->type == forge->Double)
		{
			cmd->type = COMMAND_FLOAT;
			cmd->f32 = ((const LV2_Atom_Double *)item)->body;
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

		if(++cmd >= cmds + ITEMS_MAX)
			break;
	}

	return state;
}

#endif // _VM_LV2_H
