import type { GlobScanOptions } from "bun";
const { validateObject, validateString, validateFunction, validateArray, validateBoolean } = require("internal/validators");
const { sep } = require("node:path");

const isWindows = process.platform === "win32";

interface GlobOptions {
  /** @default process.cwd() */
  cwd?: string;
  exclude?: ((ent: any) => boolean) | string[];
  /**
   * Should glob return paths as {@link Dirent} objects. `false` for strings.
   * @default false */
  withFileTypes?: boolean;
  followSymlinks?: boolean;
}

function makeDirentMatcher(patterns: string[], exclude: any, excludeGlobs: any[] | null) {
  const globs = patterns.map(p => new Bun.Glob(p));
  return { globs, exclude, excludeGlobs };
}

function* walkWithFileTypesSync(
  fs: any,
  pathMod: any,
  baseCwd: string,
  relDir: string,
  globs: any[],
  exclude: any,
  excludeGlobs: any[] | null,
  followSymlinks: boolean,
): Generator<any> {
  const fullDir = relDir ? pathMod.join(baseCwd, relDir) : baseCwd;
  let entries: any[];
  try {
    entries = fs.readdirSync(fullDir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const dirent of entries) {
    const relPath = relDir ? `${relDir}${sep}${dirent.name}` : dirent.name;
    if (typeof exclude === "function") {
      if (exclude(dirent)) continue;
    } else if (excludeGlobs) {
      if (excludeGlobs.some((g: any) => g.match(relPath))) continue;
    }
    if (globs.some(g => g.match(relPath))) {
      yield dirent;
    }
    if (dirent.isDirectory() || (followSymlinks && dirent.isSymbolicLink())) {
      yield* walkWithFileTypesSync(fs, pathMod, baseCwd, relPath, globs, exclude, excludeGlobs, followSymlinks);
    }
  }
}

async function* walkWithFileTypes(
  fs: any,
  pathMod: any,
  baseCwd: string,
  relDir: string,
  globs: any[],
  exclude: any,
  excludeGlobs: any[] | null,
  followSymlinks: boolean,
): AsyncGenerator<any> {
  const fullDir = relDir ? pathMod.join(baseCwd, relDir) : baseCwd;
  let entries: any[];
  try {
    entries = await fs.promises.readdir(fullDir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const dirent of entries) {
    const relPath = relDir ? `${relDir}${sep}${dirent.name}` : dirent.name;
    if (typeof exclude === "function") {
      if (exclude(dirent)) continue;
    } else if (excludeGlobs) {
      if (excludeGlobs.some((g: any) => g.match(relPath))) continue;
    }
    if (globs.some(g => g.match(relPath))) {
      yield dirent;
    }
    if (dirent.isDirectory() || (followSymlinks && dirent.isSymbolicLink())) {
      yield* walkWithFileTypes(fs, pathMod, baseCwd, relPath, globs, exclude, excludeGlobs, followSymlinks);
    }
  }
}

async function* glob(pattern: string | string[], options?: GlobOptions): AsyncGenerator<any> {
  const patterns = validatePattern(pattern);
  const globOptions = mapOptions(options || {});
  const exclude = globOptions.exclude;
  const excludeGlobs = Array.isArray(exclude)
    ? exclude.flatMap(pattern => [new Bun.Glob(pattern), new Bun.Glob(pattern.replace(/\/+$/, "") + "/**")])
    : null;

  if (options?.withFileTypes) {
    const fs = require("node:fs");
    const pathMod = require("node:path");
    const globs = patterns.map(p => new Bun.Glob(p));
    yield* walkWithFileTypes(
      fs,
      pathMod,
      globOptions.cwd as string,
      "",
      globs,
      exclude,
      excludeGlobs,
      Boolean(globOptions.followSymlinks),
    );
    return;
  }

  const seen = new Set<string>();
  const isExcluded = (ent: string) => {
    if (typeof exclude === "function") {
      return exclude(ent);
    }
    if (excludeGlobs) {
      return excludeGlobs.some(glob => glob.match(ent));
    }
    return false;
  };

  for (const pat of patterns) {
    if (pat.endsWith("/**") || (isWindows && pat.endsWith("\\**"))) {
      const dirPat = pat.slice(0, -2);
      for await (const ent of new Bun.Glob(dirPat).scan(globOptions)) {
        if (seen.has(ent)) continue;
        seen.add(ent);
        if (isExcluded(ent)) continue;
        yield ent;
      }
    }
    for await (const ent of new Bun.Glob(pat).scan(globOptions)) {
      if (seen.has(ent)) continue;
      seen.add(ent);
      if (isExcluded(ent)) continue;
      yield ent;
    }
  }
}

function* globSync(pattern: string | string[], options?: GlobOptions): Generator<any> {
  const patterns = validatePattern(pattern);
  const globOptions = mapOptions(options || {});
  const exclude = globOptions.exclude;
  const excludeGlobs = Array.isArray(exclude)
    ? exclude.flatMap(pattern => [new Bun.Glob(pattern), new Bun.Glob(pattern.replace(/\/+$/, "") + "/**")])
    : null;

  if (options?.withFileTypes) {
    const fs = require("node:fs");
    const pathMod = require("node:path");
    const globs = patterns.map(p => new Bun.Glob(p));
    yield* walkWithFileTypesSync(
      fs,
      pathMod,
      globOptions.cwd as string,
      "",
      globs,
      exclude,
      excludeGlobs,
      Boolean(globOptions.followSymlinks),
    );
    return;
  }

  const seen = new Set<string>();
  const isExcluded = (ent: string) => {
    if (typeof exclude === "function") {
      return exclude(ent);
    }
    if (excludeGlobs) {
      return excludeGlobs.some(glob => glob.match(ent));
    }
    return false;
  };

  for (const pat of patterns) {
    if (pat.endsWith("/**") || (isWindows && pat.endsWith("\\**"))) {
      const dirPat = pat.slice(0, -2);
      for (const ent of new Bun.Glob(dirPat).scanSync(globOptions)) {
        if (seen.has(ent)) continue;
        seen.add(ent);
        if (isExcluded(ent)) continue;
        yield ent;
      }
    }
    for (const ent of new Bun.Glob(pat).scanSync(globOptions)) {
      if (seen.has(ent)) continue;
      seen.add(ent);
      if (isExcluded(ent)) continue;
      yield ent;
    }
  }
}

function validatePattern(pattern: string | string[]): string[] {
  if (Array.isArray(pattern)) {
    validateArray(pattern, "pattern");
    return pattern.map(p => {
      validateString(p, "pattern");
      return isWindows ? p.replaceAll("/", sep) : p;
    });
  }

  validateString(pattern, "pattern");
  return [isWindows ? pattern.replaceAll("/", sep) : pattern];
}

function mapOptions(options: GlobOptions): GlobScanOptions & { exclude: GlobOptions["exclude"] } {
  validateObject(options, "options");

  let exclude = options.exclude ?? no;
  if (Array.isArray(exclude)) {
    validateArray(exclude, "options.exclude");
    if (isWindows) {
      exclude = exclude.map((pattern: string) => pattern.replaceAll("\\", "/"));
    }
  } else {
    validateFunction(exclude, "options.exclude");
  }

  if (options.followSymlinks !== undefined) {
    validateBoolean(options.followSymlinks, "options.followSymlinks");
  }

  if (options.withFileTypes !== undefined) {
    validateBoolean(options.withFileTypes, "options.withFileTypes");
  }

  return {
    // NOTE: this is subtly different from Glob's default behavior.
    // `process.cwd()` may be overridden by JS code, but native code will used the
    // cached `getcwd` on BunProcess.
    cwd: options?.cwd ?? process.cwd(),
    // https://github.com/nodejs/node/blob/a9546024975d0bfb0a8ae47da323b10fb5cbb88b/lib/internal/fs/glob.js#L655
    followSymlinks: options.followSymlinks ?? true,
    // https://github.com/oven-sh/bun/issues/20507
    onlyFiles: false,
    exclude,
  };
}

// `var` avoids TDZ checks.
var no = _ => false;

export default { glob, globSync };
