
#include "../global.h"

vec4_t raycast_aabb(object_t *object, f32 mouse_x, f32 mouse_y) {
	return raycast_box_intersect(object->transform, mouse_x, mouse_y, scene_camera);
}
