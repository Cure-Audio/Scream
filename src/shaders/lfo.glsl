@vs common_vs
in vec4 position;
in vec2 coord;
out vec2 uv;

void main() {
    gl_Position = position;
    uv = coord;
}
@end

@fs vertical_grad_fs

in vec2 uv;
out vec4 frag_color;

layout(binding=0) uniform lfo_vertical_grad_uniforms {
    vec4 colour1;
    vec4 colour2;
    vec4 colour_trail;
    float buffer_len;
};

struct lfo_buffer_item {
    float y;
};

layout(binding=0) readonly buffer lfo_line_storage_buffer {
    lfo_buffer_item lfo_y_buffer[];
};
layout(binding=1) readonly buffer lfo_trail_storage_buffer {
    lfo_buffer_item lfo_trail_buffer[];
};

vec4 src_over_blend(vec4 dst, vec4 src, float alpha)
{
    return src * alpha + dst * (1.0-alpha);
}

void main() {
    uint idx = uint(min(uv.x * buffer_len, buffer_len - 1));
    float lfo_y   = lfo_y_buffer[idx].y;
    float trail_y = lfo_trail_buffer[idx].y;

    // vertical gradient
    vec4 interp_col = mix(colour2, colour1, uv.y);
    // apply trail
    interp_col = src_over_blend(interp_col, colour_trail, trail_y);

    frag_color = uv.y < lfo_y ? interp_col : vec4(0);
}

@end

@program lfo_vertical_grad common_vs vertical_grad_fs