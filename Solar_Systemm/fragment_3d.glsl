#version 330 core


in vec3 frag_Color;
out vec4 FragColor;

void main(void) 
{
	FragColor = vec4 (frag_Color.r, frag_Color.g, frag_Color.b, 1.0);
}
