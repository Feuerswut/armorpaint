
#include "global.h"

typedef struct {
	i32            frame;
	i32            layer_index;
	gpu_texture_t *texpaint;
	gpu_texture_t *texpaint_nor;
	gpu_texture_t *texpaint_pack;
} tab_timeline_keyframe_t;

typedef struct {
	i32            layer_index;
	gpu_texture_t *texpaint;
	gpu_texture_t *texpaint_nor;
	gpu_texture_t *texpaint_pack;
} tab_timeline_origin_t;

i32  tab_timeline_selected_frame = 0;
i32  tab_timeline_selected_row   = 0;
bool tab_timeline_playing        = false;
f64  tab_timeline_play_time      = 0.0;
i32  tab_timeline_scroll         = 0;
bool tab_timeline_scrolling      = false;
f32  tab_timeline_scroll_drag_x  = 0.0f;
i32  tab_timeline_scroll_drag_v  = 0;

static i32          tab_timeline_max_frames = 200;
static i32          tab_timeline_fps        = 24;
static any_array_t *tab_timeline_keyframes  = NULL;
static any_array_t *tab_timeline_origins    = NULL;
static i32          tab_timeline_last_frame = 0;

static i32 tab_timeline_pending_from     = -1;
static i32 tab_timeline_pending_to       = -1;
static i32 tab_timeline_pending_kf_frame = -1;
static i32 tab_timeline_pending_kf_layer = -1;
static i32 tab_timeline_pending_rm_frame = -1;
static i32 tab_timeline_pending_rm_layer = -1;

static void tab_timeline_copy_tex(gpu_texture_t *dst, gpu_texture_t *src) {
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(src, 0, 0, dst->width, dst->height);
	draw_set_pipeline(NULL);
	draw_end();
}

static gpu_texture_format_t tab_timeline_tex_format() {
	return base_bits_handle->i == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
	       : base_bits_handle->i == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
	                                                    : GPU_TEXTURE_FORMAT_RGBA128;
}

static i32 tab_timeline_find_keyframe(i32 frame, i32 layer_index) {
	for (i32 i = 0; i < tab_timeline_keyframes->length; i++) {
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[i];
		if (kf->frame == frame && kf->layer_index == layer_index) {
			return i;
		}
	}
	return -1;
}

