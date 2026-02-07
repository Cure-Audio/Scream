sokol-shdc -i src\shaders\shaders.glsl -o src\shaders\shaders.glsl.h -l hlsl5
sokol-shdc -i modules\xvg\xvg_shaders.glsl -o src\shaders\xvg_shaders.glsl.h -l hlsl5

shader-hotreloader.exe -i src

cmd /k