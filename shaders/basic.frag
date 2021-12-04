#version 450

layout(set = 1, binding = 0) uniform sampler2D texture_sampler;

layout(location = 0) in vec3 frag_color; 
layout(location = 1) in vec2 frag_texture_coordinate;

layout(location = 0) out vec4 out_color;

void main() {
  out_color = vec4(frag_color, 1.0) * texture(texture_sampler, frag_texture_coordinate);
}
