// Mock WebGL2 context: lets three.js' WebGLRenderer run under Node far enough to build programs. Every
// gl.shaderSource() is recorded, so the exact GLSL three generates for each material (incl. onBeforeCompile patches,
// defines, light / shadow counts) can be captured and compiled natively.
const names = new Map(); let next = 0x8000;
const constId = (n) => { if (!names.has(n)) names.set(n, next++); return names.get(n); };
const nameOf = new Map();
const K = (n) => { const id = constId(n); nameOf.set(id, n); return id; };

export function createMockGL(canvas) {
  const shaders = new Map(); let sid = 1;
  const programs = new Map(); let pid = 1;
  const captured = [];
  const params = {
    VERSION: 'WebGL 2.0 (mock)', SHADING_LANGUAGE_VERSION: 'WebGL GLSL ES 3.00 (mock)', VENDOR: 'mock', RENDERER: 'mock',
    MAX_TEXTURE_IMAGE_UNITS: 32, MAX_VERTEX_TEXTURE_IMAGE_UNITS: 32, MAX_TEXTURE_SIZE: 16384, MAX_CUBE_MAP_TEXTURE_SIZE: 16384,
    MAX_VERTEX_ATTRIBS: 16, MAX_VERTEX_UNIFORM_VECTORS: 4096, MAX_VARYING_VECTORS: 30, MAX_FRAGMENT_UNIFORM_VECTORS: 4096,
    MAX_SAMPLES: 4, MAX_COMBINED_TEXTURE_IMAGE_UNITS: 64, MAX_3D_TEXTURE_SIZE: 2048, MAX_ARRAY_TEXTURE_LAYERS: 2048,
    MAX_DRAW_BUFFERS: 8, MAX_COLOR_ATTACHMENTS: 8, MAX_RENDERBUFFER_SIZE: 16384, MAX_UNIFORM_BLOCK_SIZE: 65536,
    MAX_VIEWPORT_DIMS: new Int32Array([16384, 16384]), SCISSOR_BOX: new Int32Array([0, 0, 1600, 900]), VIEWPORT: new Int32Array([0, 0, 1600, 900]),
    MAX_TEXTURE_MAX_ANISOTROPY_EXT: 16, MAX_CLIENT_WAIT_TIMEOUT_WEBGL: 0, CURRENT_PROGRAM: null,
  };
  const handler = {
    get(t, k) {
      if (k in t) return t[k];
      if (typeof k === 'string' && /^[A-Z0-9_]+$/.test(k)) return K(k);
      return () => undefined; // any other gl method: no-op
    },
  };
  const gl = new Proxy({
    canvas, drawingBufferWidth: 1600, drawingBufferHeight: 900, drawingBufferColorSpace: 'srgb', unpackColorSpace: 'srgb',
    getContextAttributes: () => ({ alpha: true, antialias: false, depth: true, stencil: false, premultipliedAlpha: true, preserveDrawingBuffer: false, powerPreference: 'high-performance', failIfMajorPerformanceCaveat: false }),
    isContextLost: () => false,
    getExtension: (n) => ({ name: n, MAX_TEXTURE_MAX_ANISOTROPY_EXT: K('MAX_TEXTURE_MAX_ANISOTROPY_EXT'), TEXTURE_MAX_ANISOTROPY_EXT: K('TEXTURE_MAX_ANISOTROPY_EXT'),
      clipControlEXT: () => {}, LOWER_LEFT_EXT: K('LOWER_LEFT_EXT'), ZERO_TO_ONE_EXT: K('ZERO_TO_ONE_EXT'), NEGATIVE_ONE_TO_ONE_EXT: K('NEGATIVE_ONE_TO_ONE_EXT'), UPPER_LEFT_EXT: K('UPPER_LEFT_EXT') }),
    getSupportedExtensions: () => ['EXT_color_buffer_float', 'EXT_texture_filter_anisotropic', 'EXT_clip_control', 'OES_texture_float_linear'],
    getParameter: (id) => { const n = nameOf.get(id); return n in params ? params[n] : 0; },
    getShaderPrecisionFormat: () => ({ precision: 23, rangeMin: 127, rangeMax: 127 }),
    createShader: (type) => { const s = { id: sid++, type: nameOf.get(type) }; shaders.set(s.id, s); return s; },
    shaderSource: (s, src) => { s.src = src; },
    compileShader: () => {},
    getShaderParameter: () => true, getShaderInfoLog: () => '', getShaderSource: (s) => s.src,
    createProgram: () => { const p = { id: pid++, sh: [] }; programs.set(p.id, p); return p; },
    attachShader: (p, s) => { p.sh.push(s); },
    linkProgram: (p) => { captured.push({ vs: p.sh.find(s => s.type === 'VERTEX_SHADER')?.src, fs: p.sh.find(s => s.type === 'FRAGMENT_SHADER')?.src, prog: p }); },
    getProgramParameter: (p, id) => { const n = nameOf.get(id); if (n === 'LINK_STATUS') return true; if (n === 'ACTIVE_UNIFORMS' || n === 'ACTIVE_ATTRIBUTES') return 0; return true; },
    getProgramInfoLog: () => '', getUniformLocation: () => ({}), getAttribLocation: () => -1, getUniformBlockIndex: () => 0,
    createBuffer: () => ({}), createTexture: () => ({}), createFramebuffer: () => ({}), createRenderbuffer: () => ({}), createVertexArray: () => ({}), createQuery: () => ({}), createSampler: () => ({}),
    checkFramebufferStatus: () => K('FRAMEBUFFER_COMPLETE'), getError: () => 0, fenceSync: () => ({}), clientWaitSync: () => K('ALREADY_SIGNALED'), getSyncParameter: () => K('SIGNALED'),
    getActiveUniform: () => null, getActiveAttrib: () => null,
  }, handler);
  gl._captured = captured;
  return gl;
}
