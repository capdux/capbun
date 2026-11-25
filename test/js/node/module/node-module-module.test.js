import { describe, expect, test } from "bun:test";
import { bunEnv, bunExe, normalizeBunSnapshot, ospath, tempDir } from "harness";
import Module, { _nodeModulePaths, builtinModules, createRequire, isBuiltin, wrap } from "module";
import path from "path";

describe.concurrent("node-module-module", () => {
  test("builtinModules exists", () => {
    expect(Array.isArray(builtinModules)).toBe(true);
    expect(builtinModules).toHaveLength(76);
  });

  test("isBuiltin() works", () => {
    expect(isBuiltin("fs")).toBe(true);
    expect(isBuiltin("path")).toBe(true);
    expect(isBuiltin("crypto")).toBe(true);
    expect(isBuiltin("assert")).toBe(true);
    expect(isBuiltin("util")).toBe(true);
    expect(isBuiltin("events")).toBe(true);
    expect(isBuiltin("node:events")).toBe(true);
    expect(isBuiltin("node:bacon")).toBe(false);
    expect(isBuiltin("node:test")).toBe(true);
    expect(isBuiltin("test")).toBe(false); // "test" does not alias to "node:test"
  });

  test("module.globalPaths exists", () => {
    expect(Array.isArray(require("module").globalPaths)).toBe(true);
  });

  test("createRequire trailing slash", () => {
    const req = createRequire(import.meta.dir + "/");
    expect(req.resolve("./node-module-module.test.js")).toBe(
      ospath(path.resolve(import.meta.dir, "./node-module-module.test.js")),
    );
  });

  test("createRequire trailing slash file url", () => {
    const req = createRequire(Bun.pathToFileURL(import.meta.dir + "/"));
    expect(req.resolve("./node-module-module.test.js")).toBe(
      ospath(path.resolve(import.meta.dir, "./node-module-module.test.js")),
    );
  });

  test("Module exists", () => {
    expect(Module).toBeDefined();
  });

  test("module.Module works", () => {
    expect(Module.Module === Module).toBeTrue();

    const m = new Module("asdf");
    expect(m.exports).toEqual({});
  });

  test("_nodeModulePaths() works", () => {
    const root = path.resolve("/");
    expect(() => {
      _nodeModulePaths();
    }).toThrow();
    expect(_nodeModulePaths(".").length).toBeGreaterThan(0);
    expect(_nodeModulePaths(".").pop()).toBe(root + "node_modules");
    expect(_nodeModulePaths("")).toEqual(_nodeModulePaths("."));
    expect(_nodeModulePaths("/")).toEqual([root + "node_modules"]);
    expect(_nodeModulePaths("/a/b/c/d")).toEqual([
      ospath(root + "a/b/c/d/node_modules"),
      ospath(root + "a/b/c/node_modules"),
      ospath(root + "a/b/node_modules"),
      ospath(root + "a/node_modules"),
      ospath(root + "node_modules"),
    ]);
    expect(_nodeModulePaths("/a/b/../d")).toEqual([
      ospath(root + "a/d/node_modules"),
      ospath(root + "a/node_modules"),
      ospath(root + "node_modules"),
    ]);
  });

  test("Module.wrap", () => {
    var mod = { exports: {} };
    expect(eval(wrap("exports.foo = 1; return 42"))(mod.exports, mod)).toBe(42);
    expect(mod.exports.foo).toBe(1);
    expect(wrap()).toBe("(function (exports, require, module, __filename, __dirname) { undefined\n});");
  });

  test("Overwriting _resolveFilename", async () => {
    await using proc = Bun.spawn({
      cmd: [bunExe(), "run", path.join(import.meta.dir, "resolveFilenameOverwrite.cjs")],
      env: bunEnv,
      stderr: "inherit",
      stdout: "pipe",
    });

    const stdout = await proc.stdout.text();
    expect(stdout.trim().endsWith("--pass--")).toBe(true);
    expect(await proc.exited).toBe(0);
  });

  test("Overwriting Module.prototype.require", async () => {
    await using proc = Bun.spawn({
      cmd: [bunExe(), "run", path.join(import.meta.dir, "modulePrototypeOverwrite.cjs")],
      env: bunEnv,
      stderr: "inherit",
      stdout: "pipe",
    });

    const stdout = await proc.stdout.text();
    expect(stdout.trim().endsWith("--pass--")).toBe(true);
    expect(await proc.exited).toBe(0);
  });

  test.each([
    "/file/name/goes/here.js",
    "file/here.js",
    "file\\here.js",
    "/file\\here.js",
    "\\file\\here.js",
    "\\file/here.js",
  ])("Module.prototype._compile", filename => {
    const module = new Module("module id goes here");
    const starting_exports = module.exports;
    const r = module._compile("module.exports = { module, exports, require, __filename, __dirname }", filename);
    expect(r).toBe(undefined);
    expect(module.exports).not.toBe(starting_exports);
    const { module: m, exports: e, require: req, __filename: fn, __dirname: dn } = module.exports;
    expect(m).toBe(module);
    expect(e).toBe(starting_exports);
    expect(req).toBe(module.require);
    expect(fn).toBe(filename);
    expect(dn).toBe(path.dirname(filename));
  });

  test("Module._extensions", () => {
    expect(".js" in Module._extensions).toBeTrue();
    expect(".json" in Module._extensions).toBeTrue();
    expect(".node" in Module._extensions).toBeTrue();
    expect(require.extensions).toBe(Module._extensions);
  });

  test("Module._resolveLookupPaths", () => {
    expect(Module._resolveLookupPaths("foo")).toEqual([]);
    expect(Module._resolveLookupPaths("./bar", { id: "1", filename: "/baz/abc" })).toEqual(["/baz"]);
    expect(Module._resolveLookupPaths("./bar", {})).toEqual(["."]);
    expect(Module._resolveLookupPaths("./bar", { paths: ["a"] })).toEqual(["."]);
    expect(Module._resolveLookupPaths("bar", { paths: ["a"] })).toEqual(["a"]);
  });

  test("Module.findSourceMap doesn't throw", () => {
    expect(Module.findSourceMap("foo")).toEqual(undefined);
  });

  test("require cache relative specifier", () => {
    require.cache["./bar.cjs"] = { exports: { default: "bar" } };
    expect(() => require("./bar.cjs")).toThrow("Cannot find module");
  });
  test("builtin resolution", () => {
    expect(require.resolve("fs")).toBe("fs");
    expect(require.resolve("node:fs")).toBe("node:fs");
  });
  test("require cache node builtins specifier", () => {
    // as js builtin
    try {
      const fake = { default: "bar" };
      const real = require("fs");
      expect(require.cache["fs"]).toBe(undefined);
      require.cache["fs"] = { exports: fake };
      expect(require("fs")).toBe(fake);
      expect(require("node:fs")).toBe(real);
    } finally {
      delete require.cache["fs"];
    }

    // as native module
    try {
      const fake = { default: "bar" };
      const real = require("util/types");
      expect(require.cache["util/types"]).toBe(undefined);
      require.cache["util/types"] = { exports: fake };
      expect(require("util/types")).toBe(fake);
      expect(require("node:util/types")).toBe(real);
    } finally {
      delete require.cache["util/types"];
    }
  });
  test("require a cjs file uses the 'module.exports' export", () => {
    expect(require("./esm_to_cjs_interop.mjs")).toEqual(Symbol.for("meow"));
  });

  test("Module.runMain", async () => {
    await using proc = Bun.spawn({
      cmd: [
        bunExe(),
        "--require",
        path.join(import.meta.dir, "overwrite-module-run-main-1.cjs"),
        path.join(import.meta.dir, "overwrite-module-run-main-2.cjs"),
      ],
      env: bunEnv,
      stderr: "inherit",
      stdout: "pipe",
    });

    const stdout = await proc.stdout.text();
    expect(stdout.trim()).toBe("pass");
    expect(await proc.exited).toBe(0);
  });
  test("Module.runMain 2", async () => {
    await using proc = Bun.spawn({
      cmd: [
        bunExe(),
        "--require",
        path.join(import.meta.dir, "overwrite-module-run-main-3.cjs"),
        path.join(import.meta.dir, "overwrite-module-run-main-2.cjs"),
      ],
      env: bunEnv,
      stderr: "inherit",
      stdout: "pipe",
    });

    const stdout = await proc.stdout.text();
    expect(stdout.trim()).toBe("pass");
    expect(await proc.exited).toBe(0);
  });
  test.each(["no args", "--access-early"])("children, %s", async arg => {
    await using proc = Bun.spawn({
      cmd: [bunExe(), path.join(import.meta.dir, "children-fixture/a.cjs"), arg],
      env: bunEnv,
      stderr: "inherit",
      stdout: "pipe",
    });

    const stdout = await proc.stdout.text();
    expect(stdout.trim()).toBe(`. (./a.cjs)
 ./b.cjs
  . (./a.cjs) (seen)
  ./b.cjs (seen)
  ./c.cjs
   ./d.cjs
    ./d.cjs (seen)
 ./d.cjs (seen)
 ./f.cjs
  ./d.cjs (seen)
 ./g.cjs
  ./b.cjs (seen)
  . (./a.cjs) (seen)
  ./h.cjs
   ./i.cjs
    ./j.cjs
     ./i.cjs (seen)
     ./j.cjs (seen)
     ./k.cjs
      ./j.cjs (seen)
   ./j.cjs (seen)
   ./k.cjs (seen)`);
    expect(await proc.exited).toBe(0);
  });
});

test("Overwriting _resolveFilename with a non-callable makes require() throw like Node", async () => {
  const { bunEnv, bunExe, tempDir } = require("harness");
  using dir = tempDir("resolve-filename-non-callable", {
    "dep.cjs": `module.exports = "dep";`,
    "main.cjs": `
      const Module = require("module");
      const original = Module._resolveFilename;
      const attempt = fn => {
        try {
          return "returned " + String(fn());
        } catch (e) {
          return e.constructor.name + ": " + e.message;
        }
      };
      const results = {};
      for (const [label, value] of [
        ["object", {}],
        ["string", "not a function"],
        ["undefined", undefined],
        ["null", null],
        ["number", 42],
        ["symbol", Symbol("s")],
      ]) {
        Module._resolveFilename = value;
        results[label] = {
          readsBack: Object.is(Module._resolveFilename, value),
          require: attempt(() => require("./dep.cjs")),
          requireResolve: attempt(() => require.resolve("./dep.cjs")),
          createRequire: attempt(() => Module.createRequire(__filename)("./dep.cjs")),
        };
      }
      Module._resolveFilename = new Proxy(original, {});
      results.callableProxy = attempt(() => require("./dep.cjs"));
      Module._resolveFilename = original;
      results.restored = {
        readsBack: Module._resolveFilename === original,
        require: attempt(() => require("./dep.cjs")),
      };
      console.log(JSON.stringify(results));
    `,
  });
  await using proc = Bun.spawn({
    cmd: [bunExe(), path.join(String(dir), "main.cjs")],
    env: bunEnv,
    cwd: String(dir),
    stderr: "pipe",
  });
  const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
  expect(stderr).toBe("");
  const notAFunction = {
    readsBack: true,
    require: "TypeError: Module._resolveFilename is not a function",
    requireResolve: "TypeError: Module._resolveFilename is not a function",
    createRequire: "TypeError: Module._resolveFilename is not a function",
  };
  expect(JSON.parse(stdout)).toEqual({
    object: notAFunction,
    string: notAFunction,
    undefined: notAFunction,
    null: notAFunction,
    number: notAFunction,
    symbol: notAFunction,
    callableProxy: "returned dep",
    restored: { readsBack: true, require: "returned dep" },
  });
  expect(exitCode).toBe(0);
});

describe.concurrent("Module.runMain set by a preload", () => {
  const handlers = `
    process.on("uncaughtException", error => console.log("uncaughtException: " + error.message));
    process.on("unhandledRejection", error => console.log("unhandledRejection: " + error.message));
  `;
  async function run(preload, inWorker = false, main = `console.log("main ran");`) {
    using dir = tempDir("module-run-main", {
      "preload.cjs": preload,
      "main.cjs": main,
      "worker.mjs": `new Worker(import.meta.dir + "/main.cjs", { preload: [import.meta.dir + "/preload.cjs"] });`,
    });
    await using proc = Bun.spawn({
      cmd: inWorker ? [bunExe(), "./worker.mjs"] : [bunExe(), "--require", "./preload.cjs", "./main.cjs"],
      env: bunEnv,
      cwd: String(dir),
      stderr: "pipe",
      stdout: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    return { stdout, stderr: normalizeBunSnapshot(stderr, dir), exitCode };
  }

  test.each([
    ["{}", "Object"],
    ["[]", "Array"],
    [`"a string"`, `"a string"`],
    [`Symbol("s")`, "Symbol(s)"],
    ["10n", "10"],
  ])("to %s, which is not a function", async (value, described) => {
    expect(await run(`require("module").runMain = ${value};`)).toEqual({
      stdout: "",
      stderr: `TypeError: ${described} is not a function\n\nBun v<bun-version>`,
      exitCode: 1,
    });
  });

  test("to a function that throws", async () => {
    const { stdout, stderr, exitCode } = await run(
      `require("module").runMain = () => {\n  throw new RangeError("from the override");\n};`,
    );
    expect(stderr).toMatchInlineSnapshot(`
      "1 | require("module").runMain = () => {
      2 |   throw new RangeError("from the override");
                      ^
      RangeError: from the override
          at <anonymous> (file:NN:NN)

      Bun v<bun-version>"
    `);
    expect({ stdout, exitCode }).toEqual({ stdout: "", exitCode: 1 });
  });

  test("to a function that calls the original and throws", async () => {
    const preload = `
      const Module = require("module");
      const runMain = Module.runMain;
      Module.runMain = (...args) => {
        runMain(...args);
        throw new Error("after the original");
      };
    `;
    expect(await run(preload)).toMatchObject({ stdout: "main ran\n", exitCode: 1 });
    expect(await run(handlers + preload)).toEqual({
      stdout: "main ran\nunhandledRejection: after the original\n",
      stderr: "",
      exitCode: 0,
    });
    expect(await run(handlers + preload, false, `throw new Error("from main");`)).toEqual({
      stdout: "unhandledRejection: after the original\nuncaughtException: from main\n",
      stderr: "",
      exitCode: 0,
    });
  });

  test.each([
    ["is not a function", "{}", "Object is not a function"],
    ["throws", `() => { throw new Error("thrown"); }`, "thrown"],
  ])("one that %s is reported once", async (_, value, message) => {
    const expected = { stdout: `uncaughtException: ${message}\n`, stderr: "", exitCode: 0 };
    expect(await run(`${handlers} require("module").runMain = ${value};`)).toEqual(expected);
    expect(await run(`${handlers} require("module").runMain = ${value};`, true)).toEqual(expected);
  });
});
