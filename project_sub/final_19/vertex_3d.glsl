#version 330 core

layout (location = 0) in vec3 in_Position; 
layout (location = 1) in vec3 in_Color;
out vec3 frag_Color;

uniform mat4 Matrix;

void main(void) 
{
	gl_Position = Matrix * vec4 (in_Position, 1.0);
	frag_Color = in_Color;
}