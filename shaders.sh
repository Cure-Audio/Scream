#!/bin/sh
sokol-shdc -i src/shaders/nanovg_sokol.glsl -o src/shaders/nanovg_sokol.glsl.h -l metal_macos
sokol-shdc -i src/shaders/knob.glsl -o src/shaders/knob.glsl.h -l metal_macos
sokol-shdc -i src/shaders/texquad.glsl -o src/shaders/texquad.glsl.h -l metal_macos