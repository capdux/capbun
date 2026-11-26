// Hardcoded module "node:async_hooks"
// Bun is only going to implement AsyncLocalStorage and AsyncResource (partial).
// The other functions are deprecated anyways, and would impact performance too much.
// API: https://nodejs.org/api/async_hooks.html
//
// JSC has been patched to include a special global variable $asyncContext which is set to
// a constant InternalFieldTuple<[AsyncContextData, never]>. `get` and `set` read/write to the
// first element of this tuple. Inside of PromiseOperations.js, we "snapshot" the context (store it
// in the promise reaction) and then just before we call .then, we restore it.
//
// This means context tracking is *kind-of* manual. If we receive a callback in native code
// - In Zig, call jsValue.withAsyncContextIfNeeded(); which returns another JSValue. Store that and
//   then run .$call() on it later.
// - In C++, call AsyncContextFrame::withAsyncContextIfNeeded(jsValue). Then to call it,
//   use AsyncContextFrame:: call(...) instead of JSC:: call.
//
// The above functions will return the same JSFunction if the context is empty, and there are many
// other checks to ensure that AsyncLocalStorage has virtually no impact on performance when not in
// use. But the nature of this approach makes the implementation *itself* very low-impact on performance.
//
// AsyncContextData is an immutable array managed in here, formatted [key, value, key, value] where
// each key is an AsyncLocalStorage object and the value is the associated value. There are a ton of
// calls to $assert which will verify this invariant (only during bun-debug)
//
const setAsyncHooksEnabled = $newCppFunction("NodeAsyncHooks.cpp", "jsSetAsyncHooksEnabled", 1);
const cleanupLater = $newCppFunction("NodeAsyncHooks.cpp", "jsCleanupLater", 0);
const { validateFunction, validateString, validateObject } = require("internal/validators");

const ObjectIs = Object.is;

// Only run during debug
function assertValidAsyncContextArray(frame: unknown): frame is Map<AsyncLocalStorage, any> | undefined {
  // undefined is OK
  if (frame === undefined) return true;
  // Otherwise, it must be a Map
  $assert(
    frame instanceof Map,
    "AsyncContextData must be a Map or undefined, got",
    Bun.inspect(frame, { depth: 1 }),
  );
  return true;
}

// Only run during debug
function debugFormatContextValue(value: Map<AsyncLocalStorage, any> | undefined) {
  if (value === undefined) return "undefined";
  let str = "{\n";
  for (const [als, store] of value) {
    str += `  ${(als as any).__id__}: typeof = ${typeof store}\n`;
  }
  str += "}";
  return str;
}

function get(): Map<AsyncLocalStorage, any> | undefined {
  $debug("get", debugFormatContextValue($getInternalField($asyncContext, 0)));
  return $getInternalField($asyncContext, 0);
}

function set(contextValue: Map<AsyncLocalStorage, any> | undefined) {
  $assert(assertValidAsyncContextArray(contextValue));
  $debug("set", debugFormatContextValue(contextValue));
  return $putInternalField($asyncContext, 0, contextValue);
}

class AsyncLocalStorage {
  #defaultValue = undefined;
  #name = "";

  constructor(options?: { defaultValue?: any; name?: string }) {
    if (options !== undefined) {
      validateObject(options, "options");
      this.#defaultValue = options.defaultValue;
      if (options.name !== undefined) {
        this.#name = `${options.name}`;
      }
    }
    setAsyncHooksEnabled(true);

    // In debug mode assign every AsyncLocalStorage a unique ID
    if (IS_BUN_DEVELOPMENT) {
      const uid = Math.random().toString(36).slice(2, 8);
      const source = require("bun:jsc").callerSourceOrigin();

      (this as any).__id__ = uid + "@" + require("node:path").basename(source);

      $debug("new AsyncLocalStorage uid=", (this as any).__id__, source);
    }
  }

  get name() {
    return this.#name;
  }

  static bind(fn, ...args: any) {
    validateFunction(fn, "fn");
    return this.snapshot().bind(null, fn, ...args);
  }

  static snapshot() {
    var context = get();
    return (fn, ...args) => {
      var prev = get();
      set(context);
      try {
        return fn(...args);
      } finally {
        set(prev);
      }
    };
  }

  enterWith(store) {
    cleanupLater();
    const context = get();
    const next = new Map(context);
    next.set(this, store);
    set(next);
    $assert(ObjectIs(this.getStore(), store));
  }

  exit(cb, ...args) {
    return this.run(undefined, cb, ...args);
  }

