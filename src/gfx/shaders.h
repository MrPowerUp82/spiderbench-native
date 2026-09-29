// GLSL 330 sources. COMMON is prepended to every lit fragment shader (sky model, fog, cascaded shadows, PBR-lite).
#pragma once

namespace glsl {

inline const char* COMMON = R"GLSL(
uniform vec3 uSunDir;      // towards the sun
uniform vec3 uSunColor;
uniform vec3 uSkyZenith, uSkyHorizon, uGroundAmb;
uniform vec3 uCamPos;
uniform float uFogDensity;
uniform sampler2DArrayShadow uShadow;
uniform mat4 uShadowMat[3];
uniform vec3 uCascadeEnd;  // view-space far distance of each cascade
uniform float uTime;
uniform int uDebug;        // 1 = show the shadow term, 2 = n.l

vec3 skyColor(vec3 d) {
  float h = max(d.y, 0.0);
  vec3 c = mix(uSkyHorizon, uSkyZenith, pow(h, 0.45));
  float below = clamp(-d.y * 6.0, 0.0, 1.0);
  c = mix(c, uSkyHorizon * 0.82, below);
  float sd = max(dot(d, uSunDir), 0.0);
  c += uSunColor * (pow(sd, 8.0) * 0.12 + pow(sd, 64.0) * 0.35);
  return c;
}
vec3 applyFog(vec3 col, vec3 wp) {
  vec3 v = wp - uCamPos; float dist = length(v); vec3 d = v / max(dist, 1e-3);
  // height fog: denser near the water / street level, thinning with altitude
  float hf = exp(-max(min(wp.y, uCamPos.y), 0.0) * 0.0035);
  float f = 1.0 - exp(-dist * uFogDensity * (0.35 + 0.65 * hf));
  vec3 fc = skyColor(normalize(vec3(d.x, max(d.y, 0.02), d.z)));
  fc += uSunColor * pow(max(dot(d, uSunDir), 0.0), 6.0) * 0.08;
  return mix(col, fc, clamp(f, 0.0, 1.0));
}
float shadowAt(vec3 wp, vec3 n, float viewDepth) {
  int c = viewDepth < uCascadeEnd.x ? 0 : (viewDepth < uCascadeEnd.y ? 1 : 2);
  if (viewDepth > uCascadeEnd.z) return 1.0;
  float texel = c == 0 ? 0.02 : (c == 1 ? 0.07 : 0.3);
  vec3 p = wp + n * texel * 1.5;
  vec4 sp = uShadowMat[c] * vec4(p, 1.0);
  vec3 q = sp.xyz / sp.w * 0.5 + 0.5;
  if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z > 1.0) return 1.0;
  float bias = c == 0 ? 0.0006 : (c == 1 ? 0.0009 : 0.0015);
  float s = 0.0; vec2 ts = vec2(1.0 / 2048.0);
  for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)
    s += texture(uShadow, vec4(q.xy + vec2(x, y) * ts, float(c), q.z - bias));
  s /= 9.0;
  float fade = smoothstep(uCascadeEnd.z * 0.85, uCascadeEnd.z, viewDepth);
  return mix(s, 1.0, fade);
}
// GGX specular + lambert diffuse, hemispheric ambient + sky reflection
vec3 shade(vec3 albedo, vec3 n, vec3 wp, float rough, float metal, float ao, float shadow) {
  vec3 v = normalize(uCamPos - wp);
  vec3 l = uSunDir; vec3 h = normalize(l + v);
  float ndl = max(dot(n, l), 0.0), ndv = max(dot(n, v), 1e-3), ndh = max(dot(n, h), 0.0);
  if (uDebug == 1) return vec3(shadow);
  if (uDebug == 2) return vec3(ndl);
  float a = rough * rough, a2 = a * a;
  float dd = ndh * ndh * (a2 - 1.0) + 1.0; float D = a2 / (3.14159 * dd * dd);
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  float Gv = ndv / (ndv * (1.0 - k) + k), Gl = ndl / (ndl * (1.0 - k) + k);
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
  vec3 spec = D * Gv * Gl * F / max(4.0 * ndv * ndl, 1e-3);
  vec3 kd = (1.0 - F) * (1.0 - metal);
  vec3 direct = (kd * albedo / 3.14159 + spec) * uSunColor * ndl * shadow * 3.14159;
  vec3 amb = mix(uGroundAmb, uSkyZenith * 0.75 + uSkyHorizon * 0.2, n.y * 0.5 + 0.5);
  vec3 Fv = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - ndv, 5.0);
  vec3 refl = skyColor(reflect(-v, n)) * Fv * (1.0 - rough * 0.85) * mix(0.35, 1.0, shadow);
  return direct + (amb * albedo * (1.0 - metal) * kd + refl) * ao;
}
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
)GLSL";

// ------------------------------------------------------------------------------------------------ city (uber shader)
inline const char* WORLD_VS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUv;
layout(location=3) in vec3 aTint; layout(location=4) in vec4 aMat;
uniform mat4 uViewProj; uniform mat4 uView;
out vec3 vWP; out vec3 vN; out vec2 vUv; out vec3 vTint; flat out vec4 vMat; out float vDepth;
void main() {
  vWP = aPos; vN = aNrm; vUv = aUv; vTint = aTint; vMat = aMat;
  vDepth = -(uView * vec4(aPos, 1.0)).z;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

inline const char* WORLD_FS = R"GLSL(
in vec3 vWP; in vec3 vN; in vec2 vUv; in vec3 vTint; flat in vec4 vMat; in float vDepth;
uniform sampler2D tAsphalt, tSidewalk, tGrass, tNoise, tBark;
layout(location=0) out vec4 oColor;

vec3 facade(inout vec3 n, out float rough, out float metal, out float ao, out vec3 emissive) {
  float style = floor(vMat.y), jit = fract(vMat.y) * 2.0;
  float floorH = vMat.z, bayW = vMat.w;
  vec2 g = vec2(vUv.x / bayW, vUv.y / floorH);
  vec2 cell = fract(g), id = floor(g);
  float noise = texture(tNoise, vUv * 0.13).r;
  vec3 wall = vTint * (0.82 + 0.3 * noise);
  float winW = 0.5, winH = 0.6, frame = 0.0;
  if (style < 0.5) { winW = 0.94; winH = 0.78; }            // glass curtain wall
  else if (style < 1.5) { winW = 0.42; winH = 0.56; wall *= 0.9 + 0.12 * step(0.5, fract(vUv.y * 13.0 + step(0.5, fract(vUv.x * 2.4)) * 0.5)); } // brick coursing
  else if (style < 2.5) { winW = 0.46; winH = 0.6; }        // limestone
  else if (style < 3.5) { winW = 0.96; winH = 0.46; }       // concrete ribbon windows
  else { winW = 0.38; winH = 0.66; }                        // art-deco piers
  bool shop = vUv.y < floorH * 1.25 && vUv.y > 0.2;
  if (shop) { winW = 0.86; winH = 0.72; }
  float cy = shop ? 0.5 : 0.56;
  vec2 dw = abs(cell - vec2(0.5, cy)) - vec2(winW, winH) * 0.5;
  float inWin = step(max(dw.x, dw.y), 0.0);
  float edge = smoothstep(0.0, 0.05, -max(dw.x, dw.y));
  rough = 0.85; metal = 0.0; ao = 1.0; emissive = vec3(0.0);
  vec3 col = wall;
  // horizontal floor bands / cornice lines on masonry
  if (style > 0.5 && !shop) { float band = step(fract(g.y), 0.06); col *= 1.0 - band * 0.12; }
  if (inWin > 0.5) {
    float h = hash12(id + vec2(jit * 17.0, style * 3.0));
    vec3 glass = style < 0.5 ? mix(vec3(0.12, 0.17, 0.2), vTint * 0.35, 0.5) : vec3(0.07, 0.08, 0.09);
    // interior: blinds / dark rooms / an occasional lit room
    vec3 interior = mix(vec3(0.05), vec3(0.28, 0.25, 0.2), step(0.7, h)) * (0.6 + 0.4 * step(cell.y, cy + winH * (h - 0.3)));
    col = mix(glass, interior, style < 0.5 ? 0.15 : 0.45);
    rough = style < 0.5 ? 0.06 : 0.12; metal = 0.0;
    ao = mix(0.55, 1.0, edge);
    // mullions
    if (style < 0.5) { float m = step(abs(fract(g.x * 2.0) - 0.5), 0.47); col = mix(vTint * 0.25, col, m); }
  } else {
    // window reveals: dark rim just outside the opening (depth cue)
    float rim = 1.0 - smoothstep(0.0, 0.035, max(dw.x, dw.y));
    ao = 1.0 - rim * 0.45;
  }
  return col;
}

vec3 road(out float rough) {
  vec2 p = vWP.xz;
  vec3 c = texture(tAsphalt, p / 6.0).rgb * 0.88;
  c = mix(c, texture(tAsphalt, vec2(p.y, -p.x) / 7.3 + 0.37).rgb * 0.88, 0.35);
  c *= 0.8 + 0.35 * texture(tNoise, p / 64.0).r;
  rough = 0.9;
  float axis = vMat.y, centre = vMat.z, halfW = vMat.w;
  if (axis < 1.5) {
    float across = axis < 0.5 ? vWP.x - centre : vWP.z - centre;
    float along = axis < 0.5 ? vWP.z : vWP.x;
    float a = abs(across);
    vec3 paint = vec3(0.0); float m = 0.0;
    if (axis < 0.5) { // avenue: double yellow + dashed lane lines
      if (a > 0.1 && a < 0.25) { paint = vec3(0.85, 0.66, 0.15); m = 1.0; }
      for (int i = 1; i <= 2; i++) if (abs(a - 3.6 * float(i)) < 0.08 && fract(along / 9.0) < 0.35) { paint = vec3(0.85); m = 1.0; }
    } else if (halfW > 6.0) {
      if (a < 0.12) { paint = vec3(0.85, 0.66, 0.15); m = 1.0; }
    } else if (a < 0.08 && fract(along / 9.0) < 0.35) { paint = vec3(0.8); m = 0.8; }
    // crosswalk stripes at the block ends of streets / avenue edges
    c = mix(c, paint * (0.75 + 0.25 * texture(tNoise, p / 3.0).r), m * 0.85);
    if (m > 0.0) rough = 0.6;
  }
  return c;
}

void main() {
  float mat = vMat.x;
  vec3 n = normalize(vN);
  vec3 albedo; float rough = 0.85, metal = 0.0, ao = 1.0; vec3 emissive = vec3(0.0);
  if (mat < 0.5) albedo = facade(n, rough, metal, ao, emissive);
  else if (mat < 1.5) { // roof
    float nz = texture(tNoise, vWP.xz * 0.11).r;
    albedo = vec3(0.34, 0.33, 0.32) * (0.75 + 0.45 * nz); rough = 0.95;
  }
  else if (mat < 2.5) albedo = road(rough);
  else if (mat < 3.5) { albedo = texture(tSidewalk, vWP.xz / 3.0).rgb * 0.95; rough = 0.8; }
  else if (mat < 4.5) { albedo = texture(tGrass, vWP.xz / 4.0).rgb * (0.75 + 0.4 * texture(tNoise, vWP.xz / 40.0).r); rough = 0.95; }
  else if (mat < 5.5) { albedo = vec3(0.62, 0.6, 0.57) * vTint * (0.85 + 0.25 * texture(tNoise, vUv * 0.5).r); rough = 0.85; }
  else if (mat < 6.5) { albedo = texture(tBark, vec2(vUv.x * 0.6, vUv.y * 0.35)).rgb; rough = 0.95; }
  else if (mat < 7.5) { // leaves
    float nz = texture(tNoise, vWP.xz * 0.35 + vWP.y * 0.2).r;
    albedo = vTint * (0.65 + 0.55 * nz); rough = 0.8; ao = 0.75 + 0.25 * n.y;
  }
  else if (mat < 8.5) { albedo = vTint * (0.8 + 0.2 * step(0.5, fract(vUv.x * 3.0))); rough = 0.9; }
  else if (mat < 9.5) { albedo = vTint; rough = 0.45; metal = 0.5; }
  else if (mat < 10.5) { albedo = vec3(0.1, 0.13, 0.16); rough = 0.05; }
  else { albedo = vec3(0.26, 0.28, 0.24) * (0.8 + 0.3 * texture(tNoise, vWP.xz / 90.0).r); rough = 0.95; }
  float sh = shadowAt(vWP, n, vDepth);
  vec3 col = shade(albedo, n, vWP, rough, metal, ao, sh) + emissive;
  oColor = vec4(applyFog(col, vWP), 1.0);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ shadow depth
inline const char* DEPTH_VS = R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)GLSL";
inline const char* DEPTH_SKIN_VS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=3) in uvec4 aJoints; layout(location=4) in vec4 aWeights;
uniform mat4 uViewProj; uniform mat4 uBones[64];
void main() {
  mat4 sk = uBones[aJoints.x] * aWeights.x + uBones[aJoints.y] * aWeights.y + uBones[aJoints.z] * aWeights.z + uBones[aJoints.w] * aWeights.w;
  gl_Position = uViewProj * (sk * vec4(aPos, 1.0));
}
)GLSL";
inline const char* DEPTH_FS = R"GLSL(
void main() {}
)GLSL";

// ------------------------------------------------------------------------------------------------ skinned character
inline const char* SKIN_VS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUv;
layout(location=3) in uvec4 aJoints; layout(location=4) in vec4 aWeights;
uniform mat4 uViewProj; uniform mat4 uView; uniform mat4 uBones[64];
out vec3 vWP; out vec3 vN; out vec2 vUv; out float vDepth;
void main() {
  mat4 sk = uBones[aJoints.x] * aWeights.x + uBones[aJoints.y] * aWeights.y + uBones[aJoints.z] * aWeights.z + uBones[aJoints.w] * aWeights.w;
  vec4 wp = sk * vec4(aPos, 1.0);
  vWP = wp.xyz; vN = mat3(sk) * aNrm; vUv = aUv;
  vDepth = -(uView * wp).z;
  gl_Position = uViewProj * wp;
}
)GLSL";
inline const char* SKIN_FS = R"GLSL(
in vec3 vWP; in vec3 vN; in vec2 vUv; in float vDepth;
uniform sampler2D tBase, tOrm, tNormal;
uniform vec4 uBaseColor; uniform float uRough, uMetal; uniform int uHasTex;
layout(location=0) out vec4 oColor;
// cotangent frame (no tangent attribute needed)
vec3 perturb(vec3 n, vec3 p, vec2 uv, vec3 map) {
  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  vec3 dp2p = cross(dp2, n), dp1p = cross(n, dp1);
  vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;
  float inv = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
  return normalize(mat3(T * inv, B * inv, n) * map);
}
void main() {
  vec3 n = normalize(vN);
  if (!gl_FrontFacing) n = -n;
  vec3 albedo = uBaseColor.rgb; float rough = uRough, metal = uMetal, ao = 1.0;
  if (uHasTex == 1) {
    albedo *= texture(tBase, vUv).rgb;
    vec3 orm = texture(tOrm, vUv).rgb; ao = orm.r; rough *= orm.g; metal *= orm.b;
    vec3 m = texture(tNormal, vUv).xyz * 2.0 - 1.0;
    n = perturb(n, vWP, vUv, m);
  }
  float sh = shadowAt(vWP, n, vDepth);
  vec3 col = shade(albedo, n, vWP, clamp(rough, 0.04, 1.0), metal, ao, sh);
  // rim light: keeps the silhouette readable against dark facades
  vec3 v = normalize(uCamPos - vWP);
  col += uSkyZenith * pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.25;
  oColor = vec4(applyFog(col, vWP), 1.0);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ sky (fullscreen)
inline const char* FS_TRI_VS = R"GLSL(
out vec2 vUv;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)GLSL";
inline const char* SKY_FS = R"GLSL(
in vec2 vUv; uniform mat4 uInvViewProj;
layout(location=0) out vec4 oColor;
void main() {
  vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, 0.5, 1.0);
  vec3 d = normalize(p.xyz / p.w - uCamPos);
  vec3 c = skyColor(d);
  float sd = dot(d, uSunDir);
  c += uSunColor * smoothstep(0.9996, 0.99985, sd) * 12.0; // sun disc
  // thin procedural cloud band
  if (d.y > 0.0) {
    vec2 uv = d.xz / (d.y + 0.08) * 0.6 + uTime * 0.004;
    float n = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { n += a * (sin(uv.x * 3.1 + sin(uv.y * 2.3)) * 0.5 + 0.5) * (sin(uv.y * 2.7 + sin(uv.x * 1.7)) * 0.5 + 0.5); uv *= 2.13; a *= 0.5; }
    float cl = smoothstep(0.42, 0.75, n) * smoothstep(0.0, 0.25, d.y) * 0.55;
    c = mix(c, vec3(1.0, 0.98, 0.95) * (uSunColor * 0.35 + uSkyHorizon * 0.8), cl);
  }
  oColor = vec4(c, 1.0);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ water
inline const char* WATER_VS = R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj; uniform mat4 uView;
out vec3 vWP; out float vDepth;
void main() { vWP = aPos; vDepth = -(uView * vec4(aPos, 1.0)).z; gl_Position = uViewProj * vec4(aPos, 1.0); }
)GLSL";
inline const char* WATER_FS = R"GLSL(
in vec3 vWP; in float vDepth;
uniform sampler2D tWaterN;
layout(location=0) out vec4 oColor;
void main() {
  vec2 p = vWP.xz;
  vec3 n1 = texture(tWaterN, p / 23.0 + vec2(uTime * 0.012, uTime * 0.007)).xzy * 2.0 - 1.0;
  vec3 n2 = texture(tWaterN, p / 57.0 - vec2(uTime * 0.006, -uTime * 0.01)).xzy * 2.0 - 1.0;
  vec3 n = normalize(vec3(0.0, 1.0, 0.0) + (n1 + n2) * vec3(0.18, 0.0, 0.18));
  vec3 v = normalize(uCamPos - vWP);
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  vec3 refl = skyColor(reflect(-v, n));
  vec3 deep = vec3(0.03, 0.07, 0.08);
  float sh = shadowAt(vWP, n, vDepth);
  vec3 h = normalize(uSunDir + v);
  vec3 spec = uSunColor * pow(max(dot(n, h), 0.0), 400.0) * 6.0 * sh;
  vec3 col = mix(deep + uSkyHorizon * 0.05, refl, fres) + spec;
  oColor = vec4(applyFog(col, vWP), 1.0);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ web ribbons
inline const char* WEB_VS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol;
uniform mat4 uViewProj;
out vec4 vCol; out vec3 vWP;
void main() { vCol = aCol; vWP = aPos; gl_Position = uViewProj * vec4(aPos, 1.0); }
)GLSL";
inline const char* WEB_FS = R"GLSL(
in vec4 vCol; in vec3 vWP;
layout(location=0) out vec4 oColor;
void main() {
  vec3 c = vec3(0.92, 0.93, 0.95) * (0.55 + 0.45 * max(uSunDir.y, 0.0)) + uSkyZenith * 0.3;
  oColor = vec4(applyFog(c * vCol.rgb, vWP), vCol.a);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ post
inline const char* BRIGHT_FS = R"GLSL(
in vec2 vUv; uniform sampler2D tSrc; uniform vec2 uTexel; uniform float uThreshold;
layout(location=0) out vec4 oColor;
void main() {
  vec3 c = vec3(0.0);
  c += texture(tSrc, vUv + uTexel * vec2(-1, -1)).rgb; c += texture(tSrc, vUv + uTexel * vec2(1, -1)).rgb;
  c += texture(tSrc, vUv + uTexel * vec2(-1, 1)).rgb; c += texture(tSrc, vUv + uTexel * vec2(1, 1)).rgb;
  c *= 0.25;
  if (uThreshold > 0.0) { float l = max(max(c.r, c.g), c.b); c *= max(l - uThreshold, 0.0) / max(l, 1e-4); }
  oColor = vec4(c, 1.0);
}
)GLSL";
inline const char* UPSAMPLE_FS = R"GLSL(
in vec2 vUv; uniform sampler2D tSrc; uniform vec2 uTexel;
layout(location=0) out vec4 oColor;
void main() {
  vec3 c = texture(tSrc, vUv).rgb * 4.0;
  c += texture(tSrc, vUv + vec2(uTexel.x, 0)).rgb * 2.0 + texture(tSrc, vUv - vec2(uTexel.x, 0)).rgb * 2.0;
  c += texture(tSrc, vUv + vec2(0, uTexel.y)).rgb * 2.0 + texture(tSrc, vUv - vec2(0, uTexel.y)).rgb * 2.0;
  c += texture(tSrc, vUv + uTexel).rgb + texture(tSrc, vUv - uTexel).rgb + texture(tSrc, vUv + vec2(uTexel.x, -uTexel.y)).rgb + texture(tSrc, vUv + vec2(-uTexel.x, uTexel.y)).rgb;
  oColor = vec4(c / 16.0, 1.0);
}
)GLSL";
inline const char* COMPOSITE_FS = R"GLSL(
in vec2 vUv;
uniform sampler2D tScene, tBloom;
uniform float uExposure, uBloom, uBlur;
uniform vec2 uTexel;
layout(location=0) out vec4 oColor;
vec3 aces(vec3 x) { const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14; return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0); }
vec3 sceneAt(vec2 uv) {
  // speed blur: radial streaks from the screen centre, weighted toward the edges
  if (uBlur <= 0.001) return texture(tScene, uv).rgb;
  vec2 dir = (uv - 0.5); float k = uBlur * 0.045 * smoothstep(0.08, 0.7, length(dir) * 1.6);
  vec3 c = vec3(0.0);
  for (int i = 0; i < 8; i++) c += texture(tScene, uv - dir * k * float(i) / 7.0).rgb;
  return c / 8.0;
}
void main() {
  // FXAA-lite: blend along the local luma edge
  vec3 cM = sceneAt(vUv);
  float lM = dot(cM, vec3(0.299, 0.587, 0.114));
  float lN = dot(texture(tScene, vUv + vec2(0, uTexel.y)).rgb, vec3(0.299, 0.587, 0.114));
  float lS = dot(texture(tScene, vUv - vec2(0, uTexel.y)).rgb, vec3(0.299, 0.587, 0.114));
  float lE = dot(texture(tScene, vUv + vec2(uTexel.x, 0)).rgb, vec3(0.299, 0.587, 0.114));
  float lW = dot(texture(tScene, vUv - vec2(uTexel.x, 0)).rgb, vec3(0.299, 0.587, 0.114));
  float range = max(max(max(lN, lS), max(lE, lW)), lM) - min(min(min(lN, lS), min(lE, lW)), lM);
  if (range > max(0.05, lM * 0.15)) {
    vec2 dir = vec2(-((lN + lS) - (lE + lW)), (lN + lE) - (lS + lW));
    dir = normalize(dir + 1e-5) * uTexel;
    cM = 0.5 * (texture(tScene, vUv + dir * 0.5).rgb + texture(tScene, vUv - dir * 0.5).rgb);
  }
  vec3 c = cM + texture(tBloom, vUv).rgb * uBloom;
  c = aces(c * uExposure);
  float vig = smoothstep(1.25, 0.35, length(vUv - 0.5) * 1.35);
  c *= mix(0.78, 1.0, vig);
  c = pow(c, vec3(1.0 / 2.2));
  oColor = vec4(c, 1.0);
}
)GLSL";

// ------------------------------------------------------------------------------------------------ HUD
inline const char* UI_VS = R"GLSL(
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUv; out vec4 vCol;
void main() { vUv = aUv; vCol = aCol; gl_Position = vec4(aPos / uScreen * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0); }
)GLSL";
inline const char* UI_FS = R"GLSL(
in vec2 vUv; in vec4 vCol; uniform sampler2D tFont;
layout(location=0) out vec4 oColor;
void main() {
  float a = vUv.x < 0.0 ? 1.0 : texture(tFont, vUv).a;
  oColor = vec4(vCol.rgb, vCol.a * a);
}
)GLSL";

}  // namespace glsl
