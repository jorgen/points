#version 330
in vec3 position;
in vec3 color;
out vec4 v_color;
uniform mat4 camera;
void main() {
    v_color = vec4(color, 1.0);
    gl_Position = camera * vec4( position.x, position.z, position.y, 1.0 );
}

