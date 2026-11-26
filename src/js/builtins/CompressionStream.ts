export function initializeCompressionStream(this, format) {
  const zlib = require("node:zlib");
  const {
    Z_NO_FLUSH,
    Z_FINISH,
    BROTLI_OPERATION_PROCESS,
    BROTLI_OPERATION_FINISH,
    ZSTD_e_continue,
    ZSTD_e_end,
  } = zlib.constants;

  const builders = {
    "deflate": zlib.createDeflate,
    "deflate-raw": zlib.createDeflateRaw,
    "gzip": zlib.createGzip,
    "brotli": zlib.createBrotliCompress,
    "zstd": zlib.createZstdCompress,
  };

  if (typeof format !== "string" || !Object.hasOwn(builders, format)) {
    throw $ERR_INVALID_ARG_VALUE("format", format, "must be one of: " + Object.keys(builders).join(", "));
  }

  const options = arguments.length > 1 ? arguments[1] : undefined;
  if (options !== undefined && (typeof options !== "object" || options === null)) {
    throw $ERR_INVALID_ARG_TYPE("options", "object", options);
  }
  const readableStrategy = options ?? {};
  const highWaterMark = $extractHighWaterMark(readableStrategy, 65536);
  const sizeAlgorithm = $extractSizeAlgorithm(readableStrategy);

  const kDefaultHighWaterMark = 65536;
  const collectAll = highWaterMark === Infinity;
  const outChunkSize = collectAll ? kDefaultHighWaterMark : Math.max(64, highWaterMark);

  const isBrotli = format === "brotli";
  const isZstd = format === "zstd";
  const flushFlag = isBrotli ? BROTLI_OPERATION_PROCESS : isZstd ? ZSTD_e_continue : Z_NO_FLUSH;
  const finishFlushFlag = isBrotli ? BROTLI_OPERATION_FINISH : isZstd ? ZSTD_e_end : Z_FINISH;

  const nodeStream = builders[format]({ chunkSize: outChunkSize });
  const handle = nodeStream._handle;
  let handleClosed = false;
  function closeHandle() {
    if (handleClosed) return;
    handleClosed = true;
    nodeStream._webStreamCallback = null;
    nodeStream.close();
  }

  let pendingResolve = null;
  let pendingReject = null;
  nodeStream._webStreamCallback = (err, availOutAfter, availInAfter) => {
    const resolve = pendingResolve;
    const reject = pendingReject;
    pendingResolve = null;
    pendingReject = null;
    if (err) {
      if (reject) reject(err);
    } else if (resolve) {
      resolve({ availOutAfter, availInAfter });
    }
  };

  function writeStep(flag, input, inOff, inLen, outBuf, outLen) {
    return new Promise((resolve, reject) => {
      pendingResolve = resolve;
      pendingReject = reject;
      handle.write(flag, input, inOff, inLen, outBuf, 0, outLen);
    });
  }

  const closedCapability = $newPromiseCapability(Promise);
  let transform;

  function isCancelledOrErrored() {
    const readable = $getByIdDirectPrivate(transform, "readable");
    return $getByIdDirectPrivate(readable, "state") !== $streamReadable;
  }

  async function runWrite(flag, input) {
    const controller = $getByIdDirectPrivate(transform, "controller");
    let inOff = 0;
    let inLen = input ? input.byteLength : 0;
    const collected = collectAll ? [] : null;
    while (true) {
      if (isCancelledOrErrored()) {
        closeHandle();
        return;
      }
      const outBuf = new Uint8Array(outChunkSize);
      const { availOutAfter, availInAfter } = await writeStep(flag, input, inOff, inLen, outBuf, outChunkSize);
      if (isCancelledOrErrored()) {
        closeHandle();
        return;
      }
      const have = outChunkSize - availOutAfter;
      if (have > 0) {
        const piece = have === outChunkSize ? outBuf : outBuf.subarray(0, have);
        if (collected) {
          collected.push(piece);
        } else {
          $transformStreamDefaultControllerEnqueue(controller, piece);
        }
      }
      const inDelta = inLen - availInAfter;
      if (availOutAfter !== 0) break;
      if (!collected && $getByIdDirectPrivate(transform, "backpressure")) {
        await $getByIdDirectPrivate(transform, "backpressureChangePromise").promise;
        if (isCancelledOrErrored()) {
          closeHandle();
          return;
        }
      }
      inOff += inDelta;
      inLen = availInAfter;
      if (inLen === 0) input = null;
    }
    if (collected && collected.length > 0) {
      const out = collected.length === 1 ? collected[0] : new Uint8Array(Buffer.concat(collected));
      $transformStreamDefaultControllerEnqueue(controller, out);
    }
  }

  const startAlgorithm = () => Promise.$resolve();
  const transformAlgorithm = async chunk => {
    let input;
    if (chunk instanceof ArrayBuffer) {
      input = new Uint8Array(chunk);
    } else if (ArrayBuffer.isView(chunk)) {
      if (chunk.buffer instanceof SharedArrayBuffer) {
        throw $ERR_INVALID_ARG_TYPE("chunk", ["BufferSource"], chunk);
      }
      input = new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    } else {
      throw $ERR_INVALID_ARG_TYPE("chunk", ["BufferSource"], chunk);
    }
    try {
      await runWrite(flushFlag, input);
    } catch (e) {
      closeHandle();
      closedCapability.reject.$call(undefined, e);
      throw e;
    }
  };
  const flushAlgorithm = async () => {
    try {
      await runWrite(finishFlushFlag, null);
      closeHandle();
      closedCapability.resolve.$call();
    } catch (e) {
      closeHandle();
      closedCapability.reject.$call(undefined, e);
      throw e;
    }
  };

  transform = $createTransformStream(
    startAlgorithm,
    transformAlgorithm,
    flushAlgorithm,
    1,
    () => 1,
    highWaterMark,
    sizeAlgorithm,
  );
  const writable = $getByIdDirectPrivate(transform, "writable");
  writable[Symbol.for("nodejs.webstream.isClosedPromise")] = closedCapability;
  writable[Symbol.for("nodejs.webstream.controllerErrorFunction")] = e => {
    closeHandle();
    $transformStreamError(transform, e);
    closedCapability.reject.$call(undefined, e);
  };

  $putByIdDirectPrivate(this, "compressionStreamReadable", $getByIdDirectPrivate(transform, "readable"));
  $putByIdDirectPrivate(this, "compressionStreamWritable", writable);

  return this;
}

$getter;
export function readable(this) {
  const r = $getByIdDirectPrivate(this, "compressionStreamReadable");
  if (!r) throw $makeGetterTypeError("CompressionStream", "readable");
  return r;
}

$getter;
export function writable(this) {
  const w = $getByIdDirectPrivate(this, "compressionStreamWritable");
  if (!w) throw $makeGetterTypeError("CompressionStream", "writable");
  return w;
}
