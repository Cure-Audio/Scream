// Dual filter blur. Marius Bjorge - Bandwith-Efficient Rendering, Siggraph 2015
// https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf

@vs fullscreen_triangle_vs
const vec2 positions[3] = { vec2(-1, -1), vec2(3, -1), vec2(-1, 3), };

out vec2 uv;

void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0, 1);
    uv = (pos * vec2(1, -1) + 1) * 0.5;
}
@end

@fs texread_fs
layout(binding=0)uniform texture2D tex;
layout(binding=0)uniform sampler smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv);
}
@end

@fs lightfilter_fs
layout(binding=0)uniform texture2D tex;
layout(binding=0)uniform sampler smp;
layout(binding=0) uniform fs_lightfilter {
    float u_threshold;
};

in vec2 uv;
out vec4 frag_color;

// https://en.wikipedia.org/wiki/Relative_luminance
float get_luminance(vec3 col)
{
    return 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
}

void main() {
    vec4 col = texture(sampler2D(tex, smp), uv);
    float l = get_luminance(col.rgb);
    if (l < u_threshold)
    {
        col.rgb = vec3(0);
    }
    frag_color = col;
}
@end

@fs kawase_blur_fs
in vec2 uv;
out vec4 frag_colour;
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=0) uniform fs_kawase_blur {
    vec2 u_offset;
};

void main() {
    frag_colour = texture(sampler2D(tex, smp), vec2(uv.x + u_offset.x, uv.y + u_offset.y));
    frag_colour += texture(sampler2D(tex, smp), vec2(uv.x - u_offset.x, uv.y + u_offset.y));
    frag_colour += texture(sampler2D(tex, smp), vec2(uv.x + u_offset.x, uv.y - u_offset.y));
    frag_colour += texture(sampler2D(tex, smp), vec2(uv.x - u_offset.x, uv.y - u_offset.y));
    frag_colour /= vec4(4);
}
@end

@fs downsample_fs
in vec2 uv;
out vec4 frag_colour;
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=0) uniform fs_downsample {
    vec2 u_offset;
};

vec4 downsample(vec2 uv, vec2 halfpixel)
{
    vec4 sum = texture(sampler2D(tex, smp), uv) * 4.0;
    sum += texture(sampler2D(tex, smp), uv - halfpixel.xy);
    sum += texture(sampler2D(tex, smp), uv + halfpixel.xy);
    sum += texture(sampler2D(tex, smp), uv + vec2(halfpixel.x, -halfpixel.y));
    sum += texture(sampler2D(tex, smp), uv - vec2(halfpixel.x, -halfpixel.y));
    return sum / 8.0;
}

void main() {
    frag_colour = downsample(uv, u_offset);
}
@end

@fs upsample_fs
in vec2 uv;
out vec4 frag_colour;
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=0) uniform fs_upsample {
    vec2 u_halfpixel;
};

vec4 upsample(vec2 uv, vec2 halfpixel)
{
    vec4 sum = texture(sampler2D(tex, smp), uv + vec2(-halfpixel.x * 2.0, 0.0));
    sum += texture(sampler2D(tex, smp), uv + vec2(-halfpixel.x, halfpixel.y)) * 2.0;
    sum += texture(sampler2D(tex, smp), uv + vec2(0.0, halfpixel.y * 2.0));
    sum += texture(sampler2D(tex, smp), uv + vec2(halfpixel.x, halfpixel.y)) * 2.0;
    sum += texture(sampler2D(tex, smp), uv + vec2(halfpixel.x * 2.0, 0.0));
    sum += texture(sampler2D(tex, smp), uv + vec2(halfpixel.x, -halfpixel.y)) * 2.0;
    sum += texture(sampler2D(tex, smp), uv + vec2(0.0, -halfpixel.y * 2.0));
    sum += texture(sampler2D(tex, smp), uv + vec2(-halfpixel.x, -halfpixel.y)) * 2.0;
    return sum / 12.0;
}

void main() {
    frag_colour = upsample(uv, u_halfpixel);
}
@end

@fs bloom_fs
in vec2 uv;
out vec4 frag_colour;
layout(binding=0) uniform texture2D tex_wet;
layout(binding=1) uniform texture2D tex_dry;
layout(binding=0) uniform sampler smp;
layout(binding=0) uniform fs_bloom {
    float u_amount;
};

void main() {
    vec4 wet = texture(sampler2D(tex_wet, smp), uv);
    vec4 dry = texture(sampler2D(tex_dry, smp), uv);

    frag_colour = dry + wet * u_amount;
}
@end

@program lightfilter fullscreen_triangle_vs lightfilter_fs
@program texread fullscreen_triangle_vs texread_fs
@program kawase_blur fullscreen_triangle_vs kawase_blur_fs
@program downsample fullscreen_triangle_vs downsample_fs
@program upsample fullscreen_triangle_vs upsample_fs
@program bloom fullscreen_triangle_vs bloom_fs