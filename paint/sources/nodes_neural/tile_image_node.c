
#include "../global.h"

void tile_image_node_button_on_next_frame(ui_node_t *node) {
	ui_node_t     *from_node = neural_from_node(node->inputs->buffer[0], 0);
	gpu_texture_t *input     = ui_nodes_get_node_preview_image(from_node);
	if (input != NULL) {
		char        *node_name = parser_material_node_name(node, NULL);
		ui_handle_t *h         = ui_handle(node_name);
		i32          model     = ui_nest(h, 0)->i;
		char        *dir       = neural_node_dir();
		iron_write_png(string("%s%sinput.png", dir, PATH_SEP), gpu_get_texture_pixels(input), input->width, input->height, 0);

		string_array_t *argv;
		if (model == 0) {
			argv = any_array_create_from_raw(
			    (void *[]){
			        string("%s/%s", dir, neural_node_sd_bin()),
			        "--diffusion-model",
			        string("%s/flux-2-klein-4b-Q8_0.gguf", dir),
			        "--vae",
			        // string("%s/full_encoder_small_decoder.safetensors", dir),
			        string("%s/flux_ae.safetensors", dir),
			        "--llm",
			        string("%s/Qwen3-4B-Q8_0.gguf", dir),
			        "--cfg-scale",
			        "1.0",
			        "--sampling-method",
			        "euler",
			        "--diffusion-fa",
			        "--offload-to-cpu",
			        "--steps",
			        "4",
			        "-s",
			        "-1",
			        "-W",
			        string("%d", g_config->neural_res),
			        "-H",
			        string("%d", g_config->neural_res),
			        "--circular",
			        "--strength",
			        "0.3",
			        "-p",
			        "",
			        "-i",
			        string("%s/input.png", dir),
			        "-o",
			        string("%s/output.png", dir),
			        NULL,
			    },
			    31);
		}

		iron_exec_async(argv->buffer[0], argv->buffer);
		sys_notify_on_update(neural_node_check_result, node);
	}
}

void tile_image_node_button(i32 node_id) {
	ui_node_canvas_t *canvas    = ui_nodes_get_canvas(true);
	ui_node_t        *node      = ui_get_node(canvas->nodes, node_id);
	char             *node_name = parser_material_node_name(node, NULL);
	ui_handle_t      *h         = ui_handle(node_name);
	string_array_t   *models    = any_array_create_from_raw(
        (void *[]){
            "FLUX 2 klein",
        },
        1);
	i32 model = ui_combo(ui_nest(h, 0), models, tr("Model"), false, UI_ALIGN_LEFT, true);

	if (neural_node_button(node, models->buffer[model])) {
		sys_notify_on_next_frame(&tile_image_node_button_on_next_frame, node);
	}
}

void tile_image_node_init() {

	ui_node_t *tile_image_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Tile Image"),
	                              .type   = "NEURAL_TILE_IMAGE",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = "tile_image_node_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 2}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_neural, tile_image_node_def);
	any_map_set(parser_material_node_vectors, "NEURAL_TILE_IMAGE", neural_node_vector);
	any_map_set(ui_nodes_custom_buttons, "tile_image_node_button", tile_image_node_button);
}
