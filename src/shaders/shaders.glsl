@vs bg_vs
precision mediump float;
layout(binding = 0) uniform vs_bg_uniforms {
    vec2 u_size;
    float u_padding_x;
    float u_height_header;
    float u_height_footer;
    float u_border_radius;
    vec2  u_radial_gradient_radius;
    float u_radial_gradient_y;

    int u_bg_colour_top;
    int u_bg_colour_bottom;
    int u_bg_colour_content;
};

const vec2 positions[3] = { vec2(-1, -1), vec2(3, -1), vec2(-1, 3), };
out vec2 uv;
out flat float px_scale;
out flat float padding_x_scale; // 16
out flat float height_header_scale; 
out flat float height_footer_scale;
out flat float border_radius;
out flat float feather;         // 32
out flat float blur_radius;
out flat float radial_gradient_y;
out flat vec2  radial_gradient_radius;  // 48

out flat uint bg_colour_top;
out flat uint bg_colour_bottom;
out flat uint bg_colour_content; // 60

void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0, 1);
    uv = (pos * vec2(1, -1) + 1) * 0.5;

    px_scale = u_size.x / u_size.y;
    padding_x_scale = 1 - 2 * (u_padding_x / u_size.x);
    height_header_scale = 1 - 2 * (u_height_header / u_size.y);
    height_footer_scale = 1 - 2 * (u_height_footer / u_size.y);
    border_radius = 2 * u_border_radius / u_size.y;
    feather = 4.0 / u_size.y;
    blur_radius = 2 * 4 / u_size.y;

    radial_gradient_y = u_radial_gradient_y / u_size.y;
    radial_gradient_radius = u_size / u_radial_gradient_radius;

    bg_colour_top     = u_bg_colour_top;
    bg_colour_bottom  = u_bg_colour_bottom;
    bg_colour_content = u_bg_colour_content;
}
@end

@fs bg_fs
precision mediump float;
in vec2 uv;
in flat float px_scale;
in flat float padding_x_scale;
in flat float height_header_scale;
in flat float height_footer_scale;
in flat float border_radius;
in flat float feather;
in flat float blur_radius;
in flat float radial_gradient_y;
in flat vec2  radial_gradient_radius;

in flat uint bg_colour_top;
in flat uint bg_colour_bottom;
in flat uint bg_colour_content;

out vec4 frag_colour;

// b.x = half width
// b.y = half height
// r.x = roundness top-right  
// r.y = roundness boottom-right
// r.z = roundness top-left
// r.w = roundness bottom-left
float sdRoundBox(in vec2 p, in vec2 b, in vec4 r)
{
    r.xy = (p.x>0.0)?r.xy : r.zw;
    r.x  = (p.y>0.0)?r.x  : r.y;
    vec2 q = abs(p)-b+r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}

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

vec4 src_over_blend(vec4 dst, vec4 src)
{
    vec3 rgb = src.rgb * src.a + dst.rgb * (1.0-src.a);
    return vec4(rgb, 1);
}

void main()
{
    // Content box
    vec2 p = uv * 2 - 1;
    p.y = -p.y;

    vec2 p_scale = vec2(px_scale, 1);
    vec2 half_wh = p_scale;
    half_wh.x *= padding_x_scale;
    half_wh.y *= p.y > 0 ? height_header_scale : height_footer_scale;
    float d_box = sdRoundBox(p * p_scale, half_wh, vec4(border_radius));
    float mask_content_box = smoothstep(feather, 0, d_box + feather * 0.5);

    // Inner shadow TOP
    float inner_shadow_top = smoothstep(blur_radius * 4, 0, blur_radius * 2 + p.y - height_header_scale);
    inner_shadow_top = 1 - (inner_shadow_top * 0.8745); // alpha value
    // Inner shadow BOTTOM
    float inner_shadow_bot = smoothstep(blur_radius * 4, 0, blur_radius * 2 + -p.y - height_footer_scale);
    inner_shadow_bot = 0.5 - (inner_shadow_bot * 0.5); // alpha value
    // Radial gradient lighting
    vec2 ellipse = (uv - vec2(0.5, radial_gradient_y)) * radial_gradient_radius;
    float lighting = 0.5 * clamp(1 - length(ellipse), 0.0, 1.0); // alpha value

    // Blending
    vec4 col_bg = mix(unpackUnorm4x8(bg_colour_top), unpackUnorm4x8(bg_colour_bottom), uv.y).abgr;
    vec4 col_content = unpackUnorm4x8(bg_colour_content).abgr;
    col_content = src_over_blend(col_content, vec4(1, 1, 1, inner_shadow_top));
    col_content = src_over_blend(col_content, vec4(0, 0, 0, inner_shadow_bot));
    col_content = src_over_blend(col_content, vec4(1, 1, 1, lighting));
    col_content.a = mask_content_box;

    frag_colour = src_over_blend(col_bg, col_content);
    frag_colour.rgb += dither_noise(uv);
}

