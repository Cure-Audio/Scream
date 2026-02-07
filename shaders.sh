#!/bin/sh
sokol-shdc -i src/shaders/shaders.glsl -o src/shaders/shaders.glsl.h -l metal_macos
sokol-shdc -i modules/xvg/xvg_shaders.glsl -o src/shaders/xvg_shaders.glsl.h -l metal_macos

shader-hotreloader -i src