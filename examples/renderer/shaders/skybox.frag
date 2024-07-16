#version 330
in vec3 direction;
out vec4 o_color;

uniform samplerCube skybox;

void main() {
    o_color = texture(skybox, direction);
}