static i32 tab_timeline_find_active_keyframe(i32 frame, i32 layer_index) {
	i32 best       = -1;
	i32 best_frame = -1;
	for (i32 i = 0; i < tab_timeline_keyframes->length; i++) {
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[i];
		if (kf->layer_index == layer_index && kf->frame <= frame && kf->frame > best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static i32 tab_timeline_find_origin(i32 layer_index) {
	for (i32 i = 0; i < tab_timeline_origins->length; i++) {
		if (((tab_timeline_origin_t *)tab_timeline_origins->buffer[i])->layer_index == layer_index) {
			return i;
		}
	}
	return -1;
}

static void tab_timeline_save_origins() {
	gpu_texture_format_t fmt = tab_timeline_tex_format();
	i32                  w   = config_get_texture_res_x();
	i32                  h   = config_get_texture_res_y();
	for (i32 li = 0; li < project_layers->length; li++) {
		slot_layer_t *l = project_layers->buffer[li];
		if (!slot_layer_is_layer(l)) {
			continue;
		}
		i32                    oi = tab_timeline_find_origin(li);
		tab_timeline_origin_t *o;
		if (oi < 0) {
			o                = GC_ALLOC_INIT(tab_timeline_origin_t, {0});
			o->layer_index   = li;
			o->texpaint      = gpu_create_render_target(w, h, fmt);
			o->texpaint_nor  = gpu_create_render_target(w, h, fmt);
			o->texpaint_pack = gpu_create_render_target(w, h, fmt);
			any_array_push(tab_timeline_origins, o);
		}
		else {
			o = tab_timeline_origins->buffer[oi];
		}
		tab_timeline_copy_tex(o->texpaint, l->texpaint);
		tab_timeline_copy_tex(o->texpaint_nor, l->texpaint_nor);
		tab_timeline_copy_tex(o->texpaint_pack, l->texpaint_pack);
	}
}

static void tab_timeline_load_origins() {
	for (i32 li = 0; li < project_layers->length; li++) {
		i32 oi = tab_timeline_find_origin(li);
		if (oi < 0) {
			continue;
		}
		slot_layer_t          *l = project_layers->buffer[li];
		tab_timeline_origin_t *o = tab_timeline_origins->buffer[oi];
		if (slot_layer_is_layer(l)) {
			tab_timeline_copy_tex(l->texpaint, o->texpaint);
			tab_timeline_copy_tex(l->texpaint_nor, o->texpaint_nor);
			tab_timeline_copy_tex(l->texpaint_pack, o->texpaint_pack);
		}
	}
	g_context->ddirty               = 2;
	g_context->layers_preview_dirty = true;
}

static void tab_timeline_save_to_keyframes(i32 frame) {
	for (i32 li = 0; li < project_layers->length; li++) {
		i32 kfi = tab_timeline_find_keyframe(frame, li);
		if (kfi < 0) {
			continue;
		}
		slot_layer_t            *l  = project_layers->buffer[li];
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[kfi];
		if (slot_layer_is_layer(l)) {
			tab_timeline_copy_tex(kf->texpaint, l->texpaint);
			tab_timeline_copy_tex(kf->texpaint_nor, l->texpaint_nor);
			tab_timeline_copy_tex(kf->texpaint_pack, l->texpaint_pack);
		}
	}
}

static void tab_timeline_load_from_keyframes(i32 frame) {
	bool any = false;
	for (i32 li = 0; li < project_layers->length; li++) {
		slot_layer_t *l = project_layers->buffer[li];
		if (!slot_layer_is_layer(l)) {
			continue;
		}
		i32 kfi = tab_timeline_find_active_keyframe(frame, li);
		if (kfi >= 0) {
			tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[kfi];
			tab_timeline_copy_tex(l->texpaint, kf->texpaint);
			tab_timeline_copy_tex(l->texpaint_nor, kf->texpaint_nor);
			tab_timeline_copy_tex(l->texpaint_pack, kf->texpaint_pack);
			any = true;
		}
		else {
			i32 oi = tab_timeline_find_origin(li);
			if (oi >= 0) {
				tab_timeline_origin_t *o = tab_timeline_origins->buffer[oi];
				tab_timeline_copy_tex(l->texpaint, o->texpaint);
				tab_timeline_copy_tex(l->texpaint_nor, o->texpaint_nor);
				tab_timeline_copy_tex(l->texpaint_pack, o->texpaint_pack);
				any = true;
			}
		}
	}
	if (any) {
		g_context->ddirty               = 2;
		g_context->layers_preview_dirty = true;
	}
}

static void tab_timeline_frame_change_on_next_frame(void *_) {
	i32 from                  = tab_timeline_pending_from;
	i32 to                    = tab_timeline_pending_to;
	tab_timeline_pending_from = -1;
	tab_timeline_pending_to   = -1;

	from == 0 ? tab_timeline_save_origins() : tab_timeline_save_to_keyframes(from);
	to == 0 ? tab_timeline_load_origins() : tab_timeline_load_from_keyframes(to);
}

static void tab_timeline_add_keyframe_on_next_frame(void *_) {
	i32 fr                        = tab_timeline_pending_kf_frame;
	i32 li                        = tab_timeline_pending_kf_layer;
	tab_timeline_pending_kf_frame = -1;
	tab_timeline_pending_kf_layer = -1;

	if (fr <= 0 || li < 0 || li >= project_layers->length) {
		return;
	}
	slot_layer_t *l = project_layers->buffer[li];
	if (!slot_layer_is_layer(l)) {
		return;
	}
	gpu_texture_format_t     fmt = tab_timeline_tex_format();
	i32                      w   = config_get_texture_res_x();
	i32                      h   = config_get_texture_res_y();
	i32                      kfi = tab_timeline_find_keyframe(fr, li);
	tab_timeline_keyframe_t *kf;
	if (kfi < 0) {
		kf                = GC_ALLOC_INIT(tab_timeline_keyframe_t, {0});
		kf->frame         = fr;
		kf->layer_index   = li;
		kf->texpaint      = gpu_create_render_target(w, h, fmt);
		kf->texpaint_nor  = gpu_create_render_target(w, h, fmt);
		kf->texpaint_pack = gpu_create_render_target(w, h, fmt);
		any_array_push(tab_timeline_keyframes, kf);
	}
	else {
		kf = tab_timeline_keyframes->buffer[kfi];
	}
	tab_timeline_copy_tex(kf->texpaint, l->texpaint);
	tab_timeline_copy_tex(kf->texpaint_nor, l->texpaint_nor);
	tab_timeline_copy_tex(kf->texpaint_pack, l->texpaint_pack);
}

static void tab_timeline_remove_keyframe_on_next_frame(void *_) {
	i32 fr                        = tab_timeline_pending_rm_frame;
	i32 li                        = tab_timeline_pending_rm_layer;
	tab_timeline_pending_rm_frame = -1;
	tab_timeline_pending_rm_layer = -1;

	i32 kfi = tab_timeline_find_keyframe(fr, li);
	if (kfi < 0) {
		return;
	}
	array_remove(tab_timeline_keyframes, tab_timeline_keyframes->buffer[kfi]);
	if (fr == tab_timeline_last_frame) {
		tab_timeline_load_from_keyframes(fr);
	}
}

static void tab_timeline_clear_on_next_frame(void *_) {
	gc_unroot(tab_timeline_keyframes);
	tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_keyframes);
	tab_timeline_load_origins();
	tab_timeline_last_frame = 0;
}

static void tab_timeline_init() {
	if (tab_timeline_keyframes != NULL) {
		return;
	}
	tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_keyframes);
	tab_timeline_origins = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_origins);
}

