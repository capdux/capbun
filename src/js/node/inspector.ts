// Hardcoded module "node:inspector" and "node:inspector/promises"
// Profiler and Runtime.consoleAPICalled APIs are implemented; other inspector APIs are stubs.
const { hideFromStack, throwNotImplemented } = require("internal/shared");
const { validateFunction, validateObject, validateString } = require("internal/validators");
const EventEmitter = require("node:events");

const DateNow = Date.now;
const NumberIsNaN = Number.isNaN;
const ObjectHasOwn = Object.hasOwn;
const ObjectIs = Object.is;
const StringCtor = String;

// Native profiler functions exposed via $newCppFunction
const startCPUProfiler = $newCppFunction("JSInspectorProfiler.cpp", "jsFunction_startCPUProfiler", 0);
const stopCPUProfiler = $newCppFunction("JSInspectorProfiler.cpp", "jsFunction_stopCPUProfiler", 0);
const setCPUSamplingInterval = $newCppFunction("JSInspectorProfiler.cpp", "jsFunction_setCPUSamplingInterval", 1);
const isCPUProfilerRunning = $newCppFunction("JSInspectorProfiler.cpp", "jsFunction_isCPUProfilerRunning", 0);

const CONSOLE_METHODS: readonly [string, string][] = [
  ["log", "log"],
  ["debug", "debug"],
  ["info", "info"],
  ["error", "error"],
  ["warn", "warning"],
  ["dir", "dir"],
  ["dirxml", "dirxml"],
  ["table", "table"],
  ["trace", "trace"],
  ["clear", "clear"],
  ["count", "count"],
  ["countReset", "countReset"],
  ["assert", "assert"],
  ["profile", "profile"],
  ["profileEnd", "profileEnd"],
  ["time", "time"],
  ["timeEnd", "timeEnd"],
  ["timeLog", "timeLog"],
  ["group", "startGroup"],
  ["groupCollapsed", "startGroupCollapsed"],
  ["groupEnd", "endGroup"],
];

const runtimeSessions: Session[] = [];
const hookedConsoleOriginals = new Map<string, (...args: any[]) => any>();
const hookedConsoleWrappers = new Map<string, (...args: any[]) => any>();
let consoleHookInstalled = false;
let isEmittingConsoleEvent = false;

function emitInspectorWarning(err: unknown) {
  if (err instanceof Error) {
    process.emitWarning(err);
    return;
  }
  let msg: string;
  try {
    msg = StringCtor(err);
  } catch {
    msg = "Thrown value could not be stringified";
  }
  process.emitWarning(new Error(msg));
}

function serializeConsoleArg(arg: unknown): Record<string, any> {
  if (arg === null) {
    return { type: "object", subtype: "null", value: null };
  }
  switch (typeof arg) {
    case "undefined":
      return { type: "undefined" };
    case "boolean":
      return { type: "boolean", value: arg };
    case "string":
      return { type: "string", value: arg };
    case "number": {
      if (ObjectIs(arg, -0)) {
        return { type: "number", unserializableValue: "-0", description: "-0" };
      }
      if (NumberIsNaN(arg)) {
        return { type: "number", unserializableValue: "NaN", description: "NaN" };
      }
      if (arg === Infinity) {
        return { type: "number", unserializableValue: "Infinity", description: "Infinity" };
      }
      if (arg === -Infinity) {
        return { type: "number", unserializableValue: "-Infinity", description: "-Infinity" };
      }
      return { type: "number", value: arg, description: StringCtor(arg) };
    }
    case "bigint": {
      const str = `${arg}n`;
      return { type: "bigint", unserializableValue: str, description: str };
    }
    case "symbol":
    case "function":
    case "object": {
      let description: string;
      try {
        description = StringCtor(arg);
      } catch (e) {
        emitInspectorWarning(e);
        description = typeof arg === "function" ? "[Function]" : "[object Object]";
      }
      return { type: typeof arg, description };
    }
  }
}

function dispatchConsoleAPICalled(cdpType: string, args: unknown[]) {
  if (isEmittingConsoleEvent || runtimeSessions.length === 0) return;
  isEmittingConsoleEvent = true;
  try {
    const serializedArgs: Record<string, any>[] = new Array(args.length);
    for (let i = 0; i < args.length; i++) {
      serializedArgs[i] = serializeConsoleArg(args[i]);
    }
    const message = {
      method: "Runtime.consoleAPICalled",
      params: {
        type: cdpType,
        args: serializedArgs,
        executionContextId: 1,
        timestamp: DateNow(),
      },
    };
    const sessions = runtimeSessions.slice();
    for (let i = 0; i < sessions.length; i++) {
      const session = sessions[i];
      try {
        session.emit("Runtime.consoleAPICalled", message);
        session.emit("inspectorNotification", message);
      } catch (e) {
        emitInspectorWarning(e);
      }
    }
  } finally {
    isEmittingConsoleEvent = false;
  }
}

function installConsoleHook() {
  if (consoleHookInstalled) return;
  consoleHookInstalled = true;
  const consoleObj = globalThis.console as Record<string, any>;
  if (!consoleObj || typeof consoleObj !== "object") return;

  for (let i = 0; i < CONSOLE_METHODS.length; i++) {
    const [method, cdpType] = CONSOLE_METHODS[i];
    if (!ObjectHasOwn(consoleObj, method)) continue;
    const original = consoleObj[method];
    if (typeof original !== "function") continue;
    const wrapper = function (this: any, ...args: any[]) {
      dispatchConsoleAPICalled(cdpType, args);
      return original.apply(this, args);
    };
    hookedConsoleOriginals.set(method, original);
    hookedConsoleWrappers.set(method, wrapper);
    consoleObj[method] = wrapper;
  }
}

function uninstallConsoleHook() {
  if (!consoleHookInstalled) return;
  consoleHookInstalled = false;
  const consoleObj = globalThis.console as Record<string, any>;
  if (consoleObj && typeof consoleObj === "object") {
    for (let i = 0; i < CONSOLE_METHODS.length; i++) {
      const method = CONSOLE_METHODS[i][0];
      const wrapper = hookedConsoleWrappers.get(method);
      const original = hookedConsoleOriginals.get(method);
      if (wrapper && original && consoleObj[method] === wrapper) {
        consoleObj[method] = original;
      }
    }
  }
  hookedConsoleOriginals.clear();
  hookedConsoleWrappers.clear();
}

function addRuntimeSession(session: Session) {
  for (let i = 0; i < runtimeSessions.length; i++) {
    if (runtimeSessions[i] === session) return;
  }
  runtimeSessions.push(session);
  installConsoleHook();
}

function removeRuntimeSession(session: Session) {
  for (let i = 0; i < runtimeSessions.length; i++) {
    if (runtimeSessions[i] === session) {
      runtimeSessions.splice(i, 1);
      break;
    }
  }
  if (runtimeSessions.length === 0) {
    uninstallConsoleHook();
  }
}

function open() {
  throwNotImplemented("node:inspector", 2445);
}

function close() {
  // No-op when the inspector is not open
}

function url() {
  // Return undefined since that is allowed by the Node.js API
  // https://nodejs.org/api/inspector.html#inspectorurl
  return undefined;
}

function waitForDebugger() {
  const err: any = new Error("Inspector is not active");
  err.code = "ERR_INSPECTOR_NOT_ACTIVE";
  throw err;
}

class Session extends EventEmitter {
  #connected = false;
  #profilerEnabled = false;
  #runtimeEnabled = false;

  connect() {
    if (this.#connected) {
      const err: any = new Error("The inspector session is already connected");
      err.code = "ERR_INSPECTOR_ALREADY_CONNECTED";
      throw err;
    }
    this.#connected = true;
  }

  connectToMainThread() {
    if (Bun.isMainThread) {
      const err: any = new Error("Cannot connect to main thread from main thread");
      err.code = "ERR_INSPECTOR_NOT_WORKER";
      throw err;
    }
    this.connect();
  }

  disconnect() {
    if (!this.#connected) return;
    if (isCPUProfilerRunning()) stopCPUProfiler();
    if (this.#runtimeEnabled) {
      this.#runtimeEnabled = false;
      removeRuntimeSession(this);
    }
    this.#profilerEnabled = false;
    this.#connected = false;
  }

  post(
    method: string,
    params?: object | ((err: Error | null, result?: any) => void),
    callback?: (err: Error | null, result?: any) => void,
  ) {
    validateString(method, "method");
    // Handle overloaded signature: post(method, callback) only when no third argument is passed
    if (arguments.length < 3 && typeof params === "function") {
      callback = params;
      params = undefined;
    } else if (params !== undefined) {
      validateObject(params, "params");
    }
    if (callback !== undefined) {
      validateFunction(callback, "callback");
    }

    if (!this.#connected) {
      const error: any = new Error("Session is not connected");
      error.code = "ERR_INSPECTOR_NOT_CONNECTED";
      if (callback) {
        queueMicrotask(() => callback(error));
        return;
      }
      throw error;
    }

    const result = this.#handleMethod(method, params as object | undefined);

    if (callback) {
      // Callback API - async
      queueMicrotask(() => {
        if (result instanceof Error) {
          callback(result, undefined);
        } else {
          callback(null, result);
        }
      });
    } else {
      // Sync throw for errors when no callback
      if (result instanceof Error) {
        throw result;
      }
      return result;
    }
  }

  #handleMethod(method: string, params?: object): any {
    switch (method) {
      case "Runtime.enable":
        if (!this.#runtimeEnabled) {
          this.#runtimeEnabled = true;
          addRuntimeSession(this);
        }
        return {};

      case "Runtime.disable":
        if (this.#runtimeEnabled) {
          this.#runtimeEnabled = false;
          removeRuntimeSession(this);
        }
        return {};

      case "Profiler.enable":
        this.#profilerEnabled = true;
        return {};

      case "Profiler.disable":
        if (isCPUProfilerRunning()) {
          stopCPUProfiler();
        }
        this.#profilerEnabled = false;
        return {};

      case "Profiler.start":
        if (!this.#profilerEnabled) return new Error("Profiler is not enabled. Call Profiler.enable first.");
        if (!isCPUProfilerRunning()) startCPUProfiler();
        return {};

      case "Profiler.stop":
        if (!isCPUProfilerRunning()) return new Error("Profiler is not started. Call Profiler.start first.");
        try {
          return { profile: JSON.parse(stopCPUProfiler()) };
        } catch (e) {
          return new Error(`Failed to parse profile JSON: ${e}`);
        }

      case "Profiler.setSamplingInterval": {
        if (isCPUProfilerRunning()) return new Error("Cannot change sampling interval while profiler is running");
        const interval = (params as any)?.interval;
        if (typeof interval !== "number" || interval <= 0) return new Error("interval must be a positive number");
        setCPUSamplingInterval(interval);
        return {};
      }

      case "Profiler.getBestEffortCoverage":
      case "Profiler.startPreciseCoverage":
      case "Profiler.stopPreciseCoverage":
      case "Profiler.takePreciseCoverage":
        return new Error("Coverage APIs are not supported");

      default: {
        const err: any = new Error(`Inspector method "${method}" is not supported`);
        err.code = "ERR_INSPECTOR_COMMAND";
        return err;
      }
    }
  }
}

const console = {
  ...globalThis.console,
  context: {
    console: globalThis.console,
  },
};

export default {
  console,
  open,
  close,
  url,
  waitForDebugger,
  Session,
};

hideFromStack(open, close, url, waitForDebugger, Session.prototype.constructor);
