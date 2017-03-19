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

#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
#include <inttypes.h>

#include <vm.h>

#define SLOT_MAX 32
#define REG_MAX 32

typedef struct _stack_t stack_t;
typedef struct _plughandle_t plughandle_t;

typedef double num_t;

struct _stack_t {
	num_t slots [SLOT_MAX];
	num_t regs [REG_MAX];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID vm_graph;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence *notify;
	const float *in [CTRL_MAX];
	float *out [CTRL_MAX];

	num_t in0 [CTRL_MAX];
	num_t out0 [CTRL_MAX];

	PROPS_T(props, MAX_NPROPS);
	plugstate_t state;
	plugstate_t stash;

	uint32_t graph_size;
	opcode_t opcode;

	stack_t stack;
	bool recalc;
	bool sync;
	bool is_dynamic;

	command_t cmds [ITEMS_MAX];

	num_t srate;
	int64_t frame;
};

static inline void
_stack_clear(stack_t *stack)
{
	for(unsigned i = 0; i < SLOT_MAX; i++)
		stack->slots[i] = 0.f;
}

static inline void
_stack_push(stack_t *stack, num_t val)
{
	for(unsigned i = SLOT_MAX - 1; i >= 1; i--)
		stack->slots[i] = stack->slots[i - 1];

	stack->slots[0] = val;
}

static inline num_t
_stack_pop(stack_t *stack)
{
	const num_t val = stack->slots[0];

	for(unsigned i = 0; i < SLOT_MAX - 1; i++)
		stack->slots[i] = stack->slots[i + 1];

	stack->slots[SLOT_MAX - 1] = 0.f;

	return val;
}

static inline void
_stack_push_num(stack_t *stack, const num_t *val, unsigned num)
{
	for(unsigned i = SLOT_MAX - 1; i >= num; i--)
		stack->slots[i] = stack->slots[i - num];

	for(unsigned i = 0; i < num; i++)
		stack->slots[i] = val[i];
}

static inline void
_stack_pop_num(stack_t *stack, num_t *val, unsigned num)
{
	for(unsigned i = 0; i < num; i++)
		val[i] = stack->slots[i];

	for(unsigned i = 0; i < SLOT_MAX - num; i++)
		stack->slots[i] = stack->slots[i + num];

	for(unsigned i = SLOT_MAX - 1 - num; i < SLOT_MAX - 1; i++)
		stack->slots[i] = 0.f;
}

static inline num_t
_stack_peek(stack_t *stack)
{
	const num_t val = stack->slots[0];

	return val;
}

