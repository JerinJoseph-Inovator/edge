#ifndef EDGE_OPENGL_RENDERER_H
#define EDGE_OPENGL_RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

// Called once when surface is created
void initGL();

// Called when surface is resized
void resizeGL(int width, int height);

// Called every frame to draw
void renderGL();

#ifdef __cplusplus
}
#endif

#endif //EDGE_OPENGL_RENDERER_H
