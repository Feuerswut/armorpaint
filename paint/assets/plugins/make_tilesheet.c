#include "global.h"

void        *plugin;
ui_handle_t *h0;
ui_handle_t *h1;
ui_handle_t *h2;
ui_handle_t *h3;
ui_handle_t *h4;

void *tilesheet;
int   baking;
int   tile_size;
int   columns;
int   frames;

int frame;
int col;
int row;
int wait;
int skinning;
int reimported;

void on_update() {
	if (!baking) {
		return;
	}

	iron_delay_idle_sleep();

	if (frame < frames) {
		if (skinning && !reimported) {
			project_reimport_mesh_skinned(frame);
			reimported = 1;
			wait       = 2; // Wait for re-import and re-render
			return;
		}

		if (wait > 0) {
			wait = wait - 1;
			return;
		}

		float x = col * tile_size;
		float y = row * tile_size;
		viewport_capture_screenshot_to(tilesheet, x, y, tile_size, tile_size);

		frame = frame + 1;
		col   = col + 1;
		if (col >= columns) {
			col = 0;
			row = row + 1;
		}
		reimported = 0;
	}
	else {
		viewport_save_texture(tilesheet);
		context_t *c            = script_get_context();
		c->capturing_screenshot = false;
		c->ddirty               = 2;
		gc_unroot(tilesheet);
		tilesheet = NULL;
		baking    = 0;
	}
}

void on_ui() {
	if (ui_panel(h0, "Make Tilesheet", false, false, false)) {

		ui_slider(h1, "Tile Size", 0, 512, true, 1, true, UI_ALIGN_LEFT, true);
		ui_slider(h2, "Columns", 0, 64, true, 1, true, UI_ALIGN_LEFT, true);
		ui_slider(h3, "Frames", 0, 1024, true, 1, true, UI_ALIGN_LEFT, true);
		ui_check(h4, "Skinning", "");

		if (ui_button("Bake", UI_ALIGN_CENTER, "") && !baking) {
			tile_size = h1->f;
			columns   = h2->f;
			frames    = h3->f;
			skinning  = h4->b;

			// Square atlas
			int size  = tile_size * columns;
			tilesheet = gpu_create_render_target(size, size, GPU_TEXTURE_FORMAT_RGBA32);
			gc_root(tilesheet);

			frame                   = 0;
			col                     = 0;
			row                     = 0;
			wait                    = 2;
			reimported              = 0;
			baking                  = 1;
			context_t *c            = script_get_context();
			c->capturing_screenshot = true;
			c->ddirty               = 2;
		}
	}
}

void main() {
	plugin    = plugin_create();
	h0        = ui_handle_create();
	h1        = ui_handle_create();
	h1->f     = 256;
	h2        = ui_handle_create();
	h2->f     = 8;
	h3        = ui_handle_create();
	h3->f     = 64;
	h4        = ui_handle_create();
	h4->b     = 0;
	tilesheet = NULL;
	baking    = 0;
	gc_root(plugin);
	gc_root(h0);
	gc_root(h1);
	gc_root(h2);
	gc_root(h3);
	gc_root(h4);
	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
}
