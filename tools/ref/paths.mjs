// Output locations of the bake. One bake per original quality preset (SB_QUALITY=low|med|high feeds the remake's
// ?q= switch through stubs.mjs): the default med bake goes to build/city-bake + build/refshaders.
import path from 'node:path';

export const QUALITY = process.env.SB_QUALITY || '';
const suffix = QUALITY && QUALITY !== 'med' ? '-' + QUALITY : '';
export const BAKE = path.resolve(process.env.SB_BAKE_DIR || 'build/city-bake' + suffix);
export const SHADERS = path.resolve(process.env.SB_SHADER_DIR || 'build/refshaders' + suffix);
