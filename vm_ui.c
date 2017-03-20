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
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <inttypes.h>

#include <vm.h>

#define NK_PUGL_IMPLEMENTATION
#include "nk_pugl/nk_pugl.h"

#ifdef Bool
#	undef Bool
#endif

typedef struct _atom_ser_t atom_ser_t;
typedef struct _plughandle_t plughandle_t;

struct _atom_ser_t {
	uint32_t size;
	uint32_t offset;
	union {
		const LV2_Atom *atom;
		const LV2_Atom_Event *ev;
		uint8_t *buf;
	};
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;
	LV2_URID vm_graph;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	nk_pugl_window_t win;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;

	PROPS_T(props, MAX_NPROPS);
	plugstate_t state;
	plugstate_t stash;

	uint32_t graph_size;

	float dy;

	atom_ser_t ser;
	opcode_t opcode;

	float in0 [CTRL_MAX];
	float out0 [CTRL_MAX];

	command_t cmds [ITEMS_MAX];
};

typedef struct _desc_t desc_t;

struct _desc_t {
	const char *label;
	unsigned npush;
	unsigned npop;
};

static const char *op_labels [OP_MAX] = {
	[OP_NOP]    = "-",
	[OP_CTRL]   = "Control        (1:1)",
	[OP_PUSH]   = "Push           (1:2)",
	[OP_ADD]    = "Add            (2:1)",
	[OP_SUB]    = "Sub            (2:1)",
	[OP_MUL]    = "Mul            (2:1)",
	[OP_DIV]    = "Div            (2:1)",
	[OP_NEG]    = "Neg            (1:1)",
	[OP_ABS]    = "Abs            (1:1)",
	[OP_POW]    = "Pow            (2:1)",
	[OP_SQRT]   = "Sqrt           (1:1)",
	[OP_MOD]    = "Mod            (2:1)",
	[OP_EXP]    = "Exp            (1:1)",
	[OP_EXP_2]  = "Exp2           (1:1)",
	[OP_LOG]    = "Log            (1:1)",
	[OP_LOG_2]  = "Log2           (1:1)",
	[OP_LOG_10] = "Log10          (1:1)",
	[OP_SIN]    = "Sin            (1:1)",
	[OP_COS]    = "Cos            (1:1)",
	[OP_SWAP]   = "Swap           (2:2)",
	[OP_FRAME]  = "Frame          (-:1)",
	[OP_SRATE]  = "Sample Rate    (-:1)",
	[OP_PI]     = "Pi             (-:1)",
	[OP_EQ]     = "Equal          (2:1)",
	[OP_LT]     = "LessThan       (2:1)",
	[OP_GT]     = "GreaterThan    (2:1)",
	[OP_LE]     = "LessOrEqual    (2:1)",
	[OP_GE]     = "GreaterOrEqual (2:1)",
	[OP_AND]    = "Boolean And    (2:1)",
	[OP_OR]     = "Boolean Or     (2:1)",
	[OP_NOT]    = "Boolean Not    (1:1)",
	[OP_BAND]   = "Bitwise And    (2:1)",
	[OP_BOR]    = "Bitwise Or     (2:1)",
	[OP_BNOT]   = "Bitwise Not    (1:1)",
	[OP_TER]    = "Ternary        (3:1)",
	[OP_STORE]  = "Store          (2:0)",
	[OP_LOAD]   = "Load           (1:1)",
};

static const char *cmd_labels [COMMAND_MAX] = {
	[COMMAND_NOP]    = "-",
	[COMMAND_OPCODE] = "Op Code"	,
	[COMMAND_BOOL]   = "Boolean"	,
	[COMMAND_INT]    = "Integer"	,
	[COMMAND_LONG]   = "Long"	,
	[COMMAND_FLOAT]  = "Float"	,
	[COMMAND_DOUBLE] = "Double"
};

static void
_intercept_graph(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->graph_size = impl->value.size;

	vm_deserialize(&handle->opcode, &handle->forge, handle->cmds,
		impl->value.size, impl->value.body);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = VM__graph,
		.offset = offsetof(plugstate_t, graph),
		.type = LV2_ATOM__Tuple,
		.max_size = GRAPH_SIZE,
		.event_mask = PROP_EVENT_WRITE,
		.event_cb = _intercept_graph
	}
};

static LV2_Atom_Forge_Ref
_sink(LV2_Atom_Forge_Sink_Handle handle, const void *buf, uint32_t size)
{
	atom_ser_t *ser = handle;

	const LV2_Atom_Forge_Ref ref = ser->offset + 1;

	const uint32_t new_offset = ser->offset + size;
	if(new_offset > ser->size)
	{
		uint32_t new_size = ser->size << 1;
		while(new_offset > new_size)
			new_size <<= 1;

		if(!(ser->buf = realloc(ser->buf, new_size)))
			return 0; // realloc failed

		ser->size = new_size;
	}

	memcpy(ser->buf + ser->offset, buf, size);
	ser->offset = new_offset;

	return ref;
}

