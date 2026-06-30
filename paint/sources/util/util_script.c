
#include "../global.h"

void script_set_stage(char *name) {
	if (g_project->stages == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];
		if (string_equals(s->name, name)) {
			tab_stages_selected = i;
			tab_stages_apply(s);
			return;
		}
	}
}

char *script_get_stage() {
	stage_t *s = tab_stages_get_stage();
	return s != NULL ? s->name : NULL;
}

void script_set_tilesheet_anim(object_t *o, char *anim) {
	mesh_object_t *mo = (mesh_object_t *)o->ext;

	// Locate the material slot
	i32 slot_index = tab_meshes_get_override(mo);
	if (slot_index < 0) {
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			if (g_project->_->materials->buffer[i]->data == mo->material) {
				slot_index = i;
				break;
			}
		}
	}

	slot_material_t *slot = g_project->_->materials->buffer[slot_index];

	// Locate the tilesheet animation node
	for (i32 i = 0; i < slot->canvas->nodes->length; ++i) {
		ui_node_t *node = slot->canvas->nodes->buffer[i];
		if (!string_equals(node->type, "TILESHEET_ANIM")) {
			continue;
		}

		ui_node_button_t *enum_but = node->buttons->buffer[4];
		string_array_t   *names    = string_split(u8_array_to_string(enum_but->data), "\n");
		for (i32 j = 0; j < (i32)names->length; ++j) {
			if (!string_equals(names->buffer[j], anim)) {
				continue;
			}

			enum_but->default_value->buffer[0] = (f32)j;
			make_material_parse_paint_material(true);

			// Material override
			for (i32 k = 0; k < g_project->_->paint_objects->length; ++k) {
				mesh_object_t *po = g_project->_->paint_objects->buffer[k];
				if (tab_meshes_get_override(po) == slot_index) {
					tab_meshes_set_override(po, slot_index);
					g_context->ddirty = 2;
				}
			}
			return;
		}
	}
}

static transform_t *_script_tween_transform = NULL;

static void script_tween_done(void) {
	_script_tween_transform = NULL;
}

static void script_tween_tick(void) {
	_script_tween_transform->dirty = true;
}

void script_tween_to(object_t *o, vec4_t to, f32 speed) {
	if (_script_tween_transform != NULL) {
		return;
	}

	transform_t *t          = o->transform;
	_script_tween_transform = t;
	f32    duration         = vec4_dist(t->loc, to) / speed;
	ease_t ease             = EASE_LINEAR;
	tween_to(GC_ALLOC_INIT(tween_anim_t,
	                       {.target = &t->loc.x, .to = to.x, .duration = duration, .ease = ease, .tick = script_tween_tick, .done = script_tween_done}));
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &t->loc.y, .to = to.y, .duration = duration, .ease = ease}));
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &t->loc.z, .to = to.z, .duration = duration, .ease = ease}));
}

static f32   _script_fade_opacity = 0.0f;
static char *_script_fade_stage   = NULL;

static void script_fade_draw(void *_) {
	draw_begin(NULL, false, 0);
	draw_set_color((u32)(_script_fade_opacity * 255.0f) << 24);
	draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
	draw_end();
}

static void script_fade_out_done(void *_) {
	sys_remove_update(script_fade_draw);
	gc_unroot(_script_fade_stage);
	_script_fade_stage = NULL;
}

static void script_fade_in_done(void *_) {
	script_set_stage(_script_fade_stage);
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 0.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_out_done}));
}

void script_fade_to_stage(char *stage) {
	if (_script_fade_stage != NULL) {
		return; // Fade in progress
	}
	_script_fade_stage = string_copy(stage);
	gc_root(_script_fade_stage);

	_script_fade_opacity = 0.0f;
	sys_notify_on_update(script_fade_draw, NULL);

	// Fade to black, set the stage, then fade back in
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 1.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_in_done}));
}
