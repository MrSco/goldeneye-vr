// Swift's view of port/visionos (VosApp.swift): the one call it makes.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* vos_app.mm: the immersive space's layer renderer, unretained */
void gevrVosLayerReady(void *layerRenderer);

#ifdef __cplusplus
}
#endif