static LV2_Atom *
_deref(LV2_Atom_Forge_Sink_Handle handle, LV2_Atom_Forge_Ref ref)
{
	atom_ser_t *ser = handle;

	const uint32_t offset = ref - 1;

	return (LV2_Atom *)(ser->buf + offset);
}

static void
_restore_state(plughandle_t *handle)
{
	atom_ser_t *ser = &handle->ser;
	ser->offset = 0;
	lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_object(&handle->forge, &frame, 0, handle->props.urid.patch_get);
	lv2_atom_forge_pop(&handle->forge, &frame);

	const LV2_Atom *atom = ser->atom;
	handle->writer(handle->controller, 0, lv2_atom_total_size(atom),
		handle->atom_eventTransfer, atom);
}

static void
_set_property(plughandle_t *handle, LV2_URID property)
{
	atom_ser_t *ser = &handle->ser;
	ser->offset = 0;
	lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);

	LV2_Atom_Forge_Ref ref = 1;
	props_set(&handle->props, &handle->forge, 0, property, &ref);

	const LV2_Atom_Event *ev = ser->ev;
	const LV2_Atom *atom = &ev->body;
	handle->writer(handle->controller, 0, lv2_atom_total_size(atom),
		handle->atom_eventTransfer, atom);
}

static bool
_tooltip_visible(struct nk_context *ctx)
{
	return nk_widget_has_mouse_click_down(ctx, NK_BUTTON_RIGHT, nk_true)
		|| (nk_widget_is_hovered(ctx) && nk_input_is_key_down(&ctx->input, NK_KEY_CTRL));
}

static inline void
_draw_separator(struct nk_context *ctx, float line_width)
{
	const struct nk_rect b = nk_widget_bounds(ctx);
	const float x0 = b.x;
	const float x1 = b.x + b.w;
	const float y = b.y + b.h;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	nk_stroke_line(canvas, x0, y, x1, y, line_width, ctx->style.window.background);
}

