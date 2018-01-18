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

	vm_plug_enum_t vm_plug;

	LV2_URID atom_eventTransfer;
	LV2_URID vm_graph;
	LV2_URID vm_sourceFilter;
	LV2_URID vm_destinationFilter;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	nk_pugl_window_t win;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;

	PROPS_T(props, MAX_NPROPS);
	plugstate_t state;
	plugstate_t stash;

	uint32_t graph_size;
	uint32_t sourceFilter_size;
	uint32_t destinationFilter_size;

	float dy;

	atom_ser_t ser;
	vm_api_impl_t api [OP_MAX];
	vm_filter_t sourceFilter [CTRL_MAX];
	vm_filter_t destinationFilter [CTRL_MAX];
	vm_filter_impl_t filt;

	float in0 [CTRL_MAX];
	float out0 [CTRL_MAX];

	float in1 [CTRL_MAX];
	float out1 [CTRL_MAX];

	float in2 [CTRL_MAX];
	float out2 [CTRL_MAX];

	int64_t off;
	plot_t inp [CTRL_MAX];
	plot_t outp [CTRL_MAX];

	float sample_rate;

	vm_command_t cmds [ITEMS_MAX];
};

static const char *input_labels [CTRL_MAX] = {
	"In 0:",
	"In 1:",
	"In 2:",
	"In 3:",
	"In 4:",
	"In 5:",
	"In 6:",
	"In 7:",
};

static const struct nk_color plot_bg_color = {
	.r = 0x22, .g = 0x22, .b = 0x22, .a = 0xff
};

static const struct nk_color plot_fg_color = {
	.r = 0xbb, .g = 0x66, .b = 0x00, .a = 0xff
};

static const struct nk_color plot_fg2_color = {
	.r = 0xbb, .g = 0x66, .b = 0x00, .a = 0x7f
};

static const char *ms_label = "#ms:";
static const char *nil_label = "#";
static const char *chn_label = "#chn:";
static const char *val_label = "#val:";

static void
_intercept_graph(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->graph_size = impl->value.size;

	vm_graph_deserialize(handle->api, &handle->forge, handle->cmds,
		impl->value.size, impl->value.body);
}

static void
_intercept_sourceFilter(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->sourceFilter_size = impl->value.size;

	const int status = vm_filter_deserialize(&handle->forge, &handle->filt,
		handle->sourceFilter, impl->value.size, impl->value.body);
	(void)status; //FIXME
}

static void
_intercept_destinationFilter(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	handle->destinationFilter_size = impl->value.size;

	const int status = vm_filter_deserialize(&handle->forge, &handle->filt,
		handle->destinationFilter, impl->value.size, impl->value.body);
	(void)status; //FIXME
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = VM__graph,
		.offset = offsetof(plugstate_t, graph),
		.type = LV2_ATOM__Tuple,
		.max_size = GRAPH_SIZE,
		.event_cb = _intercept_graph
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

static inline void
_draw_plot(struct nk_context *ctx, const float *vals, vm_plug_enum_t vm_plug)
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

		float mem [PLOT_MAX*4];
		float x0 = -1.f;
		unsigned J = 0;

		const float yh = bounds.y + 0.5f*bounds.h;

		for(unsigned i = 0; i < PLOT_MAX; i++)
		{
			const float sx = (float)i / PLOT_MAX;
			const float x1 = bounds.x + sx*bounds.w;
			if(x1 - x0 < 1.f)
				continue;

			const float sy = vals[i] / VM_VIS;

			if(vm_plug == VM_PLUG_AUDIO)
			{
				mem[J++] = x1;
				mem[J++] = fabsf(sy);
				x0 = x1;
			}
			else
			{
				const float dy = sy*bounds.h;

				mem[J++] = x1;
				mem[J++] = yh - dy;
				x0 = x1;
			}
		}

		nk_stroke_line(canvas, bounds.x, yh, bounds.x + bounds.w, yh, 1.f,
			ctx->style.window.border_color);

		if(vm_plug == VM_PLUG_AUDIO)
		{
			for(unsigned j = 0, k = 2*J; j < J; j += 2, k -= 2)
			{
				const float x = mem[j];
				const float sy = mem[j + 1];
				const float dy = sy*bounds.h;
				const float y1 = yh - dy;
				const float y2 = yh + dy;

				mem[j + 1] = y1;

				mem[k - 1] = y2;
				mem[k - 2] = x;
			}

			nk_stroke_polyline(canvas, &mem[J], J/2, 1.f, plot_fg2_color);
		}

		nk_stroke_polyline(canvas, mem, J/2, 1.f, plot_fg_color);

		nk_push_scissor(canvas, old_clip);
	}
}

