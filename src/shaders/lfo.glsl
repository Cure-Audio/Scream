@vs common_vs
layout(binding = 0) uniform vs_lfo_uniforms {
    vec2 topleft;
    vec2 bottomright;
    vec2 size;
};
out vec2 uv;

void main() {
    uint v_idx = gl_VertexIndex / 6u;
    uint i_idx = gl_VertexIndex - v_idx * 6;

    // Is odd
    bool is_right = (gl_VertexIndex & 1) == 1;
    bool is_bottom = i_idx >= 2 && i_idx <= 4;

    vec2 pos = vec2(
        is_right  ? bottomright.x : topleft.x,
        is_bottom ? bottomright.y : topleft.y
    );
    pos = (pos + pos) / size - vec2(1);
    pos.y = -pos.y;

    gl_Position = vec4(pos, 1, 1);
    uv = vec2(
        is_right  ? 1 : 0,
        is_bottom ? 0 : 1
    );
}
@end

@fs vertical_grad_fs

in vec2 uv;
out vec4 frag_colour;

layout(binding=1) uniform fs_lfo_uniforms {
    vec4 colour1;
    vec4 colour2;
    vec4 colour_trail;
    float buffer_len;
};

struct lfo_line_buffer_item {
    float y;
};
struct lfo_trail_buffer_item {
    float y;
};

layout(binding=0) readonly buffer lfo_line_storage_buffer {
    lfo_line_buffer_item lfo_y_buffer[];
};
layout(binding=1) readonly buffer lfo_trail_storage_buffer {
    lfo_trail_buffer_item lfo_trail_buffer[];
};

float fastsin(in float x)
{
    float norm = fract(x * 0.31831);
    norm = x > 0 ? norm : 1 - norm;
    float y = -norm * abs(norm) + norm;
    return 4 * y;
}

float dither_noise(in vec2 uv_coord){
    float noise = fract(fastsin(dot(uv_coord ,vec2(12.9898,78.233))) * 43758.5453);
    return (1.0 / 255.0) * noise - (0.5 / 255.0); // (-0.5 - 0.5) / 255 range. Shift 8bit colour +/- rgb value
}

vec4 src_over_blend(vec4 dst, vec4 src, float alpha)
{
    return src * alpha + dst * (1.0-alpha);
}

void main() {
    uint idx = uint(min(uv.x * buffer_len, buffer_len - 1));
    float lfo_y   = lfo_y_buffer[idx].y;
    float trail_y = lfo_trail_buffer[idx].y;
    float dither = dither_noise(uv);

    // vertical gradient
    vec4 interp_col = mix(colour2, colour1, uv.y);
    // apply trail
    interp_col = src_over_blend(interp_col, colour_trail, trail_y);
    interp_col.rgb += dither;

    frag_colour = uv.y < lfo_y ? interp_col : vec4(0);
}

@end

@program lfo_vertical_grad common_vs vertical_grad_fs