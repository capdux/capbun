// Hardcoded module "node:perf_hooks"
const { throwNotImplemented } = require("internal/shared");
const { validateFunction, validateObject } = require("internal/validators");
const ObjectHasOwn = Object.hasOwn;

const cppCreateHistogram = $newCppFunction("JSNodePerformanceHooksHistogram.cpp", "jsFunction_createHistogram", 3) as (
  min: number,
  max: number,
  figures: number,
) => import("node:perf_hooks").RecordableHistogram;

var {
  Performance,
  PerformanceEntry,
  PerformanceMark,
  PerformanceMeasure,
  PerformanceObserver,
  PerformanceObserverEntryList,
} = globalThis;

var constants = {
  NODE_PERFORMANCE_ENTRY_TYPE_DNS: 4,
  NODE_PERFORMANCE_ENTRY_TYPE_GC: 0,
  NODE_PERFORMANCE_ENTRY_TYPE_HTTP: 1,
  NODE_PERFORMANCE_ENTRY_TYPE_HTTP2: 2,
  NODE_PERFORMANCE_ENTRY_TYPE_NET: 3,
  NODE_PERFORMANCE_GC_FLAGS_ALL_AVAILABLE_GARBAGE: 16,
  NODE_PERFORMANCE_GC_FLAGS_ALL_EXTERNAL_MEMORY: 32,
  NODE_PERFORMANCE_GC_FLAGS_CONSTRUCT_RETAINED: 2,
  NODE_PERFORMANCE_GC_FLAGS_FORCED: 4,
  NODE_PERFORMANCE_GC_FLAGS_NO: 0,
  NODE_PERFORMANCE_GC_FLAGS_SCHEDULE_IDLE: 64,
  NODE_PERFORMANCE_GC_FLAGS_SYNCHRONOUS_PHANTOM_PROCESSING: 8,
  NODE_PERFORMANCE_GC_INCREMENTAL: 8,
  NODE_PERFORMANCE_GC_MAJOR: 4,
  NODE_PERFORMANCE_GC_MINOR: 1,
  NODE_PERFORMANCE_GC_WEAKCB: 16,
  NODE_PERFORMANCE_MILESTONE_BOOTSTRAP_COMPLETE: 7,
  NODE_PERFORMANCE_MILESTONE_ENVIRONMENT: 2,
  NODE_PERFORMANCE_MILESTONE_LOOP_EXIT: 6,
  NODE_PERFORMANCE_MILESTONE_LOOP_START: 5,
  NODE_PERFORMANCE_MILESTONE_NODE_START: 3,
  NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN_TIMESTAMP: 0,
  NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN: 1,
  NODE_PERFORMANCE_MILESTONE_V8_START: 4,
};

// PerformanceEntry is not a valid constructor, so we have to fake it.
class PerformanceNodeTiming {
  bootstrapComplete: number = 0;
  environment: number = 0;
  idleTime: number = 0;
  loopExit: number = 0;
  loopStart: number = 0;
  nodeStart: number = 0;
  v8Start: number = 0;

  // we have to fake the properties since it's not real
  get name() {
    return "node";
  }

  get entryType() {
    return "node";
  }

  get startTime() {
    return this.nodeStart;
  }

  get duration() {
    return performance.now();
  }

  toJSON() {
    return {
      name: this.name,
      entryType: this.entryType,
      startTime: this.startTime,
      duration: this.duration,
      bootstrapComplete: this.bootstrapComplete,
      environment: this.environment,
      idleTime: this.idleTime,
      loopExit: this.loopExit,
      loopStart: this.loopStart,
      nodeStart: this.nodeStart,
      v8Start: this.v8Start,
    };
  }
}
$toClass(PerformanceNodeTiming, "PerformanceNodeTiming", PerformanceEntry);

function createPerformanceNodeTiming() {
  const object = Object.create(PerformanceNodeTiming.prototype);

  object.bootstrapComplete = object.environment = object.nodeStart = object.v8Start = performance.timeOrigin;
  object.loopStart = object.idleTime = 1;
  object.loopExit = -1;
  return object;
}

function eventLoopUtilization(_utilization1, _utilization2) {
  return {
    idle: 0,
    active: 0,
    utilization: 0,
  };
}

// PerformanceEntry is not a valid constructor, so we have to fake it.
class PerformanceResourceTiming {
  constructor() {
    throwNotImplemented("PerformanceResourceTiming");
  }
}
$toClass(PerformanceResourceTiming, "PerformanceResourceTiming", PerformanceEntry);

class PerformanceNodeEntry {
  name: string;
  entryType: string;
  startTime: number;
  duration: number;
  detail: any;

  constructor(name: string, entryType: string, startTime: number, duration: number, detail?: any) {
    this.name = name;
    this.entryType = entryType;
    this.startTime = startTime;
    this.duration = duration;
    this.detail = detail;
    if (entryType === "function" && Array.isArray(detail)) {
      for (let i = 0; i < detail.length; i++) {
        this[i] = detail[i];
      }
    }
  }

  toJSON() {
    return {
      name: this.name,
      entryType: this.entryType,
      startTime: this.startTime,
      duration: this.duration,
      detail: this.detail,
    };
  }
}
Object.setPrototypeOf(PerformanceNodeEntry.prototype, PerformanceEntry.prototype);
Object.setPrototypeOf(PerformanceNodeEntry, PerformanceEntry);

const kObserverCallback = Symbol("kObserverCallback");
const kObserverTypes = Symbol("kObserverTypes");
const functionObservers = new Set<any>();

const origObserve = PerformanceObserver.prototype.observe;
PerformanceObserver.prototype.observe = function observe(options: any) {
  if (options && typeof options === "object") {
    const types = Array.isArray(options.entryTypes)
      ? options.entryTypes
      : typeof options.type === "string"
        ? [options.type]
        : null;
    if (types && types.includes("function")) {
      this[kObserverTypes] = new Set(types);
      functionObservers.add(this);
      const webTypes = types.filter((t: string) => t !== "function");
      if (webTypes.length === 0) {
        return;
      }
      options = Array.isArray(options.entryTypes) ? { ...options, entryTypes: webTypes } : options;
    }
  }
  return origObserve.$call(this, options);
};

const origDisconnect = PerformanceObserver.prototype.disconnect;
PerformanceObserver.prototype.disconnect = function disconnect() {
  functionObservers.delete(this);
  return origDisconnect.$call(this);
};

function notifyFunctionObservers(entry: PerformanceNodeEntry) {
  if (functionObservers.size === 0) return;
  queueMicrotask(() => {
    for (const observer of functionObservers) {
      const cb = observer[kObserverCallback];
      if (typeof cb === "function") {
        const list = {
          getEntries: () => [entry],
          getEntriesByName: (name: string, type?: string) =>
            entry.name === name && (!type || entry.entryType === type) ? [entry] : [],
          getEntriesByType: (type: string) => (entry.entryType === type ? [entry] : []),
        };
        cb.$call(observer, list, observer);
      }
    }
  });
}

// Intercept PerformanceObserver construction to capture callback for "function" entries
const OrigPerformanceObserver = PerformanceObserver;
const WrappedPerformanceObserver = function PerformanceObserver(this: any, callback: any) {
  const obs = new OrigPerformanceObserver(callback);
  obs[kObserverCallback] = callback;
  return obs;
} as unknown as typeof PerformanceObserver;
WrappedPerformanceObserver.prototype = OrigPerformanceObserver.prototype;
Object.setPrototypeOf(WrappedPerformanceObserver, OrigPerformanceObserver);
PerformanceObserver = WrappedPerformanceObserver;

function timerify<T extends (...args: any[]) => any>(fn: T, options?: { histogram?: any }): T {
  validateFunction(fn, "fn");
  let histogram: any;
  if (options !== undefined) {
    validateObject(options, "options");
    if (ObjectHasOwn(options, "histogram")) {
      histogram = options.histogram;
      if (histogram !== undefined) {
        validateObject(histogram, "options.histogram");
        if (typeof histogram.record !== "function") {
          throw $ERR_INVALID_ARG_TYPE("options.histogram", "RecordableHistogram", histogram);
        }
      }
    }
  }

  function timerified(this: any, ...args: any[]) {
    const start = performance.now();
    let result: any;
    let isAsync = false;
    try {
      result = new.target ? Reflect.construct(fn, args, new.target) : fn.$apply(this, args);
      if (result && typeof result.then === "function") {
        isAsync = true;
        return result.then(
          (val: any) => {
            const duration = performance.now() - start;
            if (histogram) {
              histogram.record(Math.max(1, Math.round(duration * 1e6)));
            }
            notifyFunctionObservers(new PerformanceNodeEntry(fn.name || "anonymous", "function", start, duration, args));
            return val;
          },
          (err: any) => {
            const duration = performance.now() - start;
            if (histogram) {
              histogram.record(Math.max(1, Math.round(duration * 1e6)));
            }
            notifyFunctionObservers(new PerformanceNodeEntry(fn.name || "anonymous", "function", start, duration, args));
            throw err;
          },
        );
      }
      return result;
    } finally {
      if (!isAsync) {
        const duration = performance.now() - start;
        if (histogram) {
          histogram.record(Math.max(1, Math.round(duration * 1e6)));
        }
        notifyFunctionObservers(new PerformanceNodeEntry(fn.name || "anonymous", "function", start, duration, args));
      }
    }
  }

  Object.defineProperties(timerified, {
    __proto__: null,
    length: { __proto__: null, value: fn.length, writable: false, enumerable: false, configurable: true },
    name: {
      __proto__: null,
      value: `timerified ${fn.name || "anonymous"}`,
      writable: false,
      enumerable: false,
      configurable: true,
    },
  } as PropertyDescriptorMap);

  return timerified as unknown as T;
}

export default {
  performance: {
    mark(_) {
      return performance.mark(...arguments);
    },
    measure(_) {
      return performance.measure(...arguments);
    },
    clearMarks(_) {
      return performance.clearMarks(...arguments);
    },
    clearMeasures(_) {
      return performance.clearMeasures(...arguments);
    },
    getEntries(_) {
      return performance.getEntries(...arguments);
    },
    getEntriesByName(_) {
      return performance.getEntriesByName(...arguments);
    },
    getEntriesByType(_) {
      return performance.getEntriesByType(...arguments);
    },
    setResourceTimingBufferSize(_) {
      return performance.setResourceTimingBufferSize(...arguments);
    },
    timeOrigin: performance.timeOrigin,
    toJSON(_) {
      return performance.toJSON(...arguments);
    },
    onresourcetimingbufferfull: performance.onresourcetimingbufferfull,
    nodeTiming: createPerformanceNodeTiming(),
    now: () => performance.now(),
    eventLoopUtilization: eventLoopUtilization,
    clearResourceTimings: function () {},
    timerify,
  },
  timerify,
  eventLoopUtilization,
  // performance: {
  //   clearMarks: [Function: clearMarks],
  //   clearMeasures: [Function: clearMeasures],
  //   clearResourceTimings: [Function: clearResourceTimings],
  //   getEntries: [Function: getEntries],
  //   getEntriesByName: [Function: getEntriesByName],
  //   getEntriesByType: [Function: getEntriesByType],
  //   mark: [Function: mark],
  //   measure: [Function: measure],
  //   now: performance.now,
  //   setResourceTimingBufferSize: [Function: setResourceTimingBufferSize],
  //   timeOrigin: performance.timeOrigin,
  //   toJSON: [Function: toJSON],
  //   onresourcetimingbufferfull: [Getter/Setter]
  // },
  constants,
  Performance,
  PerformanceEntry,
  PerformanceMark,
  PerformanceMeasure,
  PerformanceObserver,
  PerformanceObserverEntryList,
  PerformanceNodeTiming,
  monitorEventLoopDelay: function monitorEventLoopDelay(options?: { resolution?: number }) {
    const impl = require("internal/perf_hooks/monitorEventLoopDelay");
    return impl(options);
  },
  createHistogram: function createHistogram(options?: {
    lowest?: number | bigint;
    highest?: number | bigint;
    figures?: number;
  }): import("node:perf_hooks").RecordableHistogram {
    const opts = options || {};

    let lowest = 1;
    let highest = Number.MAX_SAFE_INTEGER;
    let figures = 3;

    if (ObjectHasOwn(opts, "lowest") && opts.lowest !== undefined) {
      if (typeof opts.lowest === "bigint") {
        lowest = Number(opts.lowest);
      } else if (typeof opts.lowest === "number") {
        lowest = opts.lowest;
      } else {
        throw $ERR_INVALID_ARG_TYPE("options.lowest", ["number", "bigint"], opts.lowest);
      }
    }

    if (ObjectHasOwn(opts, "highest") && opts.highest !== undefined) {
      if (typeof opts.highest === "bigint") {
        highest = Number(opts.highest);
      } else if (typeof opts.highest === "number") {
        highest = opts.highest;
      } else {
        throw $ERR_INVALID_ARG_TYPE("options.highest", ["number", "bigint"], opts.highest);
      }
    }

    if (ObjectHasOwn(opts, "figures") && opts.figures !== undefined) {
      if (typeof opts.figures !== "number") {
        throw $ERR_INVALID_ARG_TYPE("options.figures", "number", opts.figures);
      }
      if (opts.figures < 1 || opts.figures > 5) {
        throw $ERR_OUT_OF_RANGE("options.figures", ">= 1 && <= 5", opts.figures);
      }
      figures = opts.figures;
    }

    // Node.js validation - highest must be >= 2 * lowest
    if (lowest < 1) {
      throw $ERR_OUT_OF_RANGE("options.lowest", ">= 1 && <= 9007199254740991", lowest);
    }

    if (highest < 2 * lowest) {
      throw $ERR_OUT_OF_RANGE("options.highest", `>= ${2 * lowest} && <= 9007199254740991`, highest);
    }

    return cppCreateHistogram(lowest, highest, figures);
  },
  PerformanceResourceTiming,
};