#define VM_MIN -0x2000
#define VM_MAX 0x1fff

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	handle->dy = 20.f * nk_pugl_get_scale(&handle->win);
	const float dy = handle->dy;

	if(nk_begin(ctx, "Vm", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);
		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

		const float wh = wbounds.h
			- 2*ctx->style.window.padding.y
			- 2*ctx->style.window.border;
		nk_layout_row_dynamic(ctx, wh, 2);

		if(nk_group_begin(ctx, "Controls", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			if(nk_tree_push(ctx, NK_TREE_NODE, "Inputs", NK_MAXIMIZED))
			{
				for(unsigned i = 0; i < CTRL_MAX; i++)
				{
					char label [16];
					snprintf(label, 16, "Input %u:", i);

					const float old_val = handle->in0[i];
					nk_property_float(ctx, label, VM_MIN, &handle->in0[i], VM_MAX, 1.f, 1.f);
					nk_slider_float(ctx, VM_MIN, &handle->in0[i], VM_MAX, 1.f);
					if(old_val != handle->in0[i])
						handle->writer(handle->controller, i + 2, sizeof(float), 0, &handle->in0[i]);
				}
				nk_tree_pop(ctx);
			}

			nk_spacing(ctx, 1);

			if(nk_tree_push(ctx, NK_TREE_NODE, "Outputs", NK_MAXIMIZED))
			{
				for(unsigned i = 0; i < CTRL_MAX; i++)
				{
					char label [16];
					snprintf(label, 16, "Output %u", i);
					nk_value_float(ctx, label, handle->out0[i]);
					nk_slide_float(ctx, VM_MIN, handle->out0[i], VM_MAX, 1.f);
				}
				nk_tree_pop(ctx);
			}

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Program", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			nk_layout_row_dynamic(ctx, dy, 2);
			bool sync = false;

			for(unsigned i = 0; i < ITEMS_MAX; i++)
			{
				command_t *cmd = &handle->cmds[i];
				bool terminate = false;

				const command_enum_t old_cmd_type = cmd->type;
				int cmd_type = old_cmd_type;
				nk_combobox(ctx, cmd_labels, COMMAND_MAX, &cmd_type, dy, nk_vec2(nk_widget_width(ctx), dy*COMMAND_MAX));
				if(old_cmd_type != cmd_type)
				{
					cmd->type = cmd_type;
					cmd->i32 = 0; // reset value
					sync = true;
				}

				switch(cmd->type)
				{
					case COMMAND_BOOL:
					{
						if(nk_button_symbol_label(ctx,
							cmd->i32 ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE,
							cmd->i32 ? "true" : "false", NK_TEXT_LEFT))
						{
							cmd->i32 = !cmd->i32;
							sync = true;
						}
					} break;
					case COMMAND_INT:
					{
						int i32 = cmd->i32;
						nk_property_int(ctx, "#", VM_MIN, &i32, VM_MAX, 1, 1.f);
						if(i32 != cmd->i32)
						{
							cmd->i32 = i32;
							sync = true;
						}
					} break;
					case COMMAND_LONG:
					{
						int i64 = cmd->i64;
						nk_property_int(ctx, "#", VM_MIN, &i64, VM_MAX, 1, 1.f);
						if(i64 != cmd->i64)
						{
							cmd->i64 = i64;
							sync = true;
						}
					} break;
					case COMMAND_FLOAT:
					{
						float f32 = cmd->f32;
						nk_property_float(ctx, "#", VM_MIN, &f32, VM_MAX, 1.f, 1.f);
						if(f32 != cmd->f32)
						{
							cmd->f32 = f32;
							sync = true;
						}
					} break;
					case COMMAND_DOUBLE:
					{
						double f64 = cmd->f64;
						nk_property_double(ctx, "#", VM_MIN, &f64, VM_MAX, 1.f, 1.f);
						if(f64 != cmd->f64)
						{
							cmd->f64 = f64;
							sync = true;
						}
					} break;
					case COMMAND_OPCODE:
					{
						int op = cmd->op;
						nk_combobox(ctx, op_labels, OP_MAX, &op, dy, nk_vec2(nk_widget_width(ctx), dy*OP_MAX));
						if(op != cmd->op)
						{
							cmd->op = op;
							sync = true;
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
				{
					for(unsigned j = i; j < ITEMS_MAX; j++)
						handle->cmds[j].type = COMMAND_NOP;

					break;
				}
			}

			if(sync)
			{
				atom_ser_t *ser = &handle->ser;
				ser->offset = 0;
				lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);
				vm_serialize(&handle->opcode, &handle->forge, handle->cmds);
				props_impl_t *impl = _props_impl_search(&handle->props, handle->vm_graph);
				if(impl)
					_props_set(&handle->props, impl, ser->atom->type, ser->atom->size, LV2_ATOM_BODY_CONST(ser->atom));

				_set_property(handle, handle->vm_graph);
			}

			nk_group_end(ctx);
		}
	}

	nk_end(ctx);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
			host_resize = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!parent)
	{
		fprintf(stderr,
			"%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
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

	lv2_atom_forge_init(&handle->forge, handle->map);

	vm_opcode_init(&handle->opcode, handle->map);

	if(!props_init(&handle->props, MAX_NPROPS, plugin_uri, handle->map, handle))
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

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);
	handle->vm_graph = handle->map->map(handle->map->handle, VM__graph);

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 720;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "vm";
	cfg->title = "Vm";
	cfg->parent = (intptr_t)parent;
	cfg->host_resize = host_resize;
	cfg->data = handle;
	cfg->expose = _expose;

	if(asprintf(&cfg->font.face, "%sCousine-Regular.ttf", bundle_path) == -1)
		cfg->font.face = NULL;
	cfg->font.size = 13;
	
	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	atom_ser_t *ser = &handle->ser;
	ser->size = 1024;
	ser->buf = malloc(ser->size);

	_restore_state(handle);

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->win.cfg.font.face)
		free(handle->win.cfg.font.face);
	nk_pugl_hide(&handle->win);
	nk_pugl_shutdown(&handle->win);

	atom_ser_t *ser = &handle->ser;
	if(ser->buf)
		free(ser->buf);

	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	plughandle_t *handle = instance;

	switch(index)
	{
		case 1:
		{
			if(protocol == handle->atom_eventTransfer)
			{
				const LV2_Atom_Object *obj = buf;

				atom_ser_t *ser = &handle->ser;
				ser->offset = 0;
				lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);

				LV2_Atom_Forge_Ref ref;
				if(props_advance(&handle->props, &handle->forge, 0, obj, &ref))
				{
					nk_pugl_post_redisplay(&handle->win);
				}
			}
		} break;

		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		{
			if(protocol == 0)
			{
				handle->in0[index - 2] = *(const float *)buf;
				nk_pugl_post_redisplay(&handle->win);
			}
		} break;

		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		{
			if(protocol == 0)
			{
				handle->out0[index - 10] = *(const float *)buf;
				nk_pugl_post_redisplay(&handle->win);
			}
		} break;
	}
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	return nk_pugl_process_events(&handle->win);
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
		
	return NULL;
}

const LV2UI_Descriptor vm_vm_ui = {
	.URI            = VM_PREFIX"vm_ui",
	.instantiate    = instantiate,
	.cleanup        = cleanup,
	.port_event     = port_event,
	.extension_data = extension_data
};

LV2_SYMBOL_EXPORT const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &vm_vm_ui;
		default:
			return NULL;
	}
}
