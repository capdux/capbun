// Hardcoded module "node:util"
const types = require("node:util/types");
/** @type {import('node-inspect-extracted')} */
const utl = require("internal/util/inspect");
const { promisify } = require("internal/promisify");
const { validateString, validateOneOf } = require("internal/validators");
const { MIMEType, MIMEParams } = require("internal/util/mime");
const { deprecate } = require("internal/util/deprecate");
const { kResistStopPropagation } = require("internal/shared");

const internalErrorName = $newZigFunction("node_util_binding.zig", "internalErrorName", 1);
const parseEnv = $newZigFunction("node_util_binding.zig", "parseEnv", 1);

const NumberIsSafeInteger = Number.isSafeInteger;
const ObjectKeys = Object.keys;

var cjs_exports;

function isBuffer(value) {
  return Buffer.isBuffer(value);
}
function isFunction(value) {
  return typeof value === "function";
}

const deepEquals = Bun.deepEquals;
const isDeepStrictEqual = (a, b) => deepEquals(a, b, true);

const parseArgs = $newZigFunction("parse_args.zig", "parseArgs", 1);

const inspect = utl.inspect;
const formatWithOptions = utl.formatWithOptions;
const format = utl.format;
const stripVTControlCharacters = utl.stripVTControlCharacters;

var debugs = {};
var debugEnvRegex = /^$/;
if (process.env.NODE_DEBUG) {
  debugEnv = process.env.NODE_DEBUG;
  debugEnv = debugEnv
    .replace(/[|\\{}()[\]^$+?.]/g, "\\$&")
    .replace(/\*/g, ".*")
    .replace(/,/g, "$|^")
    .toUpperCase();
  debugEnvRegex = new RegExp("^" + debugEnv + "$", "i");
}
var debugEnv;
function debuglog(set) {
  set = set.toUpperCase();
  if (!debugs[set]) {
    if (debugEnvRegex.test(set)) {
      var pid = process.pid;
      debugs[set] = function () {
        var msg = format.$apply(cjs_exports, arguments);
        console.error("%s %d: %s", set, pid, msg);
      };
    } else {
      debugs[set] = function () {};
    }
  }
  return debugs[set];
}

function isBoolean(arg) {
  return typeof arg === "boolean";
}

function isNull(arg) {
  return arg === null;
}

function isNullOrUndefined(arg) {
  return arg == null;
}

function isNumber(arg) {
  return typeof arg === "number";
}

function isString(arg) {
  return typeof arg === "string";
}

function isSymbol(arg) {
  return typeof arg === "symbol";
}
function isUndefined(arg) {
  return arg === void 0;
}
var isRegExp = types.isRegExp;
function isObject(arg) {
  return typeof arg === "object" && arg !== null;
}
var isDate = types.isDate;
var isError = $newCppFunction("NodeUtilTypesModule.cpp", "jsFunctionIsError", 1);
function isPrimitive(arg) {
  return (
    arg === null ||
    typeof arg === "boolean" ||
    typeof arg === "number" ||
    typeof arg === "string" ||
    typeof arg === "symbol" ||
    typeof arg === "undefined"
  );
}
function pad(n) {
  return n < 10 ? "0" + n.toString(10) : n.toString(10);
}
var months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
function timestamp() {
  var d = new Date();
  var time = [pad(d.getHours()), pad(d.getMinutes()), pad(d.getSeconds())].join(":");
  return [d.getDate(), months[d.getMonth()], time].join(" ");
}
var log = function log() {
  console.log("%s - %s", timestamp(), format.$apply(cjs_exports, arguments));
};
var inherits = function inherits(ctor, superCtor) {
  if (ctor === undefined || ctor === null) {
    throw $ERR_INVALID_ARG_TYPE("ctor", "function", ctor);
  }

  if (superCtor === undefined || superCtor === null) {
    throw $ERR_INVALID_ARG_TYPE("superCtor", "function", superCtor);
  }

  if (superCtor.prototype === undefined) {
    throw $ERR_INVALID_ARG_TYPE("superCtor.prototype", "object", superCtor.prototype);
  }
  Object.defineProperty(ctor, "super_", {
    // @ts-ignore
    __proto__: null,
    value: superCtor,
    writable: true,
    configurable: true,
  });
  Object.setPrototypeOf(ctor.prototype, superCtor.prototype);
};
var _extend = function (origin, add) {
  if (!add || !isObject(add)) return origin;
  var keys = Object.keys(add);
  var i = keys.length;
  while (i--) {
    origin[keys[i]] = add[keys[i]];
  }
  return origin;
};

function callbackifyOnRejected(reason, cb) {
  if (!reason) {
    var newReason = new Error("Promise was rejected with a falsy value");
    newReason.reason = reason;
    newReason.code = "ERR_FALSY_VALUE_REJECTION";
    reason = newReason;
  }
  return cb(reason);
}
function callbackify(original) {
  const { validateFunction } = require("internal/validators");
  validateFunction(original, "original");

  // We DO NOT return the promise as it gives the user a false sense that
  // the promise is actually somehow related to the callback's execution
  // and that the callback throwing will reject the promise.
  function callbackified(...args) {
    const maybeCb = Array.prototype.pop.$call(args);
    validateFunction(maybeCb, "last argument");
    const cb = Function.prototype.bind.$call(maybeCb, this);
    // In true node style we process the callback on `nextTick` with all the
    // implications (stack, `uncaughtException`, `async_hooks`)
    original.$apply(this, args).then(
      ret => process.nextTick(cb, null, ret),
      rej => process.nextTick(callbackifyOnRejected, rej, cb),
    );
  }

  const descriptors = Object.getOwnPropertyDescriptors(original);
  // It is possible to manipulate a functions `length` or `name` property. This
  // guards against the manipulation.
  if (typeof descriptors.length.value === "number") {
    descriptors.length.value++;
  }
  if (typeof descriptors.name.value === "string") {
    descriptors.name.value += "Callbackified";
  }
  const propertiesValues = Object.values(descriptors);
  for (let i = 0; i < propertiesValues.length; i++) {
    // We want to use null-prototype objects to not rely on globally mutable
    // %Object.prototype%.
    Object.setPrototypeOf(propertiesValues[i], null);
  }
  Object.defineProperties(callbackified, descriptors);
  return callbackified;
}
var toUSVString = input => {
  return (input + "").toWellFormed();
};

function getFormatCodes(key) {
  if (key === "none") return null;
  const named = inspect.colors[key];
  if (named != null) return [`\u001b[${named[0]}m`, `\u001b[${named[1]}m`];
  if (typeof key === "string" && key.length > 0 && key[0] === "#") {
    let r, g, b;
    if (key.length === 7 && /^#[0-9a-fA-F]{6}$/.test(key)) {
      r = Number.parseInt(key.slice(1, 3), 16);
      g = Number.parseInt(key.slice(3, 5), 16);
      b = Number.parseInt(key.slice(5, 7), 16);
      return [`\u001b[38;2;${r};${g};${b}m`, "\u001b[39m"];
    }
    if (key.length === 4 && /^#[0-9a-fA-F]{3}$/.test(key)) {
      r = Number.parseInt(key[1], 16) * 17;
      g = Number.parseInt(key[2], 16) * 17;
      b = Number.parseInt(key[3], 16) * 17;
      return [`\u001b[38;2;${r};${g};${b}m`, "\u001b[39m"];
    }
  }
  validateOneOf(key, "format", ObjectKeys(inspect.colors));
}

function styleText(format, text) {
  validateString(text, "text");

  if ($isJSArray(format)) {
    let left = "";
    let right = "";
    let out = text;
    for (const key of format) {
      const codes = getFormatCodes(key);
      if (codes == null) continue;
      if (out.includes(codes[1])) {
        out = out.replaceAll(codes[1], codes[0]);
      }
      left += codes[0];
      right = `${codes[1]}${right}`;
    }

    return `${left}${out}${right}`;
  }

  const codes = getFormatCodes(format);
  if (codes == null) return text;
  let out = text;
  if (out.includes(codes[1])) {
    out = out.replaceAll(codes[1], codes[0]);
  }
  return `${codes[0]}${out}${codes[1]}`;
}

function getSystemErrorName(err: any) {
  if (typeof err !== "number") throw $ERR_INVALID_ARG_TYPE("err", "number", err);
  if (err >= 0 || !NumberIsSafeInteger(err)) throw $ERR_OUT_OF_RANGE("err", "a negative integer", err);
  return internalErrorName(err);
}

let lazyAbortedRegistry: FinalizationRegistry<{
  ref: WeakRef<AbortSignal>;
  unregisterToken: (...args: any[]) => void;
}>;
function onAbortedCallback(resolveFn: Function) {
  lazyAbortedRegistry.unregister(resolveFn);

  resolveFn();
}

function aborted(signal: AbortSignal, resource: object) {
  if (!$isObject(signal) || !(signal instanceof AbortSignal)) {
    throw $ERR_INVALID_ARG_TYPE("signal", "AbortSignal", signal);
  }

  if (!$isObject(resource)) {
    throw $ERR_INVALID_ARG_TYPE("resource", "object", resource);
  }

  if (signal.aborted) {
    return Promise.$resolve();
  }

  const { promise, resolve } = $newPromiseCapability(Promise);
  const unregisterToken = () => onAbortedCallback(resolve);
  signal.addEventListener(
    "abort",
    // Do not leak the current scope into the listener.
    // Instead, create a new function.
    unregisterToken,
    { once: true, [kResistStopPropagation]: true },
  );

  if (!lazyAbortedRegistry) {
    lazyAbortedRegistry = new FinalizationRegistry(({ ref, unregisterToken }) => {
      const signal = ref.deref();
      if (signal) signal.removeEventListener("abort", unregisterToken);
    });
  }

  // When the resource is garbage collected, clear the listener from the
  // AbortSignal so we do not cause the AbortSignal itself to leak (AbortSignal
  // keeps alive until it is signaled).
  lazyAbortedRegistry.register(
    resource,
    {
      ref: new WeakRef(signal),
      unregisterToken,
    },
    unregisterToken,
  );

  return promise;
}

cjs_exports = {
  // This is in order of `node --print 'Object.keys(util)'`
  // _errnoException,
  // _exceptionWithHostPort,
  _extend,
  callbackify,
  debug: debuglog,
  debuglog,
  deprecate,
  format,
  styleText,
  formatWithOptions,
  // getCallSite,
  // getCallSites,
  // getSystemErrorMap,
  getSystemErrorName,
  // getSystemErrorMessage,
  inherits,
  inspect,
  isDeepStrictEqual,
  promisify,
  stripVTControlCharacters,
  toUSVString,
  // transferableAbortSignal,
  // transferableAbortController,
  aborted,
  types,
  parseEnv,
  parseArgs,
  TextDecoder,
  TextEncoder,
  MIMEType,
  MIMEParams,

  // Deprecated in Node.js 22, removed in 23
  isArray: $isArray,
  isBoolean,
  isBuffer,
  isNull,
  isNullOrUndefined,
  isNumber,
  isString,
  isSymbol,
  isUndefined,
  isRegExp,
  isObject,
  isDate,
  isError,
  isFunction,
  isPrimitive,
  log,
};

export default cjs_exports;