  #restoreAfterScope(hadPrior: boolean, prior: any) {
    const current = get();
    if (hadPrior) {
      const restored = new Map(current);
      restored.set(this, prior);
      set(restored);
    } else if (current !== undefined) {
      if (current.has(this)) {
        if (current.size === 1) {
          set(undefined);
        } else {
          const restored = new Map(current);
          restored.delete(this);
          set(restored);
        }
      } else if (current.size === 0) {
        set(undefined);
      }
    }
  }

  withScope(store_value) {
    const context = get();
    const hadPrior = context !== undefined && context.has(this);
    const prior = hadPrior ? context.get(this) : this.#defaultValue;
    if (ObjectIs(store_value, prior)) {
      return {
        [Symbol.dispose]() {},
      };
    }
    const next = new Map(context);
    next.set(this, store_value);
    set(next);
    return {
      [Symbol.dispose]: () => {
        this.#restoreAfterScope(hadPrior, prior);
      },
    };
  }

  // This function is litered with $asserts to ensure that everything that
  // is assumed to be true is *actually* true.
  run(store_value, callback, ...args) {
    $debug("run " + (this as any).__id__);
    const context = get();
    const hadPrior = context !== undefined && context.has(this);
    const prior = hadPrior ? context.get(this) : this.#defaultValue;
    if (ObjectIs(store_value, prior)) {
      return callback(...args);
    }
    const next = new Map(context);
    next.set(this, store_value);
    set(next);
    $assert(ObjectIs(this.getStore(), store_value), "run: store_value was not set");
    try {
      return callback(...args);
    } finally {
      this.#restoreAfterScope(hadPrior, prior);
    }
  }

  disable() {
    $debug("disable " + (this as any).__id__);
    const context = get();
    if (context !== undefined && context.delete(this) && context.size === 0) {
      set(undefined);
    }
  }

  getStore() {
    $debug("getStore " + (this as any).__id__);
    const context = get();
    if (context !== undefined && context.has(this)) {
      return context.get(this);
    }
    return this.#defaultValue;
  }

  // Node.js internal function. In Bun's implementation, calling this is not
  // observable from outside the AsyncLocalStorage implementation.
  _enable() {}

  // Node.js internal function. In Bun's implementation, calling this is not
  // observable from outside the AsyncLocalStorage implementation.
  _propagate(_resource, _triggerResource, _type) {}
}

if (IS_BUN_DEVELOPMENT) {
  AsyncLocalStorage.prototype[Bun.inspect.custom] = function (depth, options) {
    if (depth < 0) return `AsyncLocalStorage { ${Bun.inspect((this as any).__id__, options)} }`;
    return `AsyncLocalStorage { [${options.stylize("debug id", "special")}]: ${Bun.inspect(
      (this as any).__id__,
      options,
    )} }`;
  };
}

class AsyncResource {
  type;
  #snapshot;

  constructor(type, opts?) {
    validateString(type, "type");

    let triggerAsyncId = opts;
    if (opts != null) {
      if (typeof opts !== "number") {
        triggerAsyncId = opts.triggerAsyncId === undefined ? 1 : opts.triggerAsyncId;
      }
      if (!Number.isSafeInteger(triggerAsyncId) || triggerAsyncId < -1) {
        throw $ERR_INVALID_ASYNC_ID("triggerAsyncId", triggerAsyncId);
      }
    }
    if (hasEnabledCreateHook && type.length === 0) {
      throw $ERR_ASYNC_TYPE(type);
    }

    setAsyncHooksEnabled(true);
    this.type = type;
    this.#snapshot = get();
  }

  emitBefore() {
    return true;
  }

  emitAfter() {
    return true;
  }

  asyncId() {
    return 0;
  }

  triggerAsyncId() {
    return 0;
  }

  emitDestroy() {
    //
  }

  runInAsyncScope(fn, thisArg, ...args) {
    var prev = get();
    set(this.#snapshot);
    try {
      return fn.$apply(thisArg, args);
    } finally {
      set(prev);
    }
  }

  bind(fn, thisArg) {
    validateFunction(fn, "fn");
    const bound = this.runInAsyncScope.bind(this, fn, thisArg ?? this);
    Object.defineProperty(bound, "length", {
      __proto__: null,
      value: fn.length,
      configurable: true,
    } as PropertyDescriptor);
    return bound;
  }

  static bind(fn, type, thisArg) {
    type = type || fn.name;
    return new AsyncResource(type || "bound-anonymous-fn").bind(fn, thisArg);
  }
}

// The rest of async_hooks is not implemented and is stubbed with no-ops and warnings.

function createWarning(message, isCreateHook?: boolean) {
  let warned = false;
  var wrapped = function (arg1?) {
    if (warned || (!Bun.env.BUN_FEATURE_FLAG_VERBOSE_WARNINGS && (warned = true))) return;

    const known_supported_modules = [
      // the following do not actually need async_hooks to work properly
      "zx/build/core.js",
      "datadog-core/src/storage/async_resource.js",
    ];
    const e = new Error().stack!;
    if (known_supported_modules.some(m => e.includes(m))) return;
    if (isCreateHook && arg1) {
      // this block is to specifically filter out react-server, which is often
      // times bundled into a framework or application. Their use defines three
      // handlers which are all TODO stubs. for more info see this comment:
      // https://github.com/oven-sh/bun/issues/13866#issuecomment-2397896065
      if (typeof arg1 === "object") {
        const { init, promiseResolve, destroy } = arg1;
        if (init && promiseResolve && destroy) {
          if (isEmptyFunction(init) && isEmptyFunction(destroy)) return;
        }
      }
    }

    warned = true;
    console.warn("[bun] Warning:", message);
  };
  return wrapped;
}

function isEmptyFunction(f: Function) {
  let str = f.toString();
  if (!str.startsWith("function()")) return false;
  str = str.slice("function()".length).trim();
  return /^{\s*}$/.test(str);
}

const createHookNotImpl = createWarning(
  "async_hooks.createHook is not implemented in Bun. Hooks can still be created but will never be called.",
  true,
);

let hasEnabledCreateHook = false;
function createHook(hook) {
  validateObject(hook, "hook");
  const { init, before, after, destroy, promiseResolve } = hook;
  if (init !== undefined && typeof init !== "function") throw $ERR_ASYNC_CALLBACK("hook.init");
  if (before !== undefined && typeof before !== "function") throw $ERR_ASYNC_CALLBACK("hook.before");
  if (after !== undefined && typeof after !== "function") throw $ERR_ASYNC_CALLBACK("hook.after");
  if (destroy !== undefined && typeof destroy !== "function") throw $ERR_ASYNC_CALLBACK("hook.destroy");
  if (promiseResolve !== undefined && typeof promiseResolve !== "function")
    throw $ERR_ASYNC_CALLBACK("hook.promiseResolve");

  return {
    enable() {
      createHookNotImpl(hook);
      hasEnabledCreateHook = true;
      return this;
    },
    disable() {
      createHookNotImpl();
      return this;
    },
  };
}

const executionAsyncIdNotImpl = createWarning(
  "async_hooks.executionAsyncId/triggerAsyncId are not implemented in Bun. It will return 0 every time.",
);
function executionAsyncId() {
  executionAsyncIdNotImpl();
  return 0;
}

function triggerAsyncId() {
  return 0;
}

const executionAsyncResourceWarning = createWarning(
  "async_hooks.executionAsyncResource is not implemented in Bun. It returns a reference to process.stdin every time.",
);
function executionAsyncResource() {
  executionAsyncResourceWarning();
  return process.stdin;
}

const asyncWrapProviders = {
  NONE: 0,
  DIRHANDLE: 1,
  DNSCHANNEL: 2,
  ELDHISTOGRAM: 3,
  FILEHANDLE: 4,
  FILEHANDLECLOSEREQ: 5,
  FIXEDSIZEBLOBCOPY: 6,
  FSEVENTWRAP: 7,
  FSREQCALLBACK: 8,
  FSREQPROMISE: 9,
  GETADDRINFOREQWRAP: 10,
  GETNAMEINFOREQWRAP: 11,
  HEAPSNAPSHOT: 12,
  HTTP2SESSION: 13,
  HTTP2STREAM: 14,
  HTTP2PING: 15,
  HTTP2SETTINGS: 16,
  HTTPINCOMINGMESSAGE: 17,
  HTTPCLIENTREQUEST: 18,
  JSSTREAM: 19,
  JSUDPWRAP: 20,
  MESSAGEPORT: 21,
  PIPECONNECTWRAP: 22,
  PIPESERVERWRAP: 23,
  PIPEWRAP: 24,
  PROCESSWRAP: 25,
  PROMISE: 26,
  QUERYWRAP: 27,
  SHUTDOWNWRAP: 28,
  SIGNALWRAP: 29,
  STATWATCHER: 30,
  STREAMPIPE: 31,
  TCPCONNECTWRAP: 32,
  TCPSERVERWRAP: 33,
  TCPWRAP: 34,
  TTYWRAP: 35,
  UDPSENDWRAP: 36,
  UDPWRAP: 37,
  SIGINTWATCHDOG: 38,
  WORKER: 39,
  WORKERHEAPSNAPSHOT: 40,
  WRITEWRAP: 41,
  ZLIB: 42,
  CHECKPRIMEREQUEST: 43,
  PBKDF2REQUEST: 44,
  KEYPAIRGENREQUEST: 45,
  KEYGENREQUEST: 46,
  KEYEXPORTREQUEST: 47,
  CIPHERREQUEST: 48,
  DERIVEBITSREQUEST: 49,
  HASHREQUEST: 50,
  RANDOMBYTESREQUEST: 51,
  RANDOMPRIMEREQUEST: 52,
  SCRYPTREQUEST: 53,
  SIGNREQUEST: 54,
  TLSWRAP: 55,
  VERIFYREQUEST: 56,
  INSPECTORJSBINDING: 57,
};

export default {
  AsyncLocalStorage,
  createHook,
  executionAsyncId,
  triggerAsyncId,
  executionAsyncResource,
  asyncWrapProviders,
  AsyncResource,
};