#define dBFS6_min -54
#define dBFS6_max 6
#define dBFS6_div 2
#define dBFS6_rng (dBFS6_max - dBFS6_min)

static const float mx1 = (float)(dBFS6_rng - 2*dBFS6_max) / dBFS6_rng;
static const float mx2 = (float)(2*dBFS6_max) / dBFS6_rng;

static inline float
dBFS6(float peak)
{
	const float d = dBFS6_max + 20.f * log10f(fabsf(peak) / dBFS6_div);
	const float e = (d - dBFS6_min) / dBFS6_rng;
	return NK_CLAMP(0.f, e, 1.f);
}

static inline void
_draw_mixer(struct nk_context *ctx, float peak)
{
	struct nk_rect bounds;
	const enum nk_widget_layout_states states = nk_widget(&bounds, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

		const struct nk_color bg = ctx->style.property.normal.data.color;
		nk_fill_rect(canvas, bounds, ctx->style.property.rounding, bg);
		nk_stroke_rect(canvas, bounds, ctx->style.property.rounding, ctx->style.property.border, bg);

		const struct nk_rect orig = bounds;
		struct nk_rect outline;
		const uint8_t alph = 0x7f;

		const float ox = ctx->style.font->height/2 + ctx->style.property.border + ctx->style.property.padding.x;
		const float oy = ctx->style.property.border + ctx->style.property.padding.y;
		bounds.x += ox;
		bounds.y += oy;
		bounds.w -= 2*ox;
		bounds.h -= 2*oy;
		outline = bounds;

		if(peak > 0.f) //FIXME use threshold > 0.f
		{
			// <= -6dBFS
			{
				const float dbfs = NK_MIN(peak, mx1);
				const uint8_t dcol = 0xff * dbfs / mx1;
				const struct nk_color left = nk_rgba(0x00, 0xff, 0xff, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(dcol, 0xff, 0xff-dcol, alph);
				const struct nk_color top = right;

				bounds.w *= dbfs;

				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// > 6dBFS
			if(peak > mx1)
			{
				const float dbfs = peak - mx1;
				const uint8_t dcol = 0xff * dbfs / mx2;
				const struct nk_color left = nk_rgba(0xff, 0xff, 0x00, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(0xff, 0xff - dcol, 0x00, alph);
				const struct nk_color top = right;

				bounds = outline;
				bounds.x += bounds.w * mx1;
				bounds.w *= dbfs;
				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}
		}

		// draw 6dBFS lines from -60 to +6
		for(unsigned i = 0; i <= dBFS6_rng; i += dBFS6_max)
		{
			const bool is_zero = (i == dBFS6_rng - dBFS6_max);
			const float dx = outline.w * i / dBFS6_rng;

			const float x0 = outline.x + dx;
			const float y0 = is_zero ? orig.y + 2.f : outline.y;

			const float border = (is_zero ? 2.f : 1.f) * ctx->style.window.group_border;

			const float x1 = x0;
			const float y1 = is_zero ? orig.y + orig.h - 2.f : outline.y + outline.h;

			nk_stroke_line(canvas, x0, y0, x1, y1, border, ctx->style.window.group_border_color);
		}

		nk_stroke_rect(canvas, outline, 0.f, ctx->style.window.group_border, ctx->style.window.group_border_color);
	}
}

static void
_wheel_float(struct nk_context *ctx, float *value, float stp)
{
	const struct nk_rect bounds = nk_widget_bounds(ctx);

	if(nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
	{
		if(ctx->input.mouse.scroll_delta.y > 0.f)
		{
			*value += stp;
			ctx->input.mouse.scroll_delta.y = 0.f;
		}
		else if(ctx->input.mouse.scroll_delta.y < 0.f)
		{
			*value -= stp;
			ctx->input.mouse.scroll_delta.y = 0.f;
		}
	}
}

static void
_wheel_int(struct nk_context *ctx, int *value)
{
	const struct nk_rect bounds = nk_widget_bounds(ctx);

	if(nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
	{
		if(ctx->input.mouse.scroll_delta.y > 0.f)
		{
			*value += 1;
			ctx->input.mouse.scroll_delta.y = 0.f;
		}
		else if(ctx->input.mouse.scroll_delta.y < 0.f)
		{
			*value -= 1;
			ctx->input.mouse.scroll_delta.y = 0.f;
		}
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

	const char *window_name = "Vm";
	if(nk_begin(ctx, window_name, wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
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

			bool sync = false;

			for(unsigned i = 0; i < CTRL_MAX; i++)
			{
				nk_layout_row_dynamic(ctx, dy*4, 1);
				_draw_plot(ctx, handle->inp[i].vals, handle->vm_plug);

				nk_layout_row_dynamic(ctx, dy, 2);
				if(  (handle->vm_plug == VM_PLUG_CONTROL)
					|| (handle->vm_plug == VM_PLUG_CV)
					|| (handle->vm_plug == VM_PLUG_ATOM)
					|| (handle->vm_plug == VM_PLUG_MIDI) )
				{
					if(i == 0) // calculate only once
					{
						stp = scl * VM_STP;
						fpp = scl * VM_RNG / nk_widget_width(ctx);
					}
					const float old_val = handle->in0[i];
					_wheel_float(ctx, &handle->in0[i], stp);
					nk_property_float(ctx, input_labels[i], VM_MIN, &handle->in0[i], VM_MAX, stp, fpp);

					if(old_val != handle->in0[i])
					{
						if(handle->vm_plug == VM_PLUG_ATOM)
						{
							const LV2_Atom_Float flt = {
								.atom = {
									.size = sizeof(float),
									.type = handle->forge.Float
								},
								.body = handle->in0[i]
							};

							handle->writer(handle->controller, i + 2,
								lv2_atom_total_size(&flt.atom), handle->atom_eventTransfer, &flt);
						}
						else if(handle->vm_plug == VM_PLUG_ATOM)
						{
							//FIXME send MIDI to DSP
						}
						else // CONTROL, CV
						{
							handle->writer(handle->controller, i + 2,
								sizeof(float), 0, &handle->in0[i]);
						}
					}
				}
				else if(handle->vm_plug == VM_PLUG_AUDIO)
				{
					_draw_mixer(ctx, handle->in1[i]);

					if(handle->in2[i] > handle->in1[i])
						handle->in1[i] = handle->in2[i];
					else
						handle->in1[i] -= (handle->in1[i] - handle->in2[i]) * 0.1; //FIXME make dependent on ui:updateRate
				}

				const int old_window = handle->inp[i].window;
				_wheel_int(ctx, &handle->inp[i].window);
				nk_property_int(ctx, ms_label, 10, &handle->inp[i].window, 100000, 1, 1.f);
				if(old_window != handle->inp[i].window)
					memset(handle->inp[i].vals, 0x0, sizeof(float)*PLOT_MAX);

				if(handle->vm_plug == VM_PLUG_MIDI)
				{
					vm_filter_t *filter = &handle->sourceFilter[i];

					nk_layout_row_dynamic(ctx, dy, 3);

					int filter_type = filter->type;
					nk_combobox(ctx, filter_labels, FILTER_MAX, &filter_type,
						dy, nk_vec2(nk_widget_width(ctx), dy*14)); //FIXME
					if(filter->type != filter_type)
					{
						filter->type = filter_type;
						sync = true;
					}

					const int old_channel = filter->channel;
					filter->channel = nk_propertyi(ctx, chn_label, 0x0, old_channel, 0x7, 1, 1.f);
					if(old_channel != filter->channel)
						sync = true;

					switch(filter->type)
					{
						case FILTER_CONTROLLER:
						{
							const int old_value = filter->value;
							filter->value = nk_propertyi(ctx, val_label, 0x0, old_value, 0x7f, 1, 1.f);
							if(old_value != filter->value)
								sync = true;
						} break;

						case FILTER_BENDER:
						case FILTER_MAX:
						{
							// nothing
						} break;
					}
				}
			}

			if(sync)
			{
				atom_ser_t *ser = &handle->ser;
				ser->offset = 0;
				lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);
				vm_filter_serialize(&handle->forge, &handle->filt, handle->sourceFilter);
				props_impl_t *impl = _props_bsearch(&handle->props, handle->vm_sourceFilter);
				if(impl)
					_props_impl_set(&handle->props, impl, ser->atom->type, ser->atom->size, LV2_ATOM_BODY_CONST(ser->atom));

				_set_property(handle, handle->vm_sourceFilter);
			}

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Program", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			const float ratio2 [6] = {
				0.1, 0.05, 0.05, 0.05, 0.3, 0.45
			};
			nk_layout_row(ctx, NK_DYNAMIC, dy, 6, ratio2);

			bool sync = false;

			const float stp = scl * VM_STP;
			const float fpp = scl * VM_RNG / nk_widget_width(ctx);

			for(unsigned i = 0; i < ITEMS_MAX; i++)
			{
				vm_command_t *cmd = &handle->cmds[i];
				bool terminate = false;

				nk_labelf(ctx, NK_TEXT_CENTERED, "%03u", i);
				if(cmd->type == COMMAND_NOP)
				{
					nk_spacing(ctx, 3);
				}
				else
				{
					if(nk_button_label(ctx, "+")) // insert cmd
					{
						for(unsigned j = ITEMS_MAX - 1; j > i; j--)
							handle->cmds[j] = handle->cmds[j - 1];

						cmd->type = COMMAND_OPCODE;
						cmd->i32 = 0;
						sync = true;
					}

					if(nk_button_label(ctx, "-")) // remove cmd
					{
						for(unsigned j = i; j < ITEMS_MAX - 1; j++)
							handle->cmds[j] = handle->cmds[j + 1];

						sync = true;
					}

					if(i == 0)
					{
						nk_spacing(ctx, 1);
					}
					else if(nk_button_label(ctx, "^")) // swap cmd with one above
					{
						const vm_command_t tmp = handle->cmds[i];
						handle->cmds[i] = handle->cmds[i - 1];
						handle->cmds[i - 1] = tmp;

						sync = true;
					}
				}

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
						_wheel_int(ctx, &i32);
						nk_property_int(ctx, nil_label, INT32_MIN, &i32, INT32_MAX, 1, 1.f);
						if(i32 != cmd->i32)
						{
							cmd->i32 = i32;
							sync = true;
						}
					} break;
					case COMMAND_FLOAT:
					{
						float f32 = cmd->f32;
						_wheel_float(ctx, &f32, stp);
						nk_property_float(ctx, nil_label, -HUGE, &f32, HUGE, stp, fpp);
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

						nk_spacing(ctx, 1);

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
				vm_graph_serialize(handle->api, &handle->forge, handle->cmds);
				props_impl_t *impl = _props_bsearch(&handle->props, handle->vm_graph);
				if(impl)
					_props_impl_set(&handle->props, impl, ser->atom->type, ser->atom->size, LV2_ATOM_BODY_CONST(ser->atom));

				_set_property(handle, handle->vm_graph);
			}

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Outputs", NK_WINDOW_TITLE | NK_WINDOW_BORDER))
		{
			bool sync = false;

			for(unsigned i = 0; i < CTRL_MAX; i++)
			{
				nk_layout_row_dynamic(ctx, dy*4, 1);
				_draw_plot(ctx, handle->outp[i].vals, handle->vm_plug);

				nk_layout_row_dynamic(ctx, dy, 2);
				if(  (handle->vm_plug == VM_PLUG_CONTROL)
					|| (handle->vm_plug == VM_PLUG_CV)
					|| (handle->vm_plug == VM_PLUG_ATOM)
					|| (handle->vm_plug == VM_PLUG_MIDI) )
				{
					nk_labelf(ctx, NK_TEXT_LEFT, "Out %u: %+f", i, handle->out0[i]);
				}
				else if(handle->vm_plug == VM_PLUG_AUDIO)
				{
					_draw_mixer(ctx, handle->out1[i]);

					if(handle->out2[i] > handle->out1[i])
						handle->out1[i] = handle->out2[i];
					else
						handle->out1[i] -= (handle->out1[i] - handle->out2[i]) * 0.1; //FIXME make dependent on ui:updateRate
				}

				const int old_window = handle->outp[i].window;
				_wheel_int(ctx, &handle->outp[i].window);
				nk_property_int(ctx, ms_label, 10, &handle->outp[i].window, 100000, 1, 1.f);
				if(old_window != handle->outp[i].window)
					memset(handle->outp[i].vals, 0x0, sizeof(float)*PLOT_MAX);

				if(handle->vm_plug == VM_PLUG_MIDI)
				{
					vm_filter_t *filter = &handle->destinationFilter[i];

					nk_layout_row_dynamic(ctx, dy, 3);

					int filter_type = filter->type;
					nk_combobox(ctx, filter_labels, FILTER_MAX, &filter_type,
						dy, nk_vec2(nk_widget_width(ctx), dy*14)); //FIXME
					if(filter->type != filter_type)
					{
						filter->type = filter_type;
						sync = true;
					}

					const int old_channel = filter->channel;
					filter->channel = nk_propertyi(ctx, chn_label, 0x0, old_channel, 0x7, 1, 1.f);
					if(old_channel != filter->channel)
						sync = true;

					switch(filter->type)
					{
						case FILTER_CONTROLLER:
						{
							const int old_value = filter->value;
							filter->value = nk_propertyi(ctx, val_label, 0x0, old_value, 0x7f, 1, 1.f);
							if(old_value != filter->value)
								sync = true;
						} break;

						case FILTER_BENDER:
						case FILTER_MAX:
						{
							// nothing
						} break;
					}
				}
			}

			if(sync)
			{
				atom_ser_t *ser = &handle->ser;
				ser->offset = 0;
				lv2_atom_forge_set_sink(&handle->forge, _sink, _deref, ser);
				vm_filter_serialize(&handle->forge, &handle->filt, handle->destinationFilter);
				props_impl_t *impl = _props_bsearch(&handle->props, handle->vm_destinationFilter);
				if(impl)
					_props_impl_set(&handle->props, impl, ser->atom->type, ser->atom->size, LV2_ATOM_BODY_CONST(ser->atom));

				_set_property(handle, handle->vm_destinationFilter);
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

	handle->vm_plug = vm_plug_type(plugin_uri);

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

	const int nprops = handle->vm_plug == VM_PLUG_MIDI
		? MAX_NPROPS
		: 1;

	if(!props_init(&handle->props, plugin_uri,
		defs, nprops,
		&handle->state, &handle->stash, handle->map, handle))
	{
		fprintf(stderr, "props_init failed\n");
		free(handle);
		return NULL;
	}

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);
	handle->vm_graph = handle->map->map(handle->map->handle, VM__graph);
	handle->vm_sourceFilter = handle->map->map(handle->map->handle, VM__sourceFilter);
	handle->vm_destinationFilter = handle->map->map(handle->map->handle, VM__destinationFilter);

	handle->filt.midi_Controller = handle->map->map(handle->map->handle, LV2_MIDI__Controller);
	handle->filt.midi_Bender = handle->map->map(handle->map->handle, LV2_MIDI__Bender);
	handle->filt.midi_channel = handle->map->map(handle->map->handle, LV2_MIDI__channel);
	handle->filt.midi_controllerNumber = handle->map->map(handle->map->handle, LV2_MIDI__controllerNumber);

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1280;
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
					{
						const unsigned j = idx->body - 2;
						handle->in0[j] = val->body;

						if(handle->vm_plug == VM_PLUG_AUDIO)
							handle->in2[j] = dBFS6(val->body);
					}
					else
					{
						const unsigned j = idx->body - 10;
						handle->out0[j] = val->body;

						if(handle->vm_plug == VM_PLUG_AUDIO)
							handle->out2[j] = dBFS6(val->body);
					}
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

static int
_resize(LV2UI_Handle instance, int width, int height)
{
	plughandle_t *handle = instance;

	return nk_pugl_resize(&handle->win, width, height);
}

static const LV2UI_Resize resize_ext = {
	.ui_resize = _resize
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__resize))
		return &resize_ext;

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
