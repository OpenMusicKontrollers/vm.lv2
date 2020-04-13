/*
 * Copyright (c) 2017-2020 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
#include <inttypes.h>

#include <timely.lv2/timely.h>

#include <vm.h>

#define SLOT_MAX  0x20
#define SLOT_MASK (SLOT_MAX - 1)

#define REG_MAX   0x20
#define REG_MASK  (REG_MAX - 1)

typedef union _vm_port_t vm_port_t;
typedef union _vm_const_port_t vm_const_port_t;
typedef struct _vm_stack_t vm_stack_t;
typedef struct _plughandle_t plughandle_t;
typedef struct _forge_t forge_t;

typedef double num_t;

union _vm_port_t {
	float *flt;
	LV2_Atom_Sequence *seq;
};

union _vm_const_port_t {
	const float *flt;
	const LV2_Atom_Sequence *seq;
};

struct _vm_stack_t {
	num_t slots [SLOT_MAX];
	num_t regs [REG_MAX];
	int ptr;
};

struct _forge_t {
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	vm_plug_enum_t vm_plug;

	LV2_URID vm_graph;
	LV2_URID midi_MidiEvent;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;
	vm_const_port_t in [CTRL_MAX];
	vm_port_t out [CTRL_MAX];

	float in0 [CTRL_MAX];
	num_t out0 [CTRL_MAX];
	float inm [CTRL_MAX];
	float outm [CTRL_MAX];
	bool inf [CTRL_MAX];
	bool outf [CTRL_MAX];
	forge_t forgs [CTRL_MAX];

	PROPS_T(props, MAX_NPROPS);
	plugstate_t state;
	plugstate_t stash;

	uint32_t graph_size;
	uint32_t sourceFilter_size;
	uint32_t destinationFilter_size;
	vm_api_impl_t api [OP_MAX];
	vm_filter_t sourceFilter [CTRL_MAX];
	vm_filter_t destinationFilter [CTRL_MAX];
	vm_filter_impl_t filt;

	vm_stack_t stack;
	bool needs_recalc;
	vm_status_t status;

	int64_t off;

	vm_command_t cmds [ITEMS_MAX];

	timely_t timely;
};

static inline void
_stack_clear(vm_stack_t *stack)
{
	for(unsigned i = 0; i < SLOT_MAX; i++)
		stack->slots[i] = 0;
	stack->ptr = 0;
}

static inline void
_stack_push(vm_stack_t *stack, num_t val)
{
	stack->ptr = (stack->ptr - 1) & SLOT_MASK;

	stack->slots[stack->ptr] = val;
}

static inline num_t
_stack_pop(vm_stack_t *stack)
{
	const num_t val = stack->slots[stack->ptr];

	stack->ptr = (stack->ptr + 1) & SLOT_MASK;

	return val;
}

static inline void
_stack_push_num(vm_stack_t *stack, const num_t *val, int num)
{
	for(int i = 0; i < num; i++)
		stack->slots[(stack->ptr - i - 1) & SLOT_MASK] = val[i];

	stack->ptr = (stack->ptr - num) & SLOT_MASK;
}

static inline void
_stack_pop_num(vm_stack_t *stack, num_t *val, int num)
{
	for(int i = 0; i < num; i++)
		val[i] = stack->slots[(stack->ptr + i) & SLOT_MASK];

	stack->ptr = (stack->ptr + num) & SLOT_MASK;
}

static inline num_t
_stack_peek(vm_stack_t *stack)
{
	return stack->slots[stack->ptr];
}

static inline void
_dirty(plughandle_t *handle)
{
	// sync input and output to UI
	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		handle->inf[i] = true;
		handle->outf[i] = true;
	}
}

static void
_intercept_graph(void *data, int64_t frames __attribute__((unused)),
	props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->graph_size = impl->value.size;

	handle->status = vm_graph_deserialize(handle->api, &handle->forge, handle->cmds,
		impl->value.size, impl->value.body);

	handle->needs_recalc = true;
	_dirty(handle);
}

static void
_intercept_sourceFilter(void *data, int64_t frames __attribute__((unused)),
	props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->sourceFilter_size = impl->value.size;

	const int status = vm_filter_deserialize(&handle->forge, &handle->filt,
		handle->sourceFilter, impl->value.size, impl->value.body);
	(void)status; //FIXME

	handle->needs_recalc = true;
	_dirty(handle);
}

static void
_intercept_destinationFilter(void *data, int64_t frames __attribute__((unused)),
	props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->destinationFilter_size = impl->value.size;

	const int status = vm_filter_deserialize(&handle->forge, &handle->filt,
		handle->destinationFilter, impl->value.size, impl->value.body);
	(void)status; //FIXME

	handle->needs_recalc = true;
	_dirty(handle);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = VM__graph,
		.offset = offsetof(plugstate_t, graph),
		.type = LV2_ATOM__Tuple,
		.max_size = GRAPH_SIZE,
		.event_cb = _intercept_graph,
	},
	{
		.property = VM__sourceFilter,
		.offset = offsetof(plugstate_t, sourceFilter),
		.type = LV2_ATOM__Tuple,
		.max_size = FILTER_SIZE,
		.event_cb = _intercept_sourceFilter,
	},
	{
		.property = VM__destinationFilter,
		.offset = offsetof(plugstate_t, destinationFilter),
		.type = LV2_ATOM__Tuple,
		.max_size = FILTER_SIZE,
		.event_cb = _intercept_destinationFilter,
	}
};

static void
_cb(timely_t *timely __attribute__((unused)), int64_t frames __attribute__((unused)),
	LV2_URID type __attribute__((unused)), void *data __attribute__((unused)))
{
	// nothing to do
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, num_t rate,
	const char *bundle_path __attribute__((unused)),
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->vm_plug = vm_plug_type(descriptor->URI);

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);

	handle->vm_graph = handle->map->map(handle->map->handle, VM__graph);
	handle->midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);

	handle->filt.midi_Controller = handle->map->map(handle->map->handle, LV2_MIDI__Controller);
	handle->filt.midi_Bender = handle->map->map(handle->map->handle, LV2_MIDI__Bender);
	handle->filt.midi_ProgramChange = handle->map->map(handle->map->handle, LV2_MIDI__ProgramChange);
	handle->filt.midi_ChannelPressure = handle->map->map(handle->map->handle, LV2_MIDI__ChannelPressure);
	handle->filt.midi_NotePressure = handle->map->map(handle->map->handle, LV2_MIDI__Aftertouch);
	handle->filt.midi_NoteOn = handle->map->map(handle->map->handle, LV2_MIDI__NoteOn);
	handle->filt.midi_channel = handle->map->map(handle->map->handle, LV2_MIDI__channel);
	handle->filt.midi_controllerNumber = handle->map->map(handle->map->handle, LV2_MIDI__controllerNumber);
	handle->filt.midi_noteNumber = handle->map->map(handle->map->handle, LV2_MIDI__noteNumber);
	handle->filt.midi_velocity = handle->map->map(handle->map->handle, LV2_MIDI__velocity);

	lv2_atom_forge_init(&handle->forge, handle->map);
	for(unsigned i = 0; i < CTRL_MAX; i++)
		lv2_atom_forge_init(&handle->forgs[i].forge, handle->map);

	vm_api_init(handle->api, handle->map);
	timely_init(&handle->timely, handle->map, rate, 0, _cb, handle);

	const int nprops = handle->vm_plug == VM_PLUG_MIDI
		? MAX_NPROPS
		: 1;

	if(!props_init(&handle->props, descriptor->URI,
		defs, nprops,
		&handle->state, &handle->stash, handle->map, handle))
	{
		fprintf(stderr, "props_init failed\n");
		free(handle);
		return NULL;
	}

	handle->needs_recalc = true;

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->control = data;
			break;
		case 1:
			handle->notify = data;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			handle->in[port - 2].flt = data;
			break;
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
			handle->out[port - 10].flt = data;
			break;
	}
}

#define CLIP(a, v, b) fmin(fmax(a, v), b)

static void
run_pre(plughandle_t *handle)
{
	// reset maximum values and notification flags
	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		handle->inm[i] = 0.f;
		handle->outm[i] = 0.f;
	}
}

static void
run_post(plughandle_t *handle, uint32_t frames)
{
	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		if(handle->inf[i]) // port needs notification
		{
			LV2_Atom_Forge_Frame tup_frame;
			if(handle->ref)
				handle->ref = lv2_atom_forge_frame_time(&handle->forge, frames);
			if(handle->ref)
				handle->ref = lv2_atom_forge_tuple(&handle->forge, &tup_frame);
			if(handle->ref)
				handle->ref = lv2_atom_forge_int(&handle->forge, i + 2);
			if(handle->ref)
				handle->ref = lv2_atom_forge_float(&handle->forge, handle->inm[i]);
			if(handle->ref)
				lv2_atom_forge_pop(&handle->forge, &tup_frame);

			handle->inf[i] = false;
		}

		if(handle->outf[i]) // port needs notification
		{
			LV2_Atom_Forge_Frame tup_frame;
			if(handle->ref)
				handle->ref = lv2_atom_forge_frame_time(&handle->forge, frames);
			if(handle->ref)
				handle->ref = lv2_atom_forge_tuple(&handle->forge, &tup_frame);
			if(handle->ref)
				handle->ref = lv2_atom_forge_int(&handle->forge, i + 10);
			if(handle->ref)
				handle->ref = lv2_atom_forge_float(&handle->forge, handle->outm[i]);
			if(handle->ref)
				lv2_atom_forge_pop(&handle->forge, &tup_frame);

			handle->outf[i] = false;
		}
	}
}

static LV2_Atom_Forge_Ref
send_chunk(LV2_Atom_Forge *forge, uint32_t frames, LV2_URID type,
	const uint8_t *msg, uint32_t sz)
{
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_atom(forge, sz, type);
	if(ref)
		ref = lv2_atom_forge_write(forge, msg, sz);

	return ref;
}

static void
run_internal(plughandle_t *handle, uint32_t frames,
	const float *in [CTRL_MAX], float *out [CTRL_MAX], forge_t forgs [CTRL_MAX])
{
	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		const float in1 = (handle->vm_plug == VM_PLUG_AUDIO)
			? *in[i] // don't clip audio
			: CLIP(VM_MIN, *in[i], VM_MAX);

		if(handle->in0[i] != in1)
		{
			handle->needs_recalc = true;
			handle->in0[i] = in1;

			if(in1 != handle->inm[i])
			{
				handle->inm[i] = in1;
				handle->inf[i] = true; // notify in run_post
			}
		}
	}

	if(handle->status != VM_STATUS_STATIC)
		handle->needs_recalc = true;

	if(handle->needs_recalc)
	{
		_stack_clear(&handle->stack);

		for(unsigned i = 0; i < ITEMS_MAX; i++)
loop: {
			vm_command_t *cmd = &handle->cmds[i];
			bool terminate = false;

			switch(cmd->type)
			{
				case COMMAND_BOOL:
				{
					const num_t c = cmd->i32;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_INT:
				{
					const num_t c = cmd->i32;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_FLOAT:
				{
					const num_t c = cmd->f32;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_OPCODE:
				{
					switch(cmd->op)
					{
						case OP_CTRL:
						{
							const int idx = floor(_stack_pop(&handle->stack));
							const num_t c = handle->in0[idx & CTRL_MASK];
							_stack_push(&handle->stack, c);
						} break;
						case OP_PUSH:
						{
							const num_t c = _stack_peek(&handle->stack);
							_stack_push(&handle->stack, c);
						} break;
						case OP_POP:
						{
							const num_t c = _stack_pop(&handle->stack);
							(void)c;
						} break;
						case OP_SWAP:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							_stack_push_num(&handle->stack, ab, 2);
						} break;
						case OP_STORE:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const int idx = floorf(ab[0]);
							handle->stack.regs[idx & REG_MASK] = ab[1];
						} break;
						case OP_LOAD:
						{
							const num_t a = _stack_pop(&handle->stack);
							const int idx = floorf(a);
							const num_t c = handle->stack.regs[idx & REG_MASK];
							_stack_push(&handle->stack, c);
						} break;
						case OP_BREAK:
						{
							const bool a = _stack_pop(&handle->stack);
							if(a)
								terminate = true;
						} break;
						case OP_GOTO:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							if(ab[0])
							{
								const int idx = ab[1];
								i = idx & ITEMS_MASK;
								goto loop;
							}
						} break;

						case OP_RAND:
						{
							const num_t c = (num_t)rand() / RAND_MAX;
							_stack_push(&handle->stack, c);
						} break;

						case OP_ADD:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ab[1] + ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_SUB:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ab[1] - ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_MUL:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ab[1] * ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_DIV:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ab[0] == 0.0
								? 0.0
								: ab[1] / ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_MOD:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ab[0] == 0.0
								? 0.0
								: fmod(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;
						case OP_POW:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = pow(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;

						case OP_NEG:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = -a;
							_stack_push(&handle->stack, c);
						} break;
						case OP_ABS:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = fabs(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_SQRT:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = sqrt(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_CBRT:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = cbrt(a);
							_stack_push(&handle->stack, c);
						} break;

						case OP_FLOOR:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = floor(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_CEIL:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = ceil(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ROUND:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = round(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_RINT:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = rint(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_TRUNC:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = trunc(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_MODF:
						{
							const num_t a = _stack_pop(&handle->stack);
							num_t d;
							const num_t c = modf(a, &d);
							_stack_push(&handle->stack, c);
							_stack_push(&handle->stack, d);
						} break;

						case OP_EXP:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = exp(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_EXP_2:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = exp2(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_LD_EXP:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = ldexp(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;
						case OP_FR_EXP:
						{
							const num_t a = _stack_pop(&handle->stack);
							int d;
							const num_t c = frexp(a, &d);
							_stack_push(&handle->stack, c);
							_stack_push(&handle->stack, d);
						} break;
						case OP_LOG:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = log(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_LOG_2:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = log2(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_LOG_10:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = log10(a);
							_stack_push(&handle->stack, c);
						} break;

						case OP_PI:
						{
							num_t c = M_PI;
							_stack_push(&handle->stack, c);
						} break;
						case OP_SIN:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = sin(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_COS:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = cos(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_TAN:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = tan(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ASIN:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = asin(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ACOS:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = acos(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ATAN:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = atan(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ATAN2:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = atan2(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;
						case OP_SINH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = sinh(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_COSH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = cosh(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_TANH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = tanh(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ASINH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = asinh(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ACOSH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = acosh(a);
							_stack_push(&handle->stack, c);
						} break;
						case OP_ATANH:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = atanh(a);
							_stack_push(&handle->stack, c);
						} break;

						case OP_EQ:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] == ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_LT:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] < ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_GT:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] > ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_LE:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] <= ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_GE:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] >= ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_TER:
						{
							num_t ab [3];
							_stack_pop_num(&handle->stack, ab, 3);
							const bool c = ab[0];
							_stack_push(&handle->stack, c ? ab[2] : ab[1]);
						} break;
						case OP_MINI:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = fmin(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;
						case OP_MAXI:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = fmax(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;

						case OP_AND:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] && ab[0];
							_stack_push(&handle->stack, c);
						} break;
						case OP_OR:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const bool c = ab[1] || ab[0];
							_stack_push(&handle->stack, c);
						} break;

						case OP_NOT:
						{
							const int a = _stack_pop(&handle->stack);
							const bool c = !a;
							_stack_push(&handle->stack, c);
						} break;
						case OP_BAND:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const unsigned a = ab[1];
							const unsigned b = ab[0];
							const unsigned c = a & b;
							_stack_push(&handle->stack, c);
						} break;
						case OP_BOR:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const unsigned a = ab[1];
							const unsigned b = ab[0];
							const unsigned c = a | b;
							_stack_push(&handle->stack, c);
						} break;
						case OP_BNOT:
						{
							const unsigned a = _stack_pop(&handle->stack);
							const unsigned c = ~a;
							_stack_push(&handle->stack, c);
						} break;
						case OP_LSHIFT:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const unsigned a = ab[1];
							const unsigned b = ab[0];
							const unsigned c = a <<  b;
							_stack_push(&handle->stack, c);
						} break;
						case OP_RSHIFT:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const unsigned a = ab[1];
							const unsigned b = ab[0];
							const unsigned c = a >>  b;
							_stack_push(&handle->stack, c);
						} break;

						// time
						case OP_BAR_BEAT:
						{
							const num_t c = TIMELY_BAR_BEAT(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_BAR:
						{
							const num_t c = TIMELY_BAR(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_BEAT:
						{
							const num_t bar = TIMELY_BAR(&handle->timely);
							const num_t beats_per_bar = TIMELY_BEATS_PER_BAR(&handle->timely);
							const num_t bar_beat = TIMELY_BAR_BEAT(&handle->timely);
							const num_t c = bar*beats_per_bar + bar_beat;
							_stack_push(&handle->stack, c);
						} break;
						case OP_BEAT_UNIT:
						{
							const num_t c = TIMELY_BEAT_UNIT(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_BPB:
						{
							const num_t c = TIMELY_BEATS_PER_BAR(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_BPM:
						{
							const num_t c = TIMELY_BEATS_PER_MINUTE(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_FRAME:
						{
							const num_t c = TIMELY_FRAME(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_FPS:
						{
							const num_t c = TIMELY_FRAMES_PER_SECOND(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;
						case OP_SPEED:
						{
							const num_t c = TIMELY_SPEED(&handle->timely);
							_stack_push(&handle->stack, c);
						} break;

						case OP_NOP:
						{
							// no operation
						} break;
						case OP_MAX:
							break;
					}
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

		_stack_pop_num(&handle->stack, handle->out0, CTRL_MAX);
		handle->needs_recalc = false;
	}

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		const float out1 = (handle->vm_plug == VM_PLUG_AUDIO)
			? handle->out0[i] // don't clip audio
			: CLIP(VM_MIN, handle->out0[i], VM_MAX);

		if(*out[i] != out1)
		{
			if(forgs)
			{
				if(handle->vm_plug == VM_PLUG_ATOM)
				{
					// send changes on atom output ports
					if(forgs[i].ref)
						forgs[i].ref = lv2_atom_forge_frame_time(&forgs[i].forge, frames);
					if(handle->ref)
						forgs[i].ref = lv2_atom_forge_float(&forgs[i].forge, out1);
				}
				else if(handle->vm_plug == VM_PLUG_MIDI)
				{
					const vm_filter_t *filter = &handle->destinationFilter[i];

					switch(filter->type)
					{
						case FILTER_CONTROLLER:
						{
							const uint8_t value = floor(out1 * 0x7f);
							const uint8_t msg [3] = {
								[0] = LV2_MIDI_MSG_CONTROLLER | filter->channel,
								[1] = filter->value,
								[2] = value
							};

							if(forgs[i].ref)
								forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
						} break;
						case FILTER_BENDER:
						{
							const int16_t value = floor(out1*0x2000 + 0x1fff);
							const uint8_t msg [3] = {
								[0] = LV2_MIDI_MSG_BENDER | filter->channel,
								[1] = value & 0x7f,
								[2] = value >> 7
							};

							if(forgs[i].ref)
								forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
						} break;
						case FILTER_PROGRAM_CHANGE:
						{
							const uint8_t value = floor(out1 * 0x7f);
							const uint8_t msg [2] = {
								[0] = LV2_MIDI_MSG_PGM_CHANGE | filter->channel,
								[1] = value
							};

							if(forgs[i].ref)
								forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
						} break;
						case FILTER_CHANNEL_PRESSURE:
						{
							const uint8_t value = floor(out1 * 0x7f);
							const uint8_t msg [2] = {
								[0] = LV2_MIDI_MSG_CHANNEL_PRESSURE | filter->channel,
								[1] = value
							};

							if(forgs[i].ref)
								forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
						} break;
						case FILTER_NOTE_ON:
						{
							if(floor(*out[i] * 0x7f) > 0x0)
							{
								const uint8_t value = floor(*out[i] * 0x7f);
								const uint8_t msg [3] = {
									[0] = LV2_MIDI_MSG_NOTE_OFF | filter->channel,
									[1] = value,
									[2] = 0x0
								};

								if(forgs[i].ref)
									forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
							}
							if(floor(out1 * 0x7f) > 0x0)
							{
								const uint8_t value = floor(out1 * 0x7f);
								const uint8_t msg [3] = {
									[0] = LV2_MIDI_MSG_NOTE_ON | filter->channel,
									[1] = value,
									[2] = filter->value
								};

								if(forgs[i].ref)
									forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
							}
						} break;
						case FILTER_NOTE_PRESSURE:
						{
							const uint8_t value = floor(out1 * 0x7f);
							const uint8_t msg [3] = {
								[0] = LV2_MIDI_MSG_NOTE_PRESSURE | filter->channel,
								[1] = filter->value,
								[2] = value
							};

							if(forgs[i].ref)
								forgs[i].ref = send_chunk(&forgs[i].forge, frames, handle->midi_MidiEvent, msg, sizeof(msg));
						} break;
						//FIXME handle more types

						case FILTER_MAX:
						{
							// nothing
						}	break;
					}
				}
			}

			*out[i] = out1;

			if(out1 != handle->outm[i])
			{
				handle->outm[i] = out1;
				handle->outf[i] = true; // notify out run_post
			}
		}
	}
}

static void
run_control(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->notify, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	run_pre(handle);
	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	int64_t last_t = 0;
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
		timely_advance(&handle->timely, obj, last_t, ev->time.frames);

		last_t = ev->time.frames;
	}
	timely_advance(&handle->timely, NULL, last_t, nsamples);

	// run once at end of period for controls
	{
		const float *in [CTRL_MAX ] = {
			handle->in[0].flt,
			handle->in[1].flt,
			handle->in[2].flt,
			handle->in[3].flt,
			handle->in[4].flt,
			handle->in[5].flt,
			handle->in[6].flt,
			handle->in[7].flt
		};

		float *out [CTRL_MAX ] = {
			handle->out[0].flt,
			handle->out[1].flt,
			handle->out[2].flt,
			handle->out[3].flt,
			handle->out[4].flt,
			handle->out[5].flt,
			handle->out[6].flt,
			handle->out[7].flt
		};

		run_internal(handle, nsamples -1, in, out, NULL);
	}

	run_post(handle, nsamples - 1);

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(&handle->forge, nsamples - 1);
	if(handle->ref)
		handle->ref = lv2_atom_forge_long(&handle->forge, handle->off);

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);

	handle->off += nsamples;
}

static void
run_cv_audio_advance(plughandle_t *handle, const LV2_Atom_Object *obj,
	uint32_t from, uint32_t to)
{
	if(from == to) // just run timely_advance for void range
	{
		timely_advance(&handle->timely, obj, from, to);
	}
	else
	{
		for(unsigned i = from; i < to; i++)
		{
			if(timely_advance(&handle->timely, obj, i, i + 1))
				obj = NULL; // invalidate obj for further steps if handled

			// make it inplace-safe
			const float tmp [CTRL_MAX] = {
				handle->in[0].flt[i],
				handle->in[1].flt[i],
				handle->in[2].flt[i],
				handle->in[3].flt[i],
				handle->in[4].flt[i],
				handle->in[5].flt[i],
				handle->in[6].flt[i],
				handle->in[7].flt[i]
			};

			const float *in [CTRL_MAX ] = {
				&tmp[0],
				&tmp[1],
				&tmp[2],
				&tmp[3],
				&tmp[4],
				&tmp[5],
				&tmp[6],
				&tmp[7]
			};

			float *out [CTRL_MAX ] = {
				&handle->out[0].flt[i],
				&handle->out[1].flt[i],
				&handle->out[2].flt[i],
				&handle->out[3].flt[i],
				&handle->out[4].flt[i],
				&handle->out[5].flt[i],
				&handle->out[6].flt[i],
				&handle->out[7].flt[i]
			};

			run_internal(handle, i, in, out, NULL);
		}
	}
}

static void
run_cv_audio(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->notify, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	run_pre(handle);
	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	int64_t last_t = 0;
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
		run_cv_audio_advance(handle, obj, last_t, ev->time.frames);

		last_t = ev->time.frames;
	}
	run_cv_audio_advance(handle, NULL, last_t, nsamples);

	run_post(handle, nsamples - 1);

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(&handle->forge, nsamples - 1);
	if(handle->ref)
		handle->ref = lv2_atom_forge_long(&handle->forge, handle->off);

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);

	handle->off += nsamples;
}

static void
run_atom_advance(plughandle_t *handle, const LV2_Atom_Object *obj,
	uint32_t from, uint32_t to, const float *in [CTRL_MAX], float *out [CTRL_MAX],
	forge_t forgs [CTRL_MAX])
{
	if(from == to) // just run timely_advance for void range
	{
		timely_advance(&handle->timely, obj, from, to);
	}
	else
	{
		for(unsigned i = from; i < to; i++)
		{
			if(timely_advance(&handle->timely, obj, i, i + 1))
				obj = NULL; // invalidate obj for further steps if handled

			run_internal(handle, i, in, out, forgs);
		}
	}
}

static void
run_atom(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->notify, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	run_pre(handle);
	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	forge_t *forgs = handle->forgs;
	float pin [CTRL_MAX];
	float pout [CTRL_MAX];

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		lv2_atom_forge_set_buffer(&forgs[i].forge, (uint8_t *)handle->out[i].seq, handle->out[i].seq->atom.size);
		forgs[i].ref = lv2_atom_forge_sequence_head(&forgs[i].forge, &forgs[i].frame, 0);

		pin[i] = handle->in0[i];
		pout[i] = handle->out0[i];
	}

	const float *in [CTRL_MAX ] = {
		&pin[0],
		&pin[1],
		&pin[2],
		&pin[3],
		&pin[4],
		&pin[5],
		&pin[6],
		&pin[7]
	};

	float *out [CTRL_MAX ] = {
		&pout[0],
		&pout[1],
		&pout[2],
		&pout[3],
		&pout[4],
		&pout[5],
		&pout[6],
		&pout[7]
	};

	const unsigned nseqs = CTRL_MAX + 1;
	const LV2_Atom_Sequence *seqs [nseqs];
	const LV2_Atom_Event *evs [nseqs];

	for(unsigned i = 0; i < nseqs; i++)
	{
		seqs[i] = (i == 0)
			? handle->control
			: handle->in[i-1].seq;

		evs[i] = lv2_atom_sequence_begin(&seqs[i]->body);
	}

	int64_t last_t = 0;
	while(true)
	{
		int nxt = -1;
		int64_t frames = nsamples;

		// search next event
		for(unsigned i = 0; i < nseqs; i++)
		{
			if(!evs[i] || lv2_atom_sequence_is_end(&seqs[i]->body, seqs[i]->atom.size, evs[i]))
			{
				evs[i] = NULL; // invalidate, sequence has been drained
				continue;
			}

			if(evs[i]->time.frames < frames)
			{
				frames = evs[i]->time.frames;
				nxt = i;
			}
		}

		if(nxt == -1)
			break; // no events anymore, exit loop

		// handle event
		{
			const bool is_control = (nxt == 0); // is event from control port?
			const LV2_Atom_Event *ev = evs[nxt];
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
			const LV2_Atom_Float *f32 = (const LV2_Atom_Float *)&ev->body;

			if(is_control)
			{
				props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
			}
			else if(f32->atom.type == handle->forge.Float)
			{
				pin[nxt-1] = f32->body;
			}

			run_atom_advance(handle, is_control ? obj : NULL, last_t, ev->time.frames, in, out, forgs);

			last_t = ev->time.frames;
		}

		// advance event iterator on active sequence
		evs[nxt] = lv2_atom_sequence_next(evs[nxt]);
	}
	run_atom_advance(handle, NULL, last_t, nsamples, in, out, forgs);

	run_post(handle, nsamples - 1);

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(&handle->forge, nsamples - 1);
	if(handle->ref)
		handle->ref = lv2_atom_forge_long(&handle->forge, handle->off);

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		if(forgs[i].ref)
			lv2_atom_forge_pop(&forgs[i].forge, &forgs[i].frame);
		else
			lv2_atom_sequence_clear(handle->out[i].seq);
	}

	handle->off += nsamples;
}

static void
run_midi_advance(plughandle_t *handle, const LV2_Atom_Object *obj,
	uint32_t from, uint32_t to, const float *in [CTRL_MAX], float *out [CTRL_MAX],
	forge_t forgs [CTRL_MAX])
{
	if(from == to) // just run timely_advance for void range
	{
		timely_advance(&handle->timely, obj, from, to);
	}
	else
	{
		for(unsigned i = from; i < to; i++)
		{
			if(timely_advance(&handle->timely, obj, i, i + 1))
				obj = NULL; // invalidate obj for further steps if handled

			run_internal(handle, i, in, out, forgs);
		}
	}
}

static bool
filter_midi(vm_filter_t *filter, const uint8_t *msg, float *f32)
{
	switch(filter->type)
	{
		case FILTER_CONTROLLER:
		{
			if(  (msg[0] == (LV2_MIDI_MSG_CONTROLLER | filter->channel) )
				&& (msg[1] == filter->value) )
			{
				const uint8_t value = msg[2];
				*f32 = (float)value / 0x7f;

				return true;
			}
		} break;
		case FILTER_BENDER:
		{
			if(msg[0] == (LV2_MIDI_MSG_BENDER | filter->channel) )
			{
				const int64_t value = msg[2] | (msg[1] << 7);
				*f32 = (float)(value - 0x1fff) / 0x2000;

				return true;
			}
		} break;
		case FILTER_PROGRAM_CHANGE:
		{
			if(msg[0] == (LV2_MIDI_MSG_PGM_CHANGE | filter->channel) )
			{
				const uint8_t value = msg[1];
				*f32 = (float)value / 0x7f;

				return true;
			}
		} break;
		case FILTER_CHANNEL_PRESSURE:
		{
			if(msg[0] == (LV2_MIDI_MSG_CHANNEL_PRESSURE | filter->channel) )
			{
				const uint8_t value = msg[1];
				*f32 = (float)value / 0x7f;

				return true;
			}
		} break;
		case FILTER_NOTE_ON:
		{
			if(msg[0] == (LV2_MIDI_MSG_NOTE_ON | filter->channel) )
			{
				const uint8_t value = msg[1];
				*f32 = (float)value / 0x7f;

				return true;
			}
			else if(msg[0] == (LV2_MIDI_MSG_NOTE_OFF | filter->channel) )
			{
				*f32 = 0.f;

				return true;
			}
		} break;
		case FILTER_NOTE_PRESSURE:
		{
			if(  (msg[0] == (LV2_MIDI_MSG_NOTE_PRESSURE | filter->channel) )
				&& (msg[1] == filter->value) )
			{
				const uint8_t value = msg[2];
				*f32 = (float)value / 0x7f;

				return true;
			}
		} break;
		//FIXME handle more filter types

		case FILTER_MAX:
		{
			// nothing
		}	break;
	}

	return false;
}

static void
run_midi(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->notify, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	run_pre(handle);
	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	forge_t *forgs = handle->forgs;
	float pin [CTRL_MAX];
	float pout [CTRL_MAX];

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		lv2_atom_forge_set_buffer(&forgs[i].forge, (uint8_t *)handle->out[i].seq, handle->out[i].seq->atom.size);
		forgs[i].ref = lv2_atom_forge_sequence_head(&forgs[i].forge, &forgs[i].frame, 0);

		pin[i] = handle->in0[i];
		pout[i] = handle->out0[i];
	}

	const float *in [CTRL_MAX ] = {
		&pin[0],
		&pin[1],
		&pin[2],
		&pin[3],
		&pin[4],
		&pin[5],
		&pin[6],
		&pin[7]
	};

	float *out [CTRL_MAX ] = {
		&pout[0],
		&pout[1],
		&pout[2],
		&pout[3],
		&pout[4],
		&pout[5],
		&pout[6],
		&pout[7]
	};

	const unsigned nseqs = CTRL_MAX + 1;
	const LV2_Atom_Sequence *seqs [nseqs];
	const LV2_Atom_Event *evs [nseqs];

	for(unsigned i = 0; i < nseqs; i++)
	{
		seqs[i] = (i == 0)
			? handle->control
			: handle->in[i-1].seq;

		evs[i] = lv2_atom_sequence_begin(&seqs[i]->body);
	}

	int64_t last_t = 0;
	while(true)
	{
		int nxt = -1;
		int64_t frames = nsamples;

		// search next event
		for(unsigned i = 0; i < nseqs; i++)
		{
			if(!evs[i] || lv2_atom_sequence_is_end(&seqs[i]->body, seqs[i]->atom.size, evs[i]))
			{
				evs[i] = NULL; // invalidate, sequence has been drained
				continue;
			}

			if(evs[i]->time.frames < frames)
			{
				frames = evs[i]->time.frames;
				nxt = i;
			}
		}

		if(nxt == -1)
			break; // no events anymore, exit loop

		// handle event
		{
			const bool is_control = (nxt == 0); // is event from control port?
			const LV2_Atom_Event *ev = evs[nxt];
			const LV2_Atom *atom= &ev->body;
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
			const uint8_t *msg = LV2_ATOM_BODY_CONST(atom);
			float f32 = 0;

			if(is_control)
			{
				props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
			}
			else if( (atom->type == handle->midi_MidiEvent)
				&& filter_midi(&handle->sourceFilter[nxt-1], msg, &f32) )
			{
				pin[nxt-1] = f32;
			}


			run_midi_advance(handle, is_control ? obj : NULL, last_t, ev->time.frames, in, out, forgs);

			last_t = ev->time.frames;
		}

		// advance event iterator on active sequence
		evs[nxt] = lv2_atom_sequence_next(evs[nxt]);
	}
	run_midi_advance(handle, NULL, last_t, nsamples, in, out, forgs);

	run_post(handle, nsamples - 1);

	if(handle->ref)
		handle->ref = lv2_atom_forge_frame_time(&handle->forge, nsamples - 1);
	if(handle->ref)
		handle->ref = lv2_atom_forge_long(&handle->forge, handle->off);

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		if(forgs[i].ref)
			lv2_atom_forge_pop(&forgs[i].forge, &forgs[i].frame);
		else
			lv2_atom_sequence_clear(handle->out[i].seq);
	}

	handle->off += nsamples;
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

static const LV2_Descriptor vm_control = {
	.URI            = VM_PREFIX"control",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run_control,
	.deactivate     = NULL,
	.cleanup        = cleanup,
	.extension_data = extension_data
};

static const LV2_Descriptor vm_cv = {
	.URI            = VM_PREFIX"cv",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run_cv_audio,
	.deactivate     = NULL,
	.cleanup        = cleanup,
	.extension_data = extension_data
};

static const LV2_Descriptor vm_audio = {
	.URI            = VM_PREFIX"audio",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run_cv_audio,
	.deactivate     = NULL,
	.cleanup        = cleanup,
	.extension_data = extension_data
};

static const LV2_Descriptor vm_atom = {
	.URI            = VM_PREFIX"atom",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run_atom,
	.deactivate     = NULL,
	.cleanup        = cleanup,
	.extension_data = extension_data
};

static const LV2_Descriptor vm_midi = {
	.URI            = VM_PREFIX"midi",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run_midi,
	.deactivate     = NULL,
	.cleanup        = cleanup,
	.extension_data = extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &vm_control;
		case 1:
			return &vm_cv;
		case 2:
			return &vm_audio;
		case 3:
			return &vm_atom;
		case 4:
			return &vm_midi;

		default:
			return NULL;
	}
}
