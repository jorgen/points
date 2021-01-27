#version 330
in vec3 position;
in float color;
out vec4 v_color;
uniform mat4 camera;
void main() {
    v_color = vec4(color, 0.0, 0.4, 1.0);
    gl_Position = camera * vec4( position, 1.0 );
}