void tab_timeline_draw_frame_context_menu() {
	bool has_kf = tab_timeline_keyframes != NULL && tab_timeline_selected_frame > 0 &&
	              tab_timeline_find_keyframe(tab_timeline_selected_frame, tab_timeline_selected_row) >= 0;
	ui->enabled = has_kf;
	if (ui_menu_button(tr("Remove Keyframe"), "", ICON_MINUS)) {
		tab_timeline_pending_rm_frame = tab_timeline_selected_frame;
		tab_timeline_pending_rm_layer = tab_timeline_selected_row;
		sys_notify_on_next_frame(&tab_timeline_remove_keyframe_on_next_frame, NULL);
	}
	ui->enabled = true;
}

void tab_timeline_draw_edit() {
	ui_menu_align();
	ui_handle_t *hfps = ui_handle(__ID__);
	hfps->f           = (f32)tab_timeline_fps;
	ui_slider(hfps, tr("Framerate"), 1, 60, false, 1, true, UI_ALIGN_RIGHT, true);
	if (hfps->changed) {
		tab_timeline_fps  = (i32)hfps->f;
		ui_menu_keep_open = true;
	}

	ui_menu_align();
	ui_handle_t *hframes = ui_handle(__ID__);
	hframes->f           = (f32)tab_timeline_max_frames;
	ui_slider(hframes, tr("Frame Count"), 1, 200, false, 1, true, UI_ALIGN_RIGHT, true);
	if (hframes->changed) {
		tab_timeline_max_frames = (i32)hframes->f;
		ui_menu_keep_open       = true;
	}

	if (ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		sys_notify_on_next_frame(&tab_timeline_clear_on_next_frame, NULL);
	}
}

