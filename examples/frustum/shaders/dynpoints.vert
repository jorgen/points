#version 330
in vec3 rgb;
in vec3 position;
out vec4 v_color; 
uniform mat4 camera;
void main() {
    v_color = vec4(rgb, 1.0);
    gl_Position = camera * vec4(position.xyz, 1.0 );
}

