@vs vs
in vec4 position;
in vec2 coord;
out vec2 uv;

void main() {
    gl_Position = position;
    uv = coord;
}
@end

@fs fs

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

void main() {
    // Circle (outer)
    float outer_radius = 1;
    float thickness = 0.005;
    float outer_circle = smoothstep(0, thickness, length(uv) - outer_radius);

    // Circle (inner)
    float inner_radius = 0.9;
    float inner_circle = smoothstep(0, thickness, length(uv) - inner_radius);

    // Angle
    float hyp = length(uv);
    float adj = uv.x;
    float angle_norm = acos_approx(hyp > 0 ? (adj / hyp) : 0);

    // Fan
    float fan_thickness = 0.02;
    float fan = mod(angle_norm * NUM_FANS, 2);
    fan = smoothstep(0.5 - fan_thickness, 0.5 + fan_thickness, abs(fan - 1));

    float a = (inner_circle - outer_circle) * fan;
    frag_color = vec4(a);
}

@end

@program knob vs fs