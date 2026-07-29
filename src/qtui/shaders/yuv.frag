#version 440

// YUV 4:2:0 (I420) to RGB conversion shader
// Uses BT.601 limited-range coefficients matching SDL_PIXELFORMAT_IYUV
// (the same conversion SDL's internal renderer applies).
// Y plane: full width/height, packed as full bytes
// U, V planes: quarter width/height (4:2:0 subsampling)

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D texY;  // Y plane (full resolution)
layout(binding = 2) uniform sampler2D texU;  // U plane (quarter resolution)
layout(binding = 3) uniform sampler2D texV;  // V plane (quarter resolution)

void main()
{
    // Sample YUV from the three planes (all in R channel, normalized to [0,1])
    float y = texture(texY, v_uv).r;
    float u = texture(texU, v_uv).r;
    float v = texture(texV, v_uv).r;

    // Convert from limited-range YUV (BT.601) to RGB
    // Limited-range: Y in [16/255, 235/255], U/V in [16/255, 240/255]
    // Coefficients from BT.601: Kb = 0.114, Kr = 0.299

    // Scale Y from limited to full range [0,1]
    y = (y - 16.0/255.0) * (255.0/219.0);

    // U and V are centered at 128 (0.5 in normalized form)
    u = u - 0.5;
    v = v - 0.5;

    // BT.601 YUV to RGB conversion
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;

    fragColor = vec4(r, g, b, 1.0);
}
