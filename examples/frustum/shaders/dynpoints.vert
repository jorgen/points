#version 330
in vec3 position;
uniform mat4 camera;
void main() {
    gl_Position = camera * vec4(position.xyz, 1.0 );
}