@end

@program bg bg_vs bg_fs

@vs knob_vs
layout(binding = 0) uniform vs_knob_uniforms {
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
        is_right  ? 1 : -1,
        is_bottom ? -1 : 1
    );
}
@end

@fs knob_fs

layout(binding = 1) uniform fs_knob_uniforms {
    vec4 u_colour;
    float fan_feather;
    float ring_feather;
};

in vec2 uv;
out vec4 frag_colour;

const float PI = 3.14159265;
const float NUM_FANS = 92;

// https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
// acos(x) / PI;
// out [0,1]
float acos_approx(float inX)
{
    // When used in fans, this tends to produce fans of unequal width
    // polynomial degree 1
    // float C0 = 1.56467;
    // float C1 = -0.155972;
    // float x = abs(inX);
    // float res = C1 * x + C0; // p(x)
    // res *= sqrt(1.0f - x);

    // float approx = (inX >= 0) ? res : PI - res; // Undo range reduction

    // This is good enough
    // polynomial degree 2
    float C0 = 1.57018 / PI;
    float C1 = -0.201877 / PI;
    float C2 = 0.0464619 / PI;
    float x = abs(inX);
    float res = (C2 * x + C1) * x + C0; // p(x)
    res *= sqrt(1.0f - x);

    float approx = (inX >= 0) ? res : 1 - res; // Undo range reduction

    return approx;
}

// https://iquilezles.org/articles/distfunctions2d/
float sdRing( in vec2 p, in vec2 n, in float r, float th )
{
    p.x = abs(p.x);
    p = mat2x2(n.x,n.y,-n.y,n.x)*p;
    return max( abs(length(p)-r)-th*0.5,
                length(vec2(p.x,max(0.0,abs(r-p.y)-th*0.5)))*sign(p.x) );
}

void main() {
    // Angle
    float hyp = length(uv);
    float adj = uv.x;
    float angle_norm = acos_approx(hyp > 0 ? (adj / hyp) : 0);

    // Fan
    float fan = mod(angle_norm * NUM_FANS, 2);
    fan = smoothstep(0.5 - fan_feather, 0.5 + fan_feather, abs(fan - 1));

    // Arc
    float rad = PI * (5.0 / 6.0);
    // vec2 cs = vec2(-0.5, 0.866025);
    vec2 cs = vec2(cos(rad), sin(rad));
    float ring_width = 0.08;
    float d = sdRing(uv, cs, 1 - ring_width / 2, ring_width);
    float arc = 1 - smoothstep(-ring_feather, ring_feather, d);

    float a = arc * fan;

    frag_colour = vec4(u_colour.rgb, a);
}

@end

@vs logo_vs
layout(binding = 0) uniform vs_logo_uniforms {
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
        is_bottom ? 1 : 0
    );
}
@end


@fs logo_fs
layout(binding=0)uniform texture2D logo_tex;
layout(binding=0)uniform sampler logo_smp;

layout(binding = 1) uniform fs_logo_uniforms {
    vec4 u_col0;
    vec4 u_col1;
};

in vec2 uv;
out vec4 frag_col;

void main() {
    vec4 col = mix(u_col0, u_col1, uv.y);
    col.a = texture(sampler2D(logo_tex, logo_smp), uv).a;
    frag_col = col;
}
@end

@program knob knob_vs knob_fs
@program logo logo_vs logo_fs

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