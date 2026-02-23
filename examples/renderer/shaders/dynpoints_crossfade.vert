#version 330
in vec3 position;
in vec3 rgb;       // new color (mono arrives as (val,0,0))
in vec3 old_rgb;   // old color (mono arrives as (val,0,0))
out vec4 v_color;
uniform mat4 camera;
uniform float point_scale;
uniform vec4 params; // x=fade_alpha, y=blend, z=old_is_mono, w=new_is_mono
void main() {
    vec3 new_c = (params.w > 0.5) ? vec3(rgb.x) : rgb;
    vec3 old_c = (params.z > 0.5) ? vec3(old_rgb.x) : old_rgb;
    vec3 color = mix(old_c, new_c, params.y);
    v_color = vec4(color, params.x);
    gl_Position = camera * vec4(position.xyz, 1.0);
    gl_PointSize = clamp(point_scale / gl_Position.w, 1.0, 64.0);
}
