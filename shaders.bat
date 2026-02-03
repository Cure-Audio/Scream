sokol-shdc -i src\shaders\knob.glsl -o src\shaders\knob.glsl.h -l hlsl5
sokol-shdc -i src\shaders\lfo.glsl -o src\shaders\lfo.glsl.h -l hlsl5
sokol-shdc -i modules\xvg\xvg_shaders.glsl -o src\shaders\xvg_shaders.glsl.h -l hlsl5

shader-hotreloader.exe -i src

cmd /k