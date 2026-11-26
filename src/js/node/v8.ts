// Hardcoded module "node:v8"

// This is a stub! None of this is actually implemented yet.
const { hideFromStack, throwNotImplemented } = require("internal/shared");
const jsc: typeof import("bun:jsc") = require("bun:jsc");
const { isStringOneByteRepresentation, startGCProfiler, stopGCProfiler, discardGCProfiler } = $cpp(
  "NodeV8.cpp",
  "Bun::createNodeV8Binding",
);

const DateNow = Date.now;

function notimpl(message) {
  throwNotImplemented("node:v8 " + message);
}

const jscSerialize = jsc.serialize;
const jscDeserialize = jsc.deserialize;
const BufferCtor = Buffer;
const BufferPrototype = BufferCtor.prototype;
const BufferIsBuffer = BufferCtor.isBuffer;
const BufferAllocUnsafe = BufferCtor.allocUnsafe;
const Uint8ArrayCtor = Uint8Array;
const Uint8ArrayPrototype = Uint8ArrayCtor.prototype;
const TypedArrayProto = Object.getPrototypeOf(Uint8ArrayPrototype);
const TypedArrayPrototypeSet = TypedArrayProto.set;
const getTypedArrayBuffer = Object.getOwnPropertyDescriptor(TypedArrayProto, "buffer")!.get!;
const getTypedArrayByteOffset = Object.getOwnPropertyDescriptor(TypedArrayProto, "byteOffset")!.get!;
const getTypedArrayByteLength = Object.getOwnPropertyDescriptor(TypedArrayProto, "byteLength")!.get!;
const DataViewProto = DataView.prototype;
const getDataViewBuffer = Object.getOwnPropertyDescriptor(DataViewProto, "buffer")!.get!;
const getDataViewByteOffset = Object.getOwnPropertyDescriptor(DataViewProto, "byteOffset")!.get!;
const getDataViewByteLength = Object.getOwnPropertyDescriptor(DataViewProto, "byteLength")!.get!;
const getArrayBufferByteLength = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, "byteLength")!.get!;
const getSharedArrayBufferByteLength =
  typeof SharedArrayBuffer !== "undefined"
    ? Object.getOwnPropertyDescriptor(SharedArrayBuffer.prototype, "byteLength")!.get!
    : null;
const ObjectSetPrototypeOf = Object.setPrototypeOf;
const ObjectGetPrototypeOf = Object.getPrototypeOf;
const ObjectKeys = Object.keys;
const ArrayIsArray = Array.isArray;
const ArrayBufferIsView = ArrayBuffer.isView;
const SetCtor = Set;
const SetPrototypeHas = SetCtor.prototype.has;
const SetPrototypeAdd = SetCtor.prototype.add;
const SetPrototypeForEach = SetCtor.prototype.forEach;
const getSetSize = Object.getOwnPropertyDescriptor(SetCtor.prototype, "size")!.get!;
const MapPrototypeForEach = Map.prototype.forEach;
const getMapSize = Object.getOwnPropertyDescriptor(Map.prototype, "size")!.get!;
const ReflectApply = Reflect["apply"];

function isMapObject(val: unknown): boolean {
  try {
    ReflectApply(getMapSize, val, []);
    return true;
  } catch {
    return false;
  }
}

function isSetObject(val: unknown): boolean {
  try {
    ReflectApply(getSetSize, val, []);
    return true;
  } catch {
    return false;
  }
}

function collectBuffers(val: unknown, visited: Set<unknown>, buffers: Buffer[]) {
  if (val === null || typeof val !== "object") return;
  if (ReflectApply(SetPrototypeHas, visited, [val])) return;
  ReflectApply(SetPrototypeAdd, visited, [val]);
  if (BufferIsBuffer(val)) {
    $arrayPush(buffers, val);
    return;
  }
  if (ArrayIsArray(val)) {
    for (let i = 0; i < val.length; i++) {
      collectBuffers(val[i], visited, buffers);
    }
    return;
  }
  if (ArrayBufferIsView(val)) return;
  if (isMapObject(val)) {
    ReflectApply(MapPrototypeForEach, val, [
      (v: unknown, k: unknown) => {
        collectBuffers(k, visited, buffers);
        collectBuffers(v, visited, buffers);
      },
    ]);
    return;
  }
  if (isSetObject(val)) {
    ReflectApply(SetPrototypeForEach, val, [
      (v: unknown) => {
        collectBuffers(v, visited, buffers);
      },
    ]);
    return;
  }
  const keys = ObjectKeys(val);
  for (let i = 0; i < keys.length; i++) {
    collectBuffers((val as Record<string, unknown>)[keys[i]], visited, buffers);
  }
}

function toSliceInfo(val: unknown): [ArrayBufferLike, number, number] | null {
  if (ArrayBufferIsView(val)) {
    try {
      return [
        ReflectApply(getTypedArrayBuffer, val, []),
        ReflectApply(getTypedArrayByteOffset, val, []),
        ReflectApply(getTypedArrayByteLength, val, []),
      ];
    } catch {
      return [
        ReflectApply(getDataViewBuffer, val, []),
        ReflectApply(getDataViewByteOffset, val, []),
        ReflectApply(getDataViewByteLength, val, []),
      ];
    }
  }
  try {
    return [val as ArrayBuffer, 0, ReflectApply(getArrayBufferByteLength, val, [])];
  } catch {}
  if (getSharedArrayBufferByteLength) {
    try {
      return [val as SharedArrayBuffer, 0, ReflectApply(getSharedArrayBufferByteLength, val, [])];
    } catch {}
  }
  return null;
}

class Deserializer {
  constructor() {
    notimpl("Deserializer");
  }
}
class Serializer {
  constructor() {
    notimpl("Serializer");
  }
}
class DefaultDeserializer extends Deserializer {}
class DefaultSerializer extends Serializer {}
// JSC manages one undivided heap, so a record carries a single space instead
// of V8's thirteen, and counters JSC does not track are reported as 0 rather
// than invented.
function gcHeapSnapshot(used, capacity, external) {
  const available = capacity > used ? capacity - used : 0;
  return {
    heapStatistics: {
      totalHeapSize: capacity,
      totalHeapSizeExecutable: 0,
      totalPhysicalSize: capacity,
      totalAvailableSize: available,
      usedHeapSize: used,
      heapSizeLimit: 0,
      mallocedMemory: 0,
      peakMallocedMemory: 0,
      externalMemory: external,
      totalGlobalHandlesSize: 0,
      usedGlobalHandlesSize: 0,
    },
    heapSpaceStatistics: [
      {
        spaceName: "heap",
        spaceSize: capacity,
        spaceUsedSize: used,
        spaceAvailableSize: available,
        physicalSpaceSize: capacity,
      },
    ],
  };
}

const kGCProfilerSession = Symbol("kGCProfilerSession");
const kGCProfilerStartTime = Symbol("kGCProfilerStartTime");

// A profiler that is started and then dropped without stop() would otherwise
// leave its native session open for the life of the VM; the registry releases
// it when the wrapper is collected, matching node's BaseObject finalizer.
let gcProfilerRegistry: FinalizationRegistry<number> | undefined;

class GCProfiler {
  constructor() {
    this[kGCProfilerSession] = null;
    this[kGCProfilerStartTime] = 0;
  }

  start() {
    if (this[kGCProfilerSession] !== null) return;
    this[kGCProfilerStartTime] = DateNow();
    const id = startGCProfiler();
    this[kGCProfilerSession] = id;
    (gcProfilerRegistry ??= new FinalizationRegistry(discardGCProfiler)).register(this, id, this);
  }

  stop() {
    const session = this[kGCProfilerSession];
    if (session === null) return undefined;
    this[kGCProfilerSession] = null;
    gcProfilerRegistry!.unregister(this);

    const events = stopGCProfiler(session);
    const statistics = [];
    for (let i = 0; i < events.length; i++) {
      const event = events[i];
      $arrayPush(statistics, {
        // A JavaScriptCore eden collection only scans newly allocated objects,
        // and a full collection sweeps the whole heap, so they line up with
        // V8's minor and major collection types.
        gcType: event.isFullCollection ? "MarkSweepCompact" : "Scavenge",
        cost: event.cost,
        beforeGC: gcHeapSnapshot(event.usedBefore, event.capacityBefore, event.externalBefore),
        afterGC: gcHeapSnapshot(event.usedAfter, event.capacityAfter, event.externalAfter),
      });
    }

    return {
      version: 1,
      startTime: this[kGCProfilerStartTime],
      endTime: DateNow(),
      statistics,
    };
  }

  [Symbol.dispose]() {
    this.stop();
  }
}

function cachedDataVersionTag() {
  notimpl("cachedDataVersionTag");
}
var HeapSnapshotReadable_;
function getHeapSnapshot() {
  if (!HeapSnapshotReadable_) {
    const Readable = require("node:stream").Readable;
    class HeapSnapshotReadable extends Readable {
      constructor() {
        super();
        this.push(Bun.generateHeapSnapshot("v8"));
        this.push(null);
      }
    }
    HeapSnapshotReadable_ = HeapSnapshotReadable;
  }

  return new HeapSnapshotReadable_();
}

let totalmem_ = -1;

function totalmem() {
  if (totalmem_ === -1) {
    totalmem_ = require("node:os").totalmem();
  }
  return totalmem_;
}

function getHeapStatistics() {
  const stats = jsc.heapStats();
  const memory = jsc.memoryUsage();

  // These numbers need to be plausible, even if incorrect
  // From npm's codebase:
  //
  // > static #heapLimit = Math.floor(getHeapStatistics().heap_size_limit)
  //
  return {
    total_heap_size: stats.heapSize,
    total_heap_size_executable: stats.heapSize >> 1,
    total_physical_size: memory.peak,
    total_available_size: totalmem() - stats.heapSize,
    used_heap_size: stats.heapSize,
    heap_size_limit: Math.min(memory.peak * 10, totalmem()),
    malloced_memory: stats.heapSize,
    peak_malloced_memory: memory.peak,

    // -- Copied from Node:
    does_zap_garbage: 0,
    number_of_native_contexts: stats.globalObjectCount,
    number_of_detached_contexts: 0,
    total_global_handles_size: 8192,
    used_global_handles_size: 2208,
    // ---- End of copied from Node

    external_memory: stats.extraMemorySize,
  };
}
function getHeapSpaceStatistics() {
  notimpl("getHeapSpaceStatistics");
}
function getHeapCodeStatistics() {
  notimpl("getHeapCodeStatistics");
}
function setFlagsFromString() {
  notimpl("setFlagsFromString");
}
function deserialize(value) {
  const info = toSliceInfo(value);
  if (!info) return jscDeserialize(value);
  const [buf, off, len] = info;
  const bytes = new Uint8ArrayCtor(buf, off, len);
  if (len < 5 || bytes[0] !== 0xff || bytes[1] !== 0x42 || bytes[2] !== 0x55 || bytes[3] !== 0x4e || bytes[4] !== 0x01) {
    return jscDeserialize(value);
  }
  const payload = new Uint8ArrayCtor(buf, off + 5, len - 5);
  const decoded = jscDeserialize(payload);
  if (!ArrayIsArray(decoded) || decoded.length !== 2 || !ArrayIsArray(decoded[1])) {
    throw new Error("failed to parse serialized buffer envelope");
  }
  const buffers = decoded[1];
  for (let i = 0; i < buffers.length; i++) {
    const entry = buffers[i];
    const proto = entry !== null && typeof entry === "object" ? ObjectGetPrototypeOf(entry) : null;
    if (proto !== Uint8ArrayPrototype && proto !== BufferPrototype) {
      throw new Error("failed to parse serialized buffer envelope");
    }
    ObjectSetPrototypeOf(entry, BufferPrototype);
  }
  return decoded[0];
}
function takeCoverage() {
  notimpl("takeCoverage");
}
function stopCoverage() {
  notimpl("stopCoverage");
}
function serialize(arg1) {
  const buffers: Buffer[] = [];
  collectBuffers(arg1, new SetCtor(), buffers);
  if (buffers.length === 0) {
    return jscSerialize(arg1, { binaryType: "nodebuffer" });
  }
  const payload = jscSerialize([arg1, buffers], { binaryType: "nodebuffer" });
  const len = ReflectApply(getTypedArrayByteLength, payload, []);
  const out = BufferAllocUnsafe(5 + len);
  out[0] = 0xff;
  out[1] = 0x42;
  out[2] = 0x55;
  out[3] = 0x4e;
  out[4] = 0x01;
  ReflectApply(TypedArrayPrototypeSet, out, [payload, 5]);
  return out;
}

function getDefaultHeapSnapshotPath() {
  const date = new Date();

  const worker_threads = require("node:worker_threads");
  const thread_id = worker_threads.threadId;

  const yyyy = date.getFullYear();
  const mm = date.getMonth().toString().padStart(2, "0");
  const dd = date.getDate().toString().padStart(2, "0");
  const hh = date.getHours().toString().padStart(2, "0");
  const MM = date.getMinutes().toString().padStart(2, "0");
  const ss = date.getSeconds().toString().padStart(2, "0");

  // 'Heap-${yyyymmdd}-${hhmmss}-${pid}-${thread_id}.heapsnapshot'
  return `Heap-${yyyy}${mm}${dd}-${hh}${MM}${ss}-${process.pid}-${thread_id}.heapsnapshot`;
}

let fs;

function writeHeapSnapshot(path, _options) {
  if (path !== undefined) {
    if (typeof path !== "string") {
      throw $ERR_INVALID_ARG_TYPE("path", "string", path);
    }

    if (!path) {
      throw $ERR_INVALID_ARG_VALUE("path", path, "must be a non-empty string");
    }
  } else {
    path = getDefaultHeapSnapshotPath();
  }

  if (!fs) {
    fs = require("node:fs");
  }
  fs.writeFileSync(path, Bun.generateHeapSnapshot("v8"), "utf-8");

  return path;
}
function setHeapSnapshotNearHeapLimit() {
  notimpl("setHeapSnapshotNearHeapLimit");
}
const promiseHooks = {
    createHook: () => {
      notimpl("createHook");
    },
    onInit: () => {
      notimpl("onInit");
    },
    onBefore: () => {
      notimpl("onBefore");
    },
    onAfter: () => {
      notimpl("onAfter");
    },
    onSettled: () => {
      notimpl("onSettled");
    },
  },
  startupSnapshot = {
    addDeserializeCallback: () => notimpl("addDeserializeCallback"),
    addSerializeCallback: () => notimpl("addSerializeCallback"),
    setDeserializeMainFunction: () => notimpl("setDeserializeMainFunction"),
    isBuildingSnapshot: () => false,
  };

export default {
  cachedDataVersionTag,
  GCProfiler,
  isStringOneByteRepresentation,
  getHeapSnapshot,
  getHeapStatistics,
  getHeapSpaceStatistics,
  getHeapCodeStatistics,
  setFlagsFromString,
  deserialize,
  takeCoverage,
  stopCoverage,
  serialize,
  writeHeapSnapshot,
  setHeapSnapshotNearHeapLimit,
  promiseHooks,
  startupSnapshot,
  Deserializer,
  Serializer,
  DefaultDeserializer,
  DefaultSerializer,
};

hideFromStack(
  notimpl,
  cachedDataVersionTag,
  getHeapSnapshot,
  getHeapStatistics,
  getHeapSpaceStatistics,
  getHeapCodeStatistics,
  setFlagsFromString,
  deserialize,
  takeCoverage,
  stopCoverage,
  serialize,
  writeHeapSnapshot,
  setHeapSnapshotNearHeapLimit,
  Deserializer,
  Serializer,
  DefaultDeserializer,
  DefaultSerializer,
);
