@vs knob_vs
in vec4 position;
in vec2 coord;
out vec2 uv;

void main() {
    gl_Position = position;
    uv = coord;
}
@end

@fs knob_fs

in vec2 uv;
out vec4 frag_color;

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
    float fan_feather = 0.02;
    float fan = mod(angle_norm * NUM_FANS, 2);
    fan = smoothstep(0.5 - fan_feather, 0.5 + fan_feather, abs(fan - 1));

    // Arc
    float rad = PI * (5.0 / 6.0);
    // vec2 cs = vec2(-0.5, 0.866025);
    vec2 cs = vec2(cos(rad), sin(rad));
    float ring_width = 0.08;
    float d = sdRing(uv, cs, 1 - ring_width / 2, ring_width);
    float ring_feather = 0.002;
    float arc = 1 - smoothstep(-ring_feather, ring_feather, d);

    float a = arc * fan;
    vec3 col = vec3(a);
    col *= vec3(0.7098039215686275, 0.7450980392156863, 0.7803921568627451); // apply colour

    frag_color = vec4(col, a);
}

@end

@program knob knob_vs knob_fs