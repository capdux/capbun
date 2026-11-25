import { describe, expect, jest, test } from "bun:test";

test("error.cause", () => {
  const err = new Error("error 1");
  const err2 = new Error("error 2", { cause: err });
  expect(
    Bun.inspect(err2)
      .replaceAll("\\", "/")
      .replaceAll(import.meta.dir.replaceAll("\\", "/"), "[dir]"),
  ).toMatchInlineSnapshot(`
"1 | import { describe, expect, jest, test } from "bun:test";
2 | 
3 | test("error.cause", () => {
4 |   const err = new Error("error 1");
5 |   const err2 = new Error("error 2", { cause: err });
                       ^
error: error 2
      at <anonymous> ([dir]/inspect-error.test.js:5:20)

1 | import { describe, expect, jest, test } from "bun:test";
2 | 
3 | test("error.cause", () => {
4 |   const err = new Error("error 1");
                      ^
error: error 1
      at <anonymous> ([dir]/inspect-error.test.js:4:19)
"
`);
});

test("Error", () => {
  const err = new Error("my message");
  expect(
    Bun.inspect(err)
      .replaceAll("\\", "/")
      .replaceAll(import.meta.dir.replaceAll("\\", "/"), "[dir]"),
  ).toMatchInlineSnapshot(`
"27 | "
28 | \`);
29 | });
30 | 
31 | test("Error", () => {
32 |   const err = new Error("my message");
                       ^
error: my message
      at <anonymous> ([dir]/inspect-error.test.js:32:19)
"
`);
});

test("BuildMessage", async () => {
  try {
    await import("./inspect-error-fixture-bad.js");
    expect.unreachable();
  } catch (e) {
    expect(
      Bun.inspect(e)
        .replaceAll("\\", "/")
        .replaceAll(import.meta.dir.replaceAll("\\", "/"), "[dir]"),
    ).toMatchInlineSnapshot(`
"2 | const duplicateConstDecl = 456;
          ^
error: "duplicateConstDecl" has already been declared
    at [dir]/inspect-error-fixture-bad.js:2:7

1 | const duplicateConstDecl = 123;
          ^
note: "duplicateConstDecl" was originally declared here
   at [dir]/inspect-error-fixture-bad.js:1:7"
`);
  }
});

const normalizeError = str => {
  // remove debug-only stack trace frames
  // like "at require (:1:21)"
  if (str.includes(" (:")) {
    const splits = str.split("\n");
    for (let i = 0; i < splits.length; i++) {
      if (splits[i].includes(" (:")) {
        splits.splice(i, 1);
        i--;
      }
    }
    return splits.join("\n");
  }

  return str;
};

test("Error inside minified file (no color) ", () => {
  try {
    require("./inspect-error-fixture.min.js");
    expect.unreachable();
  } catch (e) {
    expect(
      normalizeError(
        Bun.inspect(e, { colors: false })
          .replaceAll("\\", "/")
          .replaceAll(import.meta.dir.replaceAll("\\", "/"), "[dir]")
          .trim(),
      ),
    ).toMatchInlineSnapshot(`
      "21 | exports.__SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED=Z;
      22 | exports.cache=function(a){return function(){var b=U.current;if(!b)return a.apply(null,arguments);var c=b.getCacheForType(V);b=c.get(a);void 0===b&&(b=W(),c.set(a,b));c=0;for(var f=arguments.length;c<f;c++){var d=arguments[c];if("function"===typeof d||"object"===typeof d&&null!==d){var e=b.o;null===e&&(b.o=e=new WeakMap);b=e.get(d);void 0===b&&(b=W(),e.set(d,b))}else e=b.p,null===e&&(b.p=e=new Map),b=e.get(d),void 0===b&&(b=W(),e.set(d,b))}if(1===b.s)return b.v;if(2===b.s)throw b.v;try{var g=a.apply(null,
      23 | arguments);c=b;c.s=1;return c.v=g}catch(h){throw g=b,g.s=2,g.v=h,h;}}};
      24 | exports.cloneElement=function(a,b,c){if(null===a||void 0===a)throw Error("React.cloneElement(...): The argument must be a React element, but you passed "+a+".");var f=C({},a.props),d=a.key,e=a.ref,g=a._owner;if(null!=b){void 0!==b.ref&&(e=b.ref,g=K.current);void 0!==b.key&&(d=""+b.key);if(a.type&&a.type.defaultProps)var h=a.type.defaultProps;for(k in b)J.call(b,k)&&!L.hasOwnProperty(k)&&(f[k]=void 0===b[k]&&void 0!==h?h[k]:b[k])}var k=arguments.length-2;if(1===k)f.children=c;else if(1<k){h=Array(k);
      25 | for(var m=0;m<k;m++)h[m]=arguments[m+2];f.children=h}return{$$typeof:l,type:a.type,key:d,ref:e,props:f,_owner:g}};exports.createContext=function(a){a={$$typeof:u,_currentValue:a,_currentValue2:a,_threadCount:0,Provider:null,Consumer:null,_defaultValue:null,_globalName:null};a.Provider={$$typeof:t,_context:a};return a.Consumer=a};exports.createElement=M;exports.createFactory=function(a){var b=M.bind(null,a);b.type=a;return b};exports.createRef=function(){return{current:null}};
      26 | exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};expo

      error: error inside long minified file!
            at <anonymous> ([dir]/inspect-error-fixture.min.js:26:2850)
            at <anonymous> ([dir]/inspect-error-fixture.min.js:26:2890)
            at <anonymous> ([dir]/inspect-error.test.js:92:7)"
    `);
  }
});

test("Error inside minified file (color) ", () => {
  try {
    require("./inspect-error-fixture.min.js");
    expect.unreachable();
  } catch (e) {
    expect(
      // TODO: remove this workaround once snapshots work better
      normalizeError(
        Bun.stripANSI(Bun.inspect(e, { colors: true }))
          .replaceAll("\\", "/")
          .replaceAll(import.meta.dir.replaceAll("\\", "/"), "[dir]")
          .trim(),
      ),
    ).toMatchInlineSnapshot(`
      "21 | exports.__SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED=Z;
      22 | exports.cache=function(a){return function(){var b=U.current;if(!b)return a.apply(null,arguments);var c=b.getCacheForType(V);b=c.get(a);void 0===b&&(b=W(),c.set(a,b));c=0;for(var f=arguments.length;c<f;c++){var d=arguments[c];if("function"===typeof d||"object"===typeof d&&null!==d){var e=b.o;null===e&&(b.o=e=new WeakMap);b=e.get(d);void 0===b&&(b=W(),e.set(d,b))}else e=b.p,null===e&&(b.p=e=new Map),b=e.get(d),void 0===b&&(b=W(),e.set(d,b))}if(1===b.s)return b.v;if(2===b.s)throw b.v;try{var g=a.apply(null,
      23 | arguments);c=b;c.s=1;return c.v=g}catch(h){throw g=b,g.s=2,g.v=h,h;}}};
      24 | exports.cloneElement=function(a,b,c){if(null===a||void 0===a)throw Error("React.cloneElement(...): The argument must be a React element, but you passed "+a+".");var f=C({},a.props),d=a.key,e=a.ref,g=a._owner;if(null!=b){void 0!==b.ref&&(e=b.ref,g=K.current);void 0!==b.key&&(d=""+b.key);if(a.type&&a.type.defaultProps)var h=a.type.defaultProps;for(k in b)J.call(b,k)&&!L.hasOwnProperty(k)&&(f[k]=void 0===b[k]&&void 0!==h?h[k]:b[k])}var k=arguments.length-2;if(1===k)f.children=c;else if(1<k){h=Array(k);
      25 | for(var m=0;m<k;m++)h[m]=arguments[m+2];f.children=h}return{$$typeof:l,type:a.type,key:d,ref:e,props:f,_owner:g}};exports.createContext=function(a){a={$$typeof:u,_currentValue:a,_currentValue2:a,_threadCount:0,Provider:null,Consumer:null,_defaultValue:null,_globalName:null};a.Provider={$$typeof:t,_context:a};return a.Consumer=a};exports.createElement=M;exports.createFactory=function(a){var b=M.bind(null,a);b.type=a;return b};exports.createRef=function(){return{current:null}};
      26 | exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};exports.forwardRef=function(a){return{$$typeof:v,render:a}};expo | ... truncated 

      error: error inside long minified file!
            at <anonymous> ([dir]/inspect-error-fixture.min.js:26:2850)
            at <anonymous> ([dir]/inspect-error-fixture.min.js:26:2890)
            at <anonymous> ([dir]/inspect-error.test.js:120:7)"
    `);
  }
});

test("Inserted originalLine and originalColumn do not appear in node:util.inspect", () => {
  const err = new Error("my message");
  expect(
    require("util")
      .inspect(err)
      .replaceAll("\\", "/")
      .replaceAll(import.meta.path.replaceAll("\\", "/"), "[file]"),
  ).toMatchInlineSnapshot(`
"Error: my message
    at <anonymous> ([file]:149:19)"
`);
});

describe("observable properties", () => {
  for (let property of ["sourceURL", "line", "column"]) {
    test(`${property} is observable`, () => {
      const mock = jest.fn();
      const err = new Error("my message");
      Object.defineProperty(err, property, {
        get: mock,
        enumerable: true,
        configurable: true,
      });
      expect(mock).not.toHaveBeenCalled();
      Bun.inspect(err);
      expect(mock).not.toHaveBeenCalled();
    });
  }
});

test("error.stack throwing an error doesn't lead to a crash", () => {
  const err = new Error("my message");
  Object.defineProperty(err, "stack", {
    get: () => {
      throw new Error("my message");
    },
    enumerable: true,
    configurable: true,
  });
  expect(() => {
    throw err;
  }).toThrow();
});

// The printer replaces an AggregateError with the members of its `errors`
// property. When there is nothing to walk it has to print the AggregateError
// itself. A deleted `errors` used to crash the process (the empty value was
// passed to the iteration, which read it as a cell at address 0), an accessor
// was passed to it as well, the other shapes printed nothing, and a
// non-iterable `errors` made console.error throw.
describe.concurrent("AggregateError whose errors cannot be walked", () => {
  async function run(cmd, files) {
    using dir = tempDir("inspect-aggregate-error", files ?? {});
    await using proc = Bun.spawn({
      cmd: [bunExe(), ...cmd],
      cwd: String(dir),
      env: bunEnv,
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    return { stdout, stderr, exitCode };
  }

  const header = "AggregateError: outer message";
  const make = 'const e = new AggregateError([new Error("inner")], "outer message");';
  const shapes = {
    "errors was deleted": "delete e.errors;",
    "errors is an empty array": "e.errors = [];",
    "errors is not iterable": "e.errors = {};",
    "errors is null": "e.errors = null;",
    "errors is a primitive": "e.errors = 1;",
    "the errors getter throws": 'Object.defineProperty(e, "errors", { get() { throw new Error("getter"); } });',
  };

  for (const [name, shape] of Object.entries(shapes)) {
    test(`console.error: ${name}`, async () => {
      const { stdout, stderr, exitCode } = await run([
        "-e",
        `${make} ${shape} console.error(e); console.log("after");`,
      ]);
      expect(stderr).toContain(header);
      expect(stdout).toBe("after\n");
      expect(exitCode).toBe(0);
    });

    test(`uncaught: ${name}`, async () => {
      const { stderr, exitCode } = await run(["-e", `${make} ${shape} throw e;`]);
      expect(stderr).toContain(header);
      expect(exitCode).toBe(1);
    });
  }

  test("Bun.inspect: errors was deleted", async () => {
    const { stdout, stderr, exitCode } = await run([
      "-e",
      `${make} delete e.errors; console.log(JSON.stringify(Bun.inspect(e)));`,
    ]);
    expect(JSON.parse(stdout)).toContain(header);
    expect(stderr).toBe("");
    expect(exitCode).toBe(0);
  });

  test("errors is an accessor: the members it returns are printed", async () => {
    const { stdout, stderr, exitCode } = await run([
      "-e",
      `${make}
       Object.defineProperty(e, "errors", { get() { return [new Error("from the getter")]; } });
       console.error(e);
       console.log("after");`,
    ]);
    expect(stderr).toContain("error: from the getter");
    expect(stderr).not.toContain(header);
    expect(stdout).toBe("after\n");
    expect(exitCode).toBe(0);
  });

  test("members are still printed in place of the AggregateError", async () => {
    const { stderr, exitCode } = await run([
      "-e",
      'throw new AggregateError([new Error("first member"), new TypeError("second member")], "outer message");',
    ]);
    expect(stderr).toContain("error: first member");
    expect(stderr).toContain("TypeError: second member");
    expect(stderr).not.toContain(header);
    expect(exitCode).toBe(1);
  });

  // Without any user code: a module with two or more build errors rejects its
  // first load with an AggregateError of BuildMessages. JSC settles every later
  // load of it from the module registry with a copy made by
  // JSModuleLoader::duplicateError, which keeps the error type and message but
  // not the `errors` property.
  const broken = {
    "broken.ts": "export function f() {\n  const v = {b: {},),r,};\n}\n",
  };

  test("the error a second load of a module that failed to build rejects with", async () => {
    const { stdout, stderr, exitCode } = await run(["main.ts"], {
      ...broken,
      "main.ts": `
        const seen = [];
        for (let i = 0; i < 2; i++) {
          try {
            await import("./broken.ts");
          } catch (e) {
            seen.push(e.constructor.name);
            if (i === 1) console.error(e);
          }
        }
        console.log(JSON.stringify(seen));
      `,
    });
    expect(stdout).toBe('["AggregateError","AggregateError"]\n');
    expect(stderr).toContain("broken.ts");
    expect(exitCode).toBe(0);
  });

  // https://github.com/oven-sh/bun/issues/36963
  test("bun test: two files import a module that failed to build", async () => {
    const importer = 'import { f } from "./broken.ts";\nf();\n';
    const { stderr, exitCode } = await run(["test", "./a.test.ts", "./b.test.ts"], {
      ...broken,
      "a.test.ts": importer,
      "b.test.ts": importer,
    });
    expect(stderr).toContain("a.test.ts:");
    expect(stderr).toContain("b.test.ts:");
    expect(stderr).toContain("across 2 files");
    expect(exitCode).toBe(1);
  });
});

describe("error.code is a String object that has no primitive value", () => {
  const codes = {
    "toString throws": `Object.assign(new String("E_X"), { toString() { throw new Error("toString threw"); } })`,
    "no prototype": `Object.setPrototypeOf(new String("E_X"), null)`,
    "Symbol.toPrimitive returns an object": `Object.assign(new String("E_X"), { [Symbol.toPrimitive]() { return {}; } })`,
  };

  async function run(source) {
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", source],
      env: bunEnv,
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    return { stdout, stderr, exitCode, signalCode: proc.signalCode };
  }

  describe.each(Object.entries(codes))("%s", (_, code) => {
    test.concurrent("Bun.inspect and console.error return to the caller", async () => {
      const { stdout, stderr, exitCode, signalCode } = await run(`
        const e = new Error("boom");
        e.code = ${code};
        try { Bun.inspect(e); } catch {}
        try { console.error(e); } catch {}
        console.log("survived");
      `);
      expect(stderr).toContain("error: boom");
      expect({ stdout, exitCode, signalCode }).toEqual({ stdout: "survived\n", exitCode: 0, signalCode: null });
    });

    test.concurrent.each([
      ["an uncaught throw", "throw e;"],
      ["an unhandled rejection", "Promise.reject(e);"],
    ])("%s prints the error", async (_, raise) => {
      const { stdout, stderr, exitCode, signalCode } = await run(`
        const e = new Error("boom");
        e.code = ${code};
        ${raise}
      `);
      expect(stderr).toContain("error: boom");
      expect({ stdout, exitCode, signalCode }).toEqual({ stdout: "", exitCode: 1, signalCode: null });
    });
  });

  test("a String object that has a primitive value is printed", () => {
    const e = new Error("boom");
    e.code = new String("E_X");
    expect(Bun.inspect(e)).toContain(`code: "E_X"`);
  });
});