static void
_intercept_graph(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->graph_size = impl->value.size;
	handle->recalc = true;

	handle->is_dynamic = vm_deserialize(&handle->opcode, &handle->forge, handle->cmds,
		impl->value.size, impl->value.body);

	handle->sync = true;
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = VM__graph,
		.offset = offsetof(plugstate_t, graph),
		.type = LV2_ATOM__Tuple,
		.max_size = GRAPH_SIZE,
		.event_mask = PROP_EVENT_WRITE,
		.event_cb = _intercept_graph,
	}
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, num_t rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

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

	lv2_atom_forge_init(&handle->forge, handle->map);
	vm_opcode_init(&handle->opcode, handle->map);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "props_init failed\n");
		free(handle);
		return NULL;
	}

	if(!props_register(&handle->props, defs, MAX_NPROPS, &handle->state, &handle->stash))
	{
		fprintf(stderr, "props_register failed\n");
		free(handle);
		return NULL;
	}

	handle->recalc = true;
	handle->srate = rate;

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
			handle->in[port - 2] = data;
			break;
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
			handle->out[port - 10] = data;
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->notify, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	int64_t last_t = 0;
	LV2_ATOM_SEQUENCE_FOREACH(handle->control, ev)
	{
		const LV2_Atom *atom= &ev->body;
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
	}

	if(handle->sync)
	{
		props_set(&handle->props, &handle->forge, nsamples - 1, handle->vm_graph, &handle->ref);
		handle->sync = false;
	}

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		if(handle->in0[i] != *handle->in[i])
		{
			handle->recalc = true;
			handle->in0[i] = *handle->in[i];
		}
	}

	if(handle->recalc || handle->is_dynamic)
	{
		_stack_clear(&handle->stack);

		for(unsigned i = 0; i < ITEMS_MAX; i++)
		{
			command_t *cmd = &handle->cmds[i];
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
				case COMMAND_LONG:
				{
					const num_t c = cmd->i64;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_FLOAT:
				{
					const num_t c = cmd->f32;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_DOUBLE:
				{
					const num_t c = cmd->f64;
					_stack_push(&handle->stack, c);
				} break;
				case COMMAND_OPCODE:
				{
					switch(cmd->op)
					{
						case OP_CTRL:
						{
							int idx = floor(_stack_pop(&handle->stack)); //FIXME check
							if(idx < 0)
								idx = 0;
							if(idx >= CTRL_MAX)
								idx = CTRL_MAX - 1;

							const num_t c = handle->in0[idx];
							_stack_push(&handle->stack, c);
						} break;
						case OP_PUSH:
						{
							const num_t c = _stack_peek(&handle->stack);
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
						case OP_POW:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t c = pow(ab[1], ab[0]);
							_stack_push(&handle->stack, c);
						} break;
						case OP_SQRT:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = sqrt(a);
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
						case OP_EXP_10:
						{
							const num_t a = _stack_pop(&handle->stack);
							const num_t c = exp10(a);
							_stack_push(&handle->stack, c);
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
						case OP_SWAP:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);
							const num_t cd [2] = {ab[1], ab[0]};
							_stack_push_num(&handle->stack, cd, 2);
						} break;
						case OP_FRAME:
						{
							num_t c = handle->frame;
							_stack_push(&handle->stack, c);
						} break;
						case OP_SRATE:
						{
							num_t c = handle->srate;
							_stack_push(&handle->stack, c);
						} break;
						case OP_PI:
						{
							num_t c = M_PI;
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
						case OP_TER:
						{
							num_t ab [3];
							_stack_pop_num(&handle->stack, ab, 3);
							const bool c = ab[0];
							_stack_push(&handle->stack, c ? ab[2] : ab[1]);
						} break;
						case OP_STORE:
						{
							num_t ab [2];
							_stack_pop_num(&handle->stack, ab, 2);

							int idx = floorf(ab[0]);
							if(idx < 0)
								idx = 0;
							if(idx >= REG_MAX)
								idx = REG_MAX - 1;

							handle->stack.regs[idx] = ab[1];
						} break;
						case OP_LOAD:
						{
							const num_t a = _stack_pop(&handle->stack);

							int idx = floorf(a);
							if(idx < 0)
								idx = 0;
							if(idx >= REG_MAX)
								idx = REG_MAX - 1;

							const num_t c = handle->stack.regs[idx];
							_stack_push(&handle->stack, c);
						} break;
						case OP_NOP:
						{
							terminate = true;
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
#if 0
		for(unsigned i = 0; i < CTRL_MAX; i++)
			printf("out %u: %f\n", i, handle->out0[i]);
#endif

		handle->recalc = false;
	}

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);

	for(unsigned i = 0; i < CTRL_MAX; i++)
		*handle->out[i] = handle->out0[i];;

	handle->frame += nsamples;
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

static inline LV2_Worker_Status
_work(LV2_Handle instance, LV2_Worker_Respond_Function respond,
LV2_Worker_Respond_Handle worker, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work(&handle->props, respond, worker, size, body);
}

static inline LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work_response(&handle->props, size, body);
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = NULL
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;

	return NULL;
}

const LV2_Descriptor vm_nuk = {
	.URI            = VM_PREFIX"vm",
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = NULL,
	.run            = run,
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
			return &vm_nuk;
		default:
			return NULL;
	}
}
