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

#define PLOT_MAX  256
#define PLOT_MASK (PLOT_MAX - 1)

typedef struct _atom_ser_t atom_ser_t;
typedef struct _plot_t plot_t;
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

struct _plot_t {
	float vals [PLOT_MAX];
	int window;
	double pre;
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
	vm_api_impl_t api [OP_MAX];

	float in0 [CTRL_MAX];
	float out0 [CTRL_MAX];

	int64_t off;
	plot_t inp [CTRL_MAX];
	plot_t outp [CTRL_MAX];

	float sample_rate;

	vm_command_t cmds [ITEMS_MAX];
};

static const char *input_labels [CTRL_MAX] = {
	"Input 0:",
	"Input 1:",
	"Input 2:",
	"Input 3:",
	"Input 4:",
	"Input 5:",
	"Input 6:",
	"Input 7:",
};

static void
_intercept_graph(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->graph_size = impl->value.size;

	vm_deserialize(handle->api, &handle->forge, handle->cmds,
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

static const struct nk_color plot_bg_color = {
	.r = 0x22, .g = 0x22, .b = 0x22, .a = 0xff
};

static const struct nk_color plot_fg_color = {
	.r = 0xbb, .g = 0x66, .b = 0x00, .a = 0xff
};

static inline void
_draw_plot(struct nk_context *ctx, const float *vals)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

	struct nk_rect bounds;
	const nk_flags states = nk_widget(&bounds, ctx);
	if(states != NK_WIDGET_INVALID)
	{
    const struct nk_rect old_clip = canvas->clip;
		const struct nk_rect outer = nk_pad_rect(bounds, nk_vec2(-2.f, -2.f));
		struct nk_rect new_clip;
    nk_unify(&new_clip, &old_clip, outer.x, outer.y,
			outer.x + outer.w, outer.y + outer.h);

		nk_push_scissor(canvas, new_clip);
		nk_fill_rect(canvas, bounds, 0.f, plot_bg_color);
		nk_stroke_rect(canvas, bounds, 0.f, 1.f, ctx->style.window.border_color);

		float mem [PLOT_MAX*2];
		float x0 = -1.f;
		unsigned j = 0;

		for(unsigned i = 0; i < PLOT_MAX; i++)
		{
			const float sx = (float)i / PLOT_MAX;
			const float x1 = bounds.x + sx*bounds.w;
			if(x1 - x0 < 1.f)
				continue;

			const float sy = vals[i] / VM_VIS;
			const float y1 = bounds.y + (0.5f - sy)*bounds.h;

			mem[j++] = x1;
			mem[j++] = y1;
			x0 = x1;
		}

		const float yh = bounds.y + 0.5f*bounds.h;
		nk_stroke_line(canvas, bounds.x, yh, bounds.x + bounds.w, yh, 1.f,
			ctx->style.window.border_color);
		nk_stroke_polyline(canvas, mem, j/2, 1.f, plot_fg_color);
		nk_push_scissor(canvas, old_clip);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	handle->dy = 20.f * nk_pugl_get_scale(&handle->win);
	const float dy = handle->dy;

	// mouse sensitivity for dragable property widgets
	const bool has_control = nk_input_is_key_down(&ctx->input, NK_KEY_CTRL);
	const bool has_shift = nk_input_is_key_down(&ctx->input, NK_KEY_SHIFT);
	float scl = 1.f;
	if(has_control)
		scl /= 4;
	if(has_shift)
		scl /= 4;

	if(nk_begin(ctx, "Vm", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);
		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

		const float wh = wbounds.h
			- 2*ctx->style.window.padding.y
			- 2*ctx->style.window.border;

		const float ratio [3] = {0.25f, 0.5f, 0.25f};
		nk_layout_row(ctx, NK_DYNAMIC, wh, 3, ratio);

		if(nk_group_begin(ctx, "Inputs", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			float stp;
			float fpp;

			for(unsigned i = 0; i < CTRL_MAX; i++)
			{
				nk_layout_row_dynamic(ctx, dy*4, 1);
				_draw_plot(ctx, handle->inp[i].vals);

				nk_layout_row_dynamic(ctx, dy, 2);
				if(i == 0) // calculate only once
				{
					stp = scl * VM_STP;
					fpp = scl * VM_RNG / nk_widget_width(ctx);
				}
				const float old_val = handle->in0[i];
				nk_property_float(ctx, input_labels[i], VM_MIN, &handle->in0[i], VM_MAX, stp, fpp);
				if(old_val != handle->in0[i])
					handle->writer(handle->controller, i + 2, sizeof(float), 0, &handle->in0[i]);

				const int old_window = handle->inp[i].window;
				nk_property_int(ctx, "#ms", 10, &handle->inp[i].window, 100000, 1, 1.f);
				if(old_window != handle->inp[i].window)
					memset(handle->inp[i].vals, 0x0, sizeof(float)*PLOT_MAX);
			}

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Program", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			nk_layout_row_dynamic(ctx, dy, 2);
			bool sync = false;

			const float stp = scl * VM_STP;
			const float fpp = scl * VM_RNG / nk_widget_width(ctx);

			for(unsigned i = 0; i < ITEMS_MAX; i++)
			{
				vm_command_t *cmd = &handle->cmds[i];
				bool terminate = false;

				const vm_command_enum_t old_cmd_type = cmd->type;
				int cmd_type = old_cmd_type;
				nk_combobox(ctx, command_labels, COMMAND_MAX, &cmd_type,
					dy, nk_vec2(nk_widget_width(ctx), dy*14));
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
						nk_property_int(ctx, "#", INT32_MIN, &i32, INT32_MAX, 1, 1.f);
						if(i32 != cmd->i32)
						{
							cmd->i32 = i32;
							sync = true;
						}
					} break;
					case COMMAND_FLOAT:
					{
						float f32 = cmd->f32;
						nk_property_float(ctx, "#", -HUGE, &f32, HUGE, stp, fpp);
						if(f32 != cmd->f32)
						{
							cmd->f32 = f32;
							sync = true;
						}
					} break;
					case COMMAND_OPCODE:
					{
						const bool show_mnemo = true; //FIXME
						const char *desc = show_mnemo && vm_api_def[cmd->op].mnemo
							? vm_api_def[cmd->op].mnemo
							: vm_api_def[cmd->op].label;
						if(nk_combo_begin_label(ctx, desc, nk_vec2(nk_widget_width(ctx), dy*14)))
						{
							nk_layout_row_dynamic(ctx, dy, 1);
							for(unsigned op = 0; op < OP_MAX; op++)
							{
								desc = show_mnemo && vm_api_def[op].mnemo
									? vm_api_def[op].mnemo
									: vm_api_def[op].label;
								if(nk_combo_item_label(ctx, desc, NK_TEXT_LEFT))
								{
									cmd->op = op;
									sync = true;
								}
							}
							nk_combo_end(ctx);
						}
					} break;
					case COMMAND_NOP:
					{
						struct nk_keyboard *keybd = &ctx->input.keyboard;

						if(keybd->text_len == 1)
						{
							const char key = keybd->text[0];

							switch(key)
							{
								case 'o':
								{
									cmd->type = COMMAND_OPCODE;
									cmd->op = OP_NOP;
								}	break;
								case 'b':
								{
									cmd->type = COMMAND_BOOL;
									cmd->i32 = 0;
								}	break;
								case 'i':
								{
									cmd->type = COMMAND_INT;
									cmd->i32 = 0;
								}	break;
								case 'f':
								{
									cmd->type = COMMAND_FLOAT;
									cmd->f32= 0;
								}	break;

								default:
								{
									terminate = true;

									for(unsigned op = 0; op < OP_MAX; op++)
									{
										if(vm_api_def[op].key == key)
										{
											cmd->type = COMMAND_OPCODE;
											cmd->op = op;
											terminate = false;
											break;
										}
									}
								}	break;
							}

							if(!terminate)
							{
								keybd->text_len = 0;
								sync = true;
							}
						}
						else
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

			// add spacing so we can see the whole combobox of last elements
			nk_layout_row_dynamic(ctx, dy, 1);
			nk_spacing(ctx, 10);

			if(sync)
			{
				atom_ser_t *ser = &handle->ser;
				ser->offset = 0;
				lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);
				vm_serialize(handle->api, &handle->forge, handle->cmds);
				props_impl_t *impl = _props_impl_search(&handle->props, handle->vm_graph);
				if(impl)
					_props_set(&handle->props, impl, ser->atom->type, ser->atom->size, LV2_ATOM_BODY_CONST(ser->atom));

				_set_property(handle, handle->vm_graph);
			}

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Outputs", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			for(unsigned i = 0; i < CTRL_MAX; i++)
			{
				nk_layout_row_dynamic(ctx, dy*4, 1);
				_draw_plot(ctx, handle->outp[i].vals);

				nk_layout_row_dynamic(ctx, dy, 2);
				nk_labelf(ctx, NK_TEXT_LEFT, "Output %u: %+f", i, handle->out0[i]);

				const int old_window = handle->outp[i].window;
				nk_property_int(ctx, "#ms", 10, &handle->outp[i].window, 100000, 1, 1.f);
				if(old_window != handle->outp[i].window)
					memset(handle->outp[i].vals, 0x0, sizeof(float)*PLOT_MAX);
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
	LV2_Options_Option *opts = NULL;
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
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			opts = features[i]->data;
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

	const LV2_URID param_sampleRate = handle->map->map(handle->map->handle, LV2_PARAMETERS__sampleRate);
	if(opts)
	{
		for(LV2_Options_Option *opt = opts;
			(opt->key != 0) && (opt->value != NULL);
			opt++)
		{
			if( (opt->key == param_sampleRate) && (opt->type == handle->forge.Float) )
				handle->sample_rate = *(const float *)opt->value;
		}
	}
	if(!handle->sample_rate)
		handle->sample_rate = 48000.f; // fall-back

	vm_api_init(handle->api, handle->map);

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

	for(unsigned i = 0; i < CTRL_MAX; i++)
	{
		handle->inp[i].window = 1000; // 1 second window
		handle->outp[i].window = 1000; // 1 second window
	}

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
				const LV2_Atom *atom = buf;

				if(atom->type == handle->forge.Long)
				{
					const LV2_Atom_Long *off = buf;

					const int64_t dt = off->body - handle->off;
					handle->off = off->body;
					const float dts = 1000.f * dt / handle->sample_rate; // in seconds

					float mem [PLOT_MAX];
					bool needs_refresh = false;

					//FIXME can get out of sync

					for(unsigned i = 0; i < CTRL_MAX; i++)
					{
						// inputs
						{
							handle->inp[i].pre += PLOT_MAX * dts / handle->inp[i].window;
							double intp;
							const double frac = modf(handle->inp[i].pre, &intp);

							if(intp > 0)
							{
								handle->inp[i].pre = frac;

								unsigned pre = floorf(intp);
								pre &= PLOT_MASK;
								const unsigned post = PLOT_MAX - pre;

								memcpy(mem, &handle->inp[i].vals[pre], sizeof(float)*post);
								for(unsigned j = post; j < PLOT_MAX; j++)
									mem[j] = handle->in0[i];

								//FIXME can be made more efficient
								if(memcmp(handle->inp[i].vals, mem, sizeof(float)*PLOT_MAX))
									needs_refresh = true;

								memcpy(handle->inp[i].vals, mem, sizeof(float)*PLOT_MAX);
							}
						}

						// outputs
						{
							handle->outp[i].pre += PLOT_MAX * dts / handle->outp[i].window;
							double intp;
							const double frac = modf(handle->outp[i].pre, &intp);

							if(intp > 0)
							{
								handle->outp[i].pre = frac;

								unsigned pre = floorf(intp);
								pre &= PLOT_MASK;
								const unsigned post = PLOT_MAX - pre;

								memcpy(mem, &handle->outp[i].vals[pre], sizeof(float)*post);
								for(unsigned j = post; j < PLOT_MAX; j++)
									mem[j] = handle->out0[i];

								//FIXME can be made more efficient
								if(memcmp(handle->outp[i].vals, mem, sizeof(float)*PLOT_MAX))
									needs_refresh = true;

								memcpy(handle->outp[i].vals, mem, sizeof(float)*PLOT_MAX);
							}
						}
					}

					if(needs_refresh)
						nk_pugl_post_redisplay(&handle->win);
				}
				else if(atom->type == handle->forge.Tuple)
				{
					const LV2_Atom_Int *idx = buf + 8;
					const LV2_Atom_Float *val = buf + 8 + 16;

					if(idx->body < 10)
						handle->in0[idx->body - 2] = val->body;
					else
						handle->out0[idx->body - 10] = val->body;
				}
				else // !tuple
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
