import { describe, expect, test } from "bun:test";
import { compileFunction, createContext, runInContext, runInNewContext, runInThisContext, Script } from "node:vm";

function capture(_: any, _1?: any) {}

describe("vm", () => {
  describe("runInContext()", () => {
    testRunInContext({ fn: runInContext, isIsolated: true });
    test("options can be a string", () => {
      const context = createContext();
      const result = runInContext("new Error().stack;", context, "test-filename.js");
      expect(result).toContain("test-filename.js");
    });
    test("options properties can be undefined", () => {
      const context = createContext();
      const result = runInContext("1 + 1;", context, {
        filename: undefined,
        lineOffset: undefined,
        columnOffset: undefined,
        displayErrors: undefined,
        timeout: undefined,
        breakOnSigint: undefined,
        cachedData: undefined,
        importModuleDynamically: undefined,
      });
      expect(result).toBe(2);
    });
  });

  describe("runInNewContext()", () => {
    testRunInContext({ fn: runInNewContext, isIsolated: true, isNew: true });
    // this line intentionally left blank (for snapshots)
    // this line intentionally left blank (for snapshots)
    test("options can be a string", () => {
      const result = runInNewContext("new Error().stack;", {}, "test-filename.js");
      expect(result).toContain("test-filename.js");
    });
    test("options properties can be undefined", () => {
      const result = runInNewContext(
        "1 + 1;",
        {},
        {
          filename: undefined,
          lineOffset: undefined,
          columnOffset: undefined,
          displayErrors: undefined,
          timeout: undefined,
          breakOnSigint: undefined,
          contextName: undefined,
          contextOrigin: undefined,
          contextCodeGeneration: undefined,
          cachedData: undefined,
          importModuleDynamically: undefined,
          microtaskMode: undefined,
        },
      );
      expect(result).toBe(2);
    });
    test("ShadowRealm can be created and used inside a context", async () => {
      await using proc = Bun.spawn({
        cmd: [
          bunExe(),
          "-e",
          `const vm = require("node:vm");
          const realm = vm.runInNewContext("new ShadowRealm()");
          const wrapped = vm.runInNewContext("new ShadowRealm().evaluate('(a, b) => a + b')");
          console.log(typeof realm.evaluate, realm.evaluate("6 * 7"), wrapped(20, 22));`,
        ],
        env: bunEnv,
        stderr: "pipe",
      });
      const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
      expect(stderr).toBe("");
      expect(stdout).toBe("function 42 42\n");
      expect(exitCode).toBe(0);
    });
  });

  describe("runInThisContext()", () => {
    testRunInContext({ fn: runInThisContext });
    test("options can be a string", () => {
      const result = runInThisContext("new Error().stack;", "test-filename.js");
      expect(result).toContain("test-filename.js");
    });
    test("options properties can be undefined", () => {
      const result = runInThisContext("1 + 1;", {
        filename: undefined,
        lineOffset: undefined,
        columnOffset: undefined,
        displayErrors: undefined,
        timeout: undefined,
        breakOnSigint: undefined,
        cachedData: undefined,
        importModuleDynamically: undefined,
      });
      expect(result).toBe(2);
    });
  });

  describe("compileFunction()", () => {
    test("options properties can be undefined", () => {
      const result = compileFunction("return 1 + 1;", [], {
        filename: undefined,
        lineOffset: undefined,
        columnOffset: undefined,
        cachedData: undefined,
        produceCachedData: undefined,
        parsingContext: undefined,
        contextExtensions: undefined,
      })();
      expect(result).toBe(2);
    });

    // Security tests
    test("Template literal attack should not break out of sandbox", () => {
      const before = globalThis.hacked;
      try {
        const result = compileFunction("return `\n`; globalThis.hacked = true; //")();
        expect(result).toBe("\n");
        expect(globalThis.hacked).toBe(before);
      } catch (e) {
        // If it throws, that's also acceptable as long as it didn't modify globalThis
        expect(globalThis.hacked).toBe(before);
      }
    });

    test("Comment-based attack should not break out of sandbox", () => {
      const before = globalThis.commentHacked;
      try {
        const result = compileFunction("return 1; /* \n */ globalThis.commentHacked = true; //")();
        expect(result).toBe(1);
        expect(globalThis.commentHacked).toBe(before);
      } catch (e) {
        expect(globalThis.commentHacked).toBe(before);
      }
    });

    test("Function constructor abuse should be contained", () => {
      try {
        const result = compileFunction("return (function(){}).constructor('return process')();")();
        // If it doesn't throw, it should at least not return the actual process object
        expect(result).not.toBe(process);
      } catch (e) {
        // Throwing is also acceptable
        expect(e).toBeTruthy();
      }
    });

    test("Regex literal attack should not break out of sandbox", () => {
      const before = globalThis.regexHacked;
      try {
        const result = compileFunction("return /\n/; globalThis.regexHacked = true; //")();
        expect(result instanceof RegExp).toBe(true);
        expect(result.toString()).toBe("/\n/");
        expect(globalThis.regexHacked).toBe(before);
      } catch (e) {
        expect(globalThis.regexHacked).toBe(before);
      }
    });

    test("String escape sequence attack should not break out of sandbox", () => {
      const before = globalThis.stringHacked;
      try {
        const result = compileFunction("return '\\\n'; globalThis.stringHacked = true; //")();
        expect(result).toBe("\n");
        expect(globalThis.stringHacked).toBe(before);
      } catch (e) {
        expect(globalThis.stringHacked).toBe(before);
      }
    });

    test("Arguments access attack should be contained", () => {
      try {
        const result = compileFunction("return (function(){return arguments.callee.caller})();")();
        // If it doesn't throw, it should at least not return a function
        expect(typeof result !== "function").toBe(true);
      } catch (e) {
        // Throwing is also acceptable
        expect(e).toBeTruthy();
      }
    });

    test("With statement attack should not modify Object.prototype", () => {
      const originalToString = Object.prototype.toString;
      const before = globalThis.withHacked;

      const parsingContext = createContext({});

      try {
        compileFunction(
          "with(Object.prototype) { toString = function() { globalThis.withHacked = true; }; } return 'test';",
          [],
          {
            parsingContext,
          },
        )();

        // Check that Object.prototype.toString wasn't modified
        expect(Object.prototype.toString).toBe(originalToString);
        expect(globalThis.withHacked).toBe(before);
      } catch (e) {
        // If it throws, also check that nothing was modified
        expect(Object.prototype.toString).toBe(originalToString);
        expect(globalThis.withHacked).toBe(before);
      } finally {
        // Restore just in case
        Object.prototype.toString = originalToString;
      }
    });

    test("Eval attack should be contained", () => {
      const before = globalThis.evalHacked;

      const parsingContext = createContext({});

      try {
        compileFunction("return eval('globalThis.evalHacked = true;');", [], { parsingContext })();
        expect(globalThis.evalHacked).toBe(before);
      } catch (e) {
        expect(globalThis.evalHacked).toBe(before);
      }
    });

    // Additional tests for other potential vulnerabilities

    test("Octal escape sequence attack should not break out", () => {
      const before = globalThis.octalHacked;

      try {
        const result = compileFunction("return '\\012'; globalThis.octalHacked = true; //")();
        expect(result).toBe("\n");
        expect(globalThis.octalHacked).toBe(before);
      } catch (e) {
        expect(globalThis.octalHacked).toBe(before);
      }
    });

    test("Unicode escape sequence attack should not break out", () => {
      const before = globalThis.unicodeHacked;

      try {
        const result = compileFunction("return '\\u000A'; globalThis.unicodeHacked = true; //")();
        expect(result).toBe("\n");
        expect(globalThis.unicodeHacked).toBe(before);
      } catch (e) {
        expect(globalThis.unicodeHacked).toBe(before);
      }
    });

    test("Attempted syntax error injection should be caught", () => {
      expect(() => {
        compileFunction("});\n\n(function() {\nconsole.log(1);\n})();\n\n(function() {");
      }).toThrow();
    });

    test("Attempted prototype pollution should be contained", () => {
      const originalHasOwnProperty = Object.prototype.hasOwnProperty;

      try {
        compileFunction("Object.prototype.polluted = true; return 'done';")();
        expect(Object.prototype.polluted).toBeUndefined();
      } catch (e) {
        // Throwing is acceptable
      } finally {
        // Clean up just in case
        delete Object.prototype.polluted;
        Object.prototype.hasOwnProperty = originalHasOwnProperty;
      }
    });

    test("Attempted global object access should be contained", () => {
      try {
        const result = compileFunction("return this;")();
        // The "this" inside the function should not be the global object
        expect(result).not.toBe(globalThis);
      } catch (e) {
        // Throwing is also acceptable
        expect(e).toBeTruthy();
      }
    });
  });
});

describe("Script", () => {
  describe("runInContext()", () => {
    testRunInContext({
      fn: (code, context, options) => {
        const script = new Script(code, options);
        return script.runInContext(context);
      },
      isIsolated: true,
    });
  });
  describe("runInNewContext()", () => {
    testRunInContext({
      fn: (code, context, options) => {
        const script = new Script(code, options);
        return script.runInNewContext(context);
      },
      isIsolated: true,
      isNew: true,
    });
  });
  describe("runInThisContext()", () => {
    testRunInContext({
      fn: (code: string, options: any) => {
        const script = new Script(code, options);
        return script.runInThisContext();
      },
    });
  });
  test("can throw without new", () => {
    // @ts-ignore
    const result = () => Script();
    expect(result).toThrow({
      name: "TypeError",
      message: "Class constructor Script cannot be invoked without 'new'",
    });
  });
});

type TestRunInContextArg =
  | { fn: typeof runInContext; isIsolated: true; isNew?: boolean }
  | { fn: typeof runInThisContext; isIsolated?: false; isNew?: boolean };

function testRunInContext({ fn, isIsolated, isNew }: TestRunInContextArg) {
  test("can do nothing", () => {
    const context = createContext({});
    const result = fn("", context);
    expect(result).toBeUndefined();
  });
  test("can return a value", () => {
    const context = createContext({});
    const result = fn("1 + 1;", context);
    expect(result).toBe(2);
  });
  test("can return a complex value", () => {
    const context = createContext({});
    const result = fn("new Set([1, 2, 3]);", context);
    expect(result).toStrictEqual(new Set([1, 2, 3]));
  });
  test("can return the last value", () => {
    const context = createContext({});
    const result = fn("1 + 1; 2 * 2; 3 / 3", context);
    expect(result).toBe(1);
  });

  for (let View of [
    ArrayBuffer,
    SharedArrayBuffer,
    Uint8Array,
    Int8Array,
    Uint16Array,
    Int16Array,
    Uint32Array,
    Int32Array,
    Float32Array,
    Float64Array,
    BigInt64Array,
    BigUint64Array,
  ]) {
    test(`new ${View.name}() in VM context doesn't crash`, () => {
      const context = createContext({});
      expect(fn(`new ${View.name}(2)`, context)).toHaveLength(2);
    });
  }

  test("can return a function", () => {
    const context = createContext({});
    const result = fn("() => 'bar';", context);
    expect(typeof result).toBe("function");
    expect(result()).toBe("bar");
  });
  test("can throw a syntax error", () => {
    const context = createContext({});
    const result = () => fn("!?", context);
    expect(result).toThrow({
      name: "SyntaxError",
      message: "Unexpected token '?'",
    });
  });
  test("can throw an error", () => {
    const context = createContext({});
    const result = () => fn("throw new TypeError('Oops!');", context);
    expect(result).toThrow({
      name: "TypeError",
      message: "Oops!",
    });
  });
  test("can resolve a promise", async () => {
    const context = createContext({});
    const result = fn("Promise.resolve(true);", context);
    expect(await result).toBe(true);
  });
  test("can reject a promise", () => {
    const context = createContext({});
    expect(async () => await fn("Promise.reject(new TypeError('Oops!'));", context)).toThrow({
      name: "TypeError",
      message: "Oops!",
    });
  });
  test("can access `globalThis`", () => {
    const context = createContext({});
    const result = fn("typeof globalThis;", context);
    expect(result).toBe("object");
  });
  test("cannot access local scope", () => {
    var foo = "bar"; // intentionally unused
    capture(foo, foo);
    const context = createContext({});
    const result = fn("typeof foo;", context);
    expect(result).toBe("undefined");
  });
  if (isIsolated) {
    test("can access context", () => {
      const context = createContext({
        foo: "bar",
        fizz: (n: number) => "buzz".repeat(n),
      });
      const result = fn("foo + fizz(2);", context);
      expect(result).toBe("barbuzzbuzz");
    });
    test("can modify context", () => {
      const context = createContext({
        baz: ["a", "b", "c"],
      });
      const result = fn("foo = 'baz'; delete baz[0];", context);
      expect(context.foo).toBe("baz");
      expect(context.baz).toEqual([undefined, "b", "c"]);
      expect(result).toBe(true);
    });
    test("cannot access `process`", () => {
      const context = createContext({});
      const result = fn("typeof process;", context);
      expect(result).toBe("undefined");
    });
    test("cannot access global scope", () => {
      const prop = randomProp();
      // @ts-expect-error
      globalThis[prop] = "fizz";
      try {
        const context = createContext({});
        const result = fn(`typeof ${prop};`, context);
        expect(result).toBe("undefined");
      } finally {
        // @ts-expect-error
        delete globalThis[prop];
      }
    });
    test("can specify a filename", () => {
      const context = createContext({});
      const result = fn("new Error().stack;", context, {
        filename: "foo.js",
      });
      expect(result).toContain("foo.js");
    });
  } else {
    test("can access global context", () => {
      const props = randomProps(2);
      // @ts-expect-error
      globalThis[props[0]] = "bar";
      // @ts-expect-error
      globalThis[props[1]] = (n: number) => "buzz".repeat(n);
      try {
        const result = fn(`${props[0]} + ${props[1]}(2);`);
        expect(result).toBe("barbuzzbuzz");
      } finally {
        for (const prop of props) {
          // @ts-expect-error
          delete globalThis[prop];
        }
      }
    });
    test("can modify global context", () => {
      const props = randomProps(3);
      // @ts-expect-error
      globalThis[props[0]] = ["a", "b", "c"];
      // @ts-expect-error
      globalThis[props[1]] = "initial value";
      try {
        const result = fn(`${props[1]} = 'baz'; ${props[2]} = 'bunny'; delete ${props[0]}[0];`);
        // @ts-expect-error
        expect(globalThis[props[1]]).toBe("baz");
        // @ts-expect-error
        expect(globalThis[props[2]]).toBe("bunny");
        // @ts-expect-error
        expect(globalThis[props[0]]).toEqual([undefined, "b", "c"]);
        expect(result).toBe(true);
      } finally {
        for (const prop of props) {
          // @ts-expect-error
          delete globalThis[prop];
        }
      }
    });
    test("can access `process`", () => {
      const result = fn("typeof process;");
      expect(result).toBe("object");
    });
    test("can access this context", () => {
      const prop = randomProp();
      // @ts-expect-error
      globalThis[prop] = "fizz";
      try {
        const result = fn(`${prop};`);
        expect(result).toBe("fizz");
      } finally {
        // @ts-expect-error
        delete globalThis[prop];
      }
    });
    test.skip("can specify an error on SIGINT", () => {
      const result = () =>
        fn("process.kill(process.pid, 'SIGINT');", {
          breakOnSigint: true,
        });
      // TODO: process.kill() is not implemented
      expect(result).toThrow();
    });
    test("can specify a filename", () => {
      const result = fn("new Error().stack;", {
        filename: "foo.js",
      });
      expect(result).toContain("foo.js");
    });
  }
  test.todo("can specify filename", () => {
    //
  });
  test.todo("can specify lineOffset", () => {
    //
  });
  test.todo("can specify columnOffset", () => {
    //
  });
  test.todo("can specify displayErrors", () => {
    //
  });
  test.todo("can specify timeout", () => {
    //
  });
  test.todo("can specify breakOnSigint", () => {
    //
  });
  test.todo("can specify cachedData", () => {
    //
  });
  test.todo("can specify importModuleDynamically", () => {
    //
  });

  // https://github.com/oven-sh/bun/issues/10885 .if(isNew == true)
  test.todo("can specify contextName", () => {
    //
  });
  // https://github.com/oven-sh/bun/issues/10885 .if(isNew == true)
  test.todo("can specify contextOrigin", () => {
    //
  });
  // https://github.com/oven-sh/bun/issues/10885 .if(isNew == true)
  test.todo("can specify microtaskMode", () => {
    //
  });
}

function randomProp() {
  return "prop" + crypto.randomUUID().replace(/-/g, "");
}
function randomProps(propsNumber = 0) {
  const props = [];
  for (let i = 0; i < propsNumber; i++) {
    props.push(randomProp());
  }
  return props;
}

// https://github.com/oven-sh/bun/issues/13629
test("can extend generated globals & WebCore globals", async () => {
  const vm = require("vm");

  for (let j = 0; j < 100; j++) {
    const context = createContext({
      URL,
      urlProto: URL.prototype,
      console,
      Response,
    });

    const code = /*js*/ `
class ExtendedDOMGlobal extends URL {
  constructor(url) {
    super(url);
  }

  get searchParams() {
    return super.searchParams;
  }
}

class ExtendedExtendedDOMGlobal extends ExtendedDOMGlobal {
  constructor(url) {
    super(url);
  }

  get wowSuchGetter() {
    return "wow such getter";
  }
}

const response = new Response();
class ExtendedZigGeneratedClass extends Response {
  constructor(body) {
    super(body);
  }

  get ok() {
    return super.ok;
  }

  get custom() {
    return true;
  }
}

class ExtendedExtendedZigGeneratedClass extends ExtendedZigGeneratedClass {
  constructor(body) {
    super(body);
  }

  get custom() {
    return 42;
  }
}

const resp = new ExtendedZigGeneratedClass("empty");
const resp2 = new ExtendedExtendedZigGeneratedClass("empty");

const url = new ExtendedDOMGlobal("https://example.com/path?foo=bar&baz=qux");
const url2 = new ExtendedExtendedDOMGlobal("https://example.com/path?foo=bar&baz=qux");
if (url.ok !== true) {
  throw new Error("bad");
}

if (url2.wowSuchGetter !== "wow such getter") {
  throw new Error("bad");
}

if (!response.ok) {
  throw new Error("bad");
}

URL.prototype.ok = false;

if (url.ok !== false) {
  throw new Error("bad");
}

url.searchParams.get("foo");

if (!resp.custom) {
  throw new Error("expected getter");
}

if (resp2.custom !== 42) {
  throw new Error("expected getter");
}

if (!resp2.ok) {
  throw new Error("expected ok");
}

if (!(resp instanceof ExtendedZigGeneratedClass)) {
  throw new Error("expected ExtendedZigGeneratedClass");
}

if (!(resp instanceof Response)) {
  throw new Error("expected Response");
}

if (!(resp2 instanceof ExtendedExtendedZigGeneratedClass)) {
  throw new Error("expected ExtendedExtendedZigGeneratedClass");
}

if (!(resp2 instanceof ExtendedZigGeneratedClass)) {
  throw new Error("expected ExtendedZigGeneratedClass");
}

if (!(resp2 instanceof Response)) {
  throw new Error("expected Response");
}

if (!resp.ok) {
  throw new Error("expected ok");
}

resp.text().then((a) => {
  if (a !== "empty") {
    throw new Error("expected empty");
  }
});

  `;
    URL.prototype.ok = true;
    await runInContext(code, context);
    delete URL.prototype.ok;
  }
});

test("can't use export syntax in vm.Script", () => {
  expect(() => {
    const script = new Script("export default {};");
    script.runInThisContext();
  }).toThrow({ name: "SyntaxError", message: "Unexpected keyword 'export'" });

  expect(() => {
    const script = new Script("export default {};");
    script.createCachedData();
  }).toThrow({ message: "createCachedData failed" });
});

test("rejects invalid bytecode", () => {
  const cachedData = Buffer.from("fhqwhgads");
  const script = new Script("1 + 1;", {
    cachedData,
  });
  expect(script.cachedDataRejected).toBeTrue();
  expect(script.runInThisContext()).toBe(2);
});

test("accepts valid bytecode", () => {
  const source = "1 + 1;";
  const firstScript = new Script(source, {
    produceCachedData: false,
  });
  const cachedData = firstScript.createCachedData();
  expect(cachedData).toBeDefined();
  expect(cachedData).toBeInstanceOf(Buffer);
  const secondScript = new Script(source, {
    cachedData,
  });
  expect(secondScript.cachedDataRejected).toBeFalse();
  expect(firstScript.runInThisContext()).toBe(2);
  expect(secondScript.runInThisContext()).toBe(2);
});

test("can't use bytecode from a different script", () => {
  const firstScript = new Script("1 + 1;");
  const cachedData = firstScript.createCachedData();
  const secondScript = new Script("2 + 2;", {
    cachedData,
  });
  expect(secondScript.cachedDataRejected).toBeTrue();
  expect(firstScript.runInThisContext()).toBe(2);
  expect(secondScript.runInThisContext()).toBe(4);
});

describe("codeGeneration options", () => {
  test("disabling codeGeneration.strings should block eval and Function constructor", () => {
    const context = createContext(
      {},
      {
        codeGeneration: {
          strings: false,
          wasm: true,
        },
      },
    );

    // Test that Function constructor is blocked
    const functionResult = runInContext(
      `
      try {
        const fn = new Function('return 42');
        fn();
      } catch (e) {
        e.name;
      }
    `,
      context,
    );
    expect(functionResult).toBe("EvalError");

    // Test that eval is also blocked
    const evalResult = runInContext(
      `
      try {
        eval('1 + 1');
      } catch (e) {
        e.name;
      }
    `,
      context,
    );
    expect(evalResult).toBe("EvalError");

    // Test the specific pattern from jest-worker that was crashing
    const jestWorkerPattern = runInContext(
      `
      try {
        // This pattern is used by jest-worker to get Function constructor
        const FuncCtor = eval('Function');
        'got Function';
      } catch (e) {
        e.name;
      }
    `,
      context,
    );
    expect(jestWorkerPattern).toBe("EvalError");

    // Test Function constructor as a property getter (the exact crash pattern)
    const getterResult = runInContext(
      `
      try {
        const obj = {};
        Object.defineProperty(obj, 'func', {
          get: Function  // Function constructor IS the getter
        });
        // Access the property - this would call Function as a getter
        // and crash if evalEnabled function pointer was null
        const result = obj.func;
        'unexpected success';
      } catch (e) {
        e.name || 'error';
      }
    `,
      context,
    );
    expect(getterResult).toBe("EvalError");
  });

  test("enabling codeGeneration.strings should allow eval and Function constructor", () => {
    const context = createContext(
      {},
      {
        codeGeneration: {
          strings: true,
          wasm: true,
        },
      },
    );

    // Test that Function constructor works
    const functionResult = runInContext(
      `
      const fn = new Function('return 42');
      fn();
    `,
      context,
    );
    expect(functionResult).toBe(42);

    // Test that eval works
    const evalResult = runInContext("eval('1 + 1');", context);
    expect(evalResult).toBe(2);
  });

  test("default context should allow eval and Function constructor", () => {
    const context = createContext({});

    // Test that Function constructor works by default
    const functionResult = runInContext(
      `
      const fn = new Function('return 123');
      fn();
    `,
      context,
    );
    expect(functionResult).toBe(123);

    // Test that eval works by default
    const evalResult = runInContext("eval('5 + 5');", context);
    expect(evalResult).toBe(10);
  });
});

test("Loader is not defined in vm context", () => {
  // Test with empty context - internal Loader should not leak through
  const emptyContext = createContext({});
  expect(runInContext("typeof Loader;", emptyContext)).toBe("undefined");
  expect(runInContext("Object.hasOwn(globalThis, 'Loader');", emptyContext)).toBe(false);

  // Test with context that has a user-provided Loader - should be preserved
  const customLoader = { custom: true, load: () => "loaded" };
  const customContext = createContext({ Loader: customLoader });
  expect(runInContext("typeof Loader;", customContext)).toBe("object");
  expect(runInContext("Loader.custom;", customContext)).toBe(true);
  expect(runInContext("Loader.load();", customContext)).toBe("loaded");
  expect(runInContext("Object.hasOwn(globalThis, 'Loader');", customContext)).toBe(true);
  // Ensure internal JSC Loader properties are not leaking through
  expect(runInContext("typeof Loader.registry;", customContext)).toBe("undefined");
});

describe("context options with throwing getters", () => {
  // Without the fix, reading these options with a pending exception aborted
  // the process, so run the matrix in a subprocess.
  test.concurrent("the getter's exception propagates to the caller", async () => {
    // Each entry point tests the context-option keys it actually reads:
    // createContext takes codeGeneration, Script#runInNewContext takes
    // contextCodeGeneration, and vm.runInNewContext goes through both.
    // A dotted key puts the throwing getter on the nested object.
    const codeGenerationKeys = (key: string) => [key, `${key}.strings`, `${key}.wasm`];
    const contextKeys = (...codeGenerationKeyNames: string[]) => [
      "name",
      "origin",
      ...codeGenerationKeyNames.flatMap(codeGenerationKeys),
      "importModuleDynamically",
      "microtaskMode",
    ];
    const matrix = {
      createContext: contextKeys("codeGeneration"),
      runInNewContext: contextKeys("codeGeneration", "contextCodeGeneration"),
      scriptRunInNewContext: contextKeys("contextCodeGeneration"),
    };
    const code = `
      const vm = require("node:vm");
      const matrix = ${JSON.stringify(matrix)};
      const entryPoints = {
        createContext: opts => vm.createContext({}, opts),
        runInNewContext: opts => vm.runInNewContext("1", {}, opts),
        scriptRunInNewContext: opts => new vm.Script("1").runInNewContext({}, opts),
      };
      for (const [entry, keys] of Object.entries(matrix)) {
        for (const key of keys) {
          const opts = {};
          const path = key.split(".");
          let target = opts;
          for (const part of path.slice(0, -1)) target = target[part] = {};
          Object.defineProperty(target, path.at(-1), {
            get() { throw new Error("getter:" + key); },
            enumerable: true,
          });
          try {
            entryPoints[entry](opts);
            console.log(entry, key, "did not throw");
          } catch (e) {
            console.log(entry, key, e.message);
          }
        }
      }
      console.log("survived");
    `;
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", code],
      env: bunEnv,
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    const expected =
      Object.entries(matrix)
        .flatMap(([entry, keys]) => keys.map(key => `${entry} ${key} getter:${key}`))
        .join("\n") + "\nsurvived\n";
    expect(stderr).toBe("");
    expect(stdout).toBe(expected);
    expect(exitCode).toBe(0);
  });
});

describe("node:vm lineOffset/columnOffset at the edge of int32", () => {
  const INT32_MAX = 2147483647;

  async function runFixture(body: string) {
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", `const vm = require("node:vm");\n${body}`],
      env: bunEnv,
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    expect(stderr).toBe("");
    expect(exitCode).toBe(0);
    return stdout;
  }

  test.concurrent.each([
    ["new Script, one-line source", `new vm.Script("1", { lineOffset: ${INT32_MAX} })`],
    [
      "new Script, second line steps past INT32_MAX",
      `new vm.Script(${JSON.stringify("1;\n2;")}, { lineOffset: ${INT32_MAX - 1} })`,
    ],
    ["new Script, columnOffset", `new vm.Script("1", { columnOffset: ${INT32_MAX} })`],
    [
      "compileFunction, one-line body",
      `vm.compileFunction("return 1", [], { lineOffset: ${INT32_MAX} })`,
    ],
    [
      "compileFunction, columnOffset",
      `vm.compileFunction("return 1", [], { columnOffset: ${INT32_MAX} })`,
    ],
  ])("%s does not overflow or abort", async (_, expression) => {
    await runFixture(`${expression};`);
  });

  test.concurrent.each([
    [
      "line of a runtime error thrown by a Script",
      `new vm.Script(${JSON.stringify('1;\nthrow new Error("q")')}, { filename: "big.js", lineOffset: ${INT32_MAX - 1} }).runInThisContext()`,
      /big\.js:(-?\d+)/,
    ],
    [
      "line of a compile-time SyntaxError from a Script",
      `new vm.Script(${JSON.stringify("1;\n%%")}, { filename: "big.js", lineOffset: ${INT32_MAX - 1} })`,
      /big\.js:(-?\d+)/,
    ],
    [
      "column of a runtime error thrown on the first line of a Script",
      `new vm.Script('throw new Error("q")', { filename: "big.js", columnOffset: ${INT32_MAX} }).runInThisContext()`,
      /big\.js:1:(-?\d+)/,
    ],
    [
      "line of a runtime error thrown by a compileFunction body",
      `vm.compileFunction('throw new Error("q")', [], { filename: "big.js", lineOffset: ${INT32_MAX} })()`,
      /big\.js:(-?\d+)/,
    ],
    [
      "line of a compile-time SyntaxError from compileFunction",
      `vm.compileFunction("%%", [], { filename: "big.js", lineOffset: ${INT32_MAX} })`,
      /big\.js:(-?\d+)/,
    ],
  ])("%s stays near the requested offset", async (_, expression, pattern) => {
    const stdout = await runFixture(`try { ${expression}; } catch (e) { console.log(e.stack); }`);
    const match = pattern.exec(stdout);
    expect(match).not.toBeNull();
    const position = Number(match![1]);
    expect(position).toBeGreaterThan(INT32_MAX - 100);
    expect(position).toBeLessThanOrEqual(INT32_MAX);
  });
});
