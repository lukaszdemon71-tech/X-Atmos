#pragma once
// Compile-check stub. Not OpenGL. See ../README.md.
#define GL_QUADS 0x0007
typedef unsigned int GLenum;
typedef float GLfloat;
typedef int GLint;
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
void glBegin(GLenum);
void glEnd(void);
void glVertex2i(GLint, GLint);