void tab_timeline_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Timeline"), false, -1, false) && ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		tab_timeline_init();

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw((f32[]){-100, -100, -40, -40, -40, -40, -60}, 7);
		ui_row(row);
		if (ui_icon_button(tr("Keyframe"), ICON_PLUS, UI_ALIGN_CENTER)) {
			i32 li = tab_timeline_selected_row;
			if (tab_timeline_selected_frame > 0 && li < project_layers->length && slot_layer_is_layer(project_layers->buffer[li])) {
				tab_timeline_pending_kf_frame = tab_timeline_selected_frame;
				tab_timeline_pending_kf_layer = li;
				sys_notify_on_next_frame(&tab_timeline_add_keyframe_on_next_frame, NULL);
			}
		}
		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_timeline_draw_edit, -1, -1);
		}

		if (tab_timeline_playing) {
			if (ui_icon_button(tr("Pause"), ICON_PAUSE, UI_ALIGN_CENTER)) {
				tab_timeline_playing = false;
			}
		}
		else {
			if (ui_icon_button(tr("Play"), ICON_PLAY, UI_ALIGN_CENTER)) {
				tab_timeline_playing   = true;
				tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_fps;
			}
		}
		if (ui_icon_button(tr("Stop"), ICON_STOP, UI_ALIGN_CENTER)) {
			tab_timeline_playing        = false;
			tab_timeline_selected_frame = 0;
		}
		if (ui_icon_button(tr("Previous"), ICON_CHEVRON_LEFT, UI_ALIGN_CENTER)) {
			if (tab_timeline_selected_frame > 0) {
				tab_timeline_selected_frame--;
				tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_fps;
			}
		}
		if (ui_icon_button(tr("Next"), ICON_CHEVRON_RIGHT, UI_ALIGN_CENTER)) {
			if (tab_timeline_selected_frame < tab_timeline_max_frames - 1) {
				tab_timeline_selected_frame++;
				tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_fps;
			}
		}
		ui->enabled = false;
		ui_text(i32_to_string(tab_timeline_selected_frame), UI_ALIGN_CENTER, 0x00000000);
		ui->enabled = true;
		ui_end_sticky();

		f32 layer_name_w    = 100.0f * UI_SCALE();
		f32 frame_w         = 16.0f * UI_SCALE();
		f32 start_x         = ui->_x + layer_name_w;
		f32 start_y         = ui->_y;
		i32 font_h          = draw_font_height(ui->ops->font, ui->font_size);
		i32 strip_h         = (i32)(ui->ops->theme->ELEMENT_H * UI_SCALE());
		f32 track_w         = ui->_window_w - start_x;
		i32 visible         = (i32)(track_w / frame_w);
		i32 max_scroll      = math_max(tab_timeline_max_frames - visible, 0);
		tab_timeline_scroll = (i32)math_min(math_max(tab_timeline_scroll, 0), max_scroll);

		if (tab_timeline_playing) {
			iron_delay_idle_sleep();
			f64 elapsed                                     = sys_time() - tab_timeline_play_time;
			tab_timeline_selected_frame                     = (i32)(elapsed * tab_timeline_fps) % tab_timeline_max_frames;
			ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
		}

		// Frame number labels every 5 frames
		draw_set_color(ui->ops->theme->LABEL_COL);
		i32 label_start = (tab_timeline_scroll / 5) * 5;
		for (i32 i = label_start; i < tab_timeline_scroll + visible + 1 && i < tab_timeline_max_frames; i += 5) {
			f32   lx      = start_x + (i - tab_timeline_scroll) * frame_w;
			char *label   = i32_to_string(i);
			f32   label_w = draw_string_width(ui->ops->font, ui->font_size, label);
			draw_string(label, lx + (frame_w - label_w) / 2.0f, start_y);
		}

		u32 base_col   = ui->ops->theme->BUTTON_COL;
		u32 bright_col = base_col + 0x00101010;
		u32 sel_col    = ui->ops->theme->HIGHLIGHT_COL;
		i32 row_count  = project_layers->length;

		gpu_texture_t *icons     = resource_get("icons.k");
		f32            icon_size = strip_h - 2;

		for (i32 ri = 0; ri < row_count; ri++) {
			slot_layer_t *layer  = project_layers->buffer[ri];
			f32           row_y  = start_y + font_h + 2 + ri * strip_h;
			f32           icon_y = row_y + (strip_h - icon_size) / 2.0f;

			rect_t *rect = resource_tile50(icons, ICON_LAYERS);
			draw_set_color(ui->ops->theme->LABEL_COL);
			draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, ui->_x, icon_y, icon_size, icon_size);
			draw_set_color(ui->ops->theme->LABEL_COL);
			draw_string(layer->name, ui->_x + icon_size + 2, row_y + (strip_h - font_h) / 2.0f);

			for (i32 i = tab_timeline_scroll; i < tab_timeline_scroll + visible + 1 && i < tab_timeline_max_frames; i++) {
				f32  x        = start_x + (i - tab_timeline_scroll) * frame_w;
				bool selected = i == tab_timeline_selected_frame && ri == tab_timeline_selected_row;
				u32  col      = selected ? sel_col : (i % 5 == 0) ? bright_col : base_col;

				draw_set_color(col);
				draw_filled_rect(x, row_y, frame_w - 1, strip_h - 1);

				if (i == 0 || (tab_timeline_keyframes != NULL && tab_timeline_find_keyframe(i, ri) >= 0)) {
					draw_set_color(ui->ops->theme->LABEL_COL);
					draw_filled_circle(x + frame_w / 2.0f, row_y + strip_h / 2.0f, 3.0f * UI_SCALE(), 12);
				}

				bool in_cell = !tab_timeline_scrolling && ui->input_x > ui->_window_x + x && ui->input_x < ui->_window_x + x + frame_w &&
				               ui->input_y > ui->_window_y + row_y && ui->input_y < ui->_window_y + row_y + strip_h;
				if (in_cell && ui->input_down) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					tab_timeline_play_time      = sys_time() - (f64)i / tab_timeline_fps;
				}
				if (in_cell && ui->input_released_r) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					ui_menu_draw(&tab_timeline_draw_frame_context_menu, -1, -1);
				}
			}
		}

		// Scrollbar
		f32 scrollbar_h = 8.0f * UI_SCALE();
		f32 scrollbar_y = start_y + font_h + 2 + row_count * strip_h;
		f32 handle_w    = track_w * (f32)visible / tab_timeline_max_frames;
		f32 handle_x    = start_x + (max_scroll > 0 ? tab_timeline_scroll * (track_w - handle_w) / max_scroll : 0);

		draw_set_color(base_darker(ui->ops->theme->BUTTON_COL, 0x00101010));
		draw_filled_rect(start_x, scrollbar_y, track_w, scrollbar_h);
		draw_set_color(ui->ops->theme->BUTTON_COL + 0x00202020);
		draw_filled_rect(handle_x, scrollbar_y, handle_w, scrollbar_h);

		if (ui->input_started && ui->input_x > ui->_window_x + start_x && ui->input_x < ui->_window_x + start_x + track_w &&
		    ui->input_y > ui->_window_y + scrollbar_y && ui->input_y < ui->_window_y + scrollbar_y + scrollbar_h) {
			tab_timeline_scrolling     = true;
			tab_timeline_scroll_drag_x = ui->input_x;
			tab_timeline_scroll_drag_v = tab_timeline_scroll;
		}
		if (ui->input_released) {
			tab_timeline_scrolling = false;
		}
		if (tab_timeline_scrolling && ui->input_down && max_scroll > 0) {
			f32 delta           = ui->input_x - tab_timeline_scroll_drag_x;
			tab_timeline_scroll = (i32)(tab_timeline_scroll_drag_v + delta * max_scroll / (track_w - handle_w));
			tab_timeline_scroll = (i32)math_min(math_max(tab_timeline_scroll, 0), max_scroll);
		}

		// Select frame
		if (tab_timeline_selected_frame != tab_timeline_last_frame) {
			tab_timeline_pending_from = tab_timeline_last_frame;
			tab_timeline_pending_to   = tab_timeline_selected_frame;
			tab_timeline_last_frame   = tab_timeline_selected_frame;
			sys_notify_on_next_frame(&tab_timeline_frame_change_on_next_frame, NULL);
		}

		draw_set_color(0xffffffff);
		ui->_y = scrollbar_y + scrollbar_h + 2;
	}
}
