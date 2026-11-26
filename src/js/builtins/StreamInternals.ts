/*
 * Copyright (C) 2015 Canon Inc.
 * Copyright (C) 2015 Igalia.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// @internal

export function markPromiseAsHandled(promise: Promise<unknown>) {
  $assert($isPromise(promise));
  $putPromiseInternalField(
    promise,
    $promiseFieldFlags,
    $getPromiseInternalField(promise, $promiseFieldFlags) | $promiseFlagsIsHandled,
  );
}

export function shieldingPromiseResolve(result) {
  const promise = Promise.$resolve(result);
  if (promise.$then === undefined) promise.$then = $Promise.prototype.$then;
  return promise;
}

export function promiseInvokeOrNoopMethodNoCatch(object, method, args) {
  if (method === undefined) return Promise.$resolve();
  return $shieldingPromiseResolve(method.$apply(object, args));
}

export function promiseInvokeOrNoopNoCatch(object, key, args) {
  return $promiseInvokeOrNoopMethodNoCatch(object, object[key], args);
}

export function promiseInvokeOrNoopMethod(object, method, args) {
  try {
    return $promiseInvokeOrNoopMethodNoCatch(object, method, args);
  } catch (error) {
    return Promise.$reject(error);
  }
}

export function promiseInvokeOrNoop(object, key, args) {
  try {
    return $promiseInvokeOrNoopNoCatch(object, key, args);
  } catch (error) {
    return Promise.$reject(error);
  }
}

export function promiseInvokeOrFallbackOrNoop(object, key1, args1, key2, args2) {
  try {
    const method = object[key1];
    if (method === undefined) return $promiseInvokeOrNoopNoCatch(object, key2, args2);
    return $shieldingPromiseResolve(method.$apply(object, args1));
  } catch (error) {
    return Promise.$reject(error);
  }
}

export function validateAndNormalizeQueuingStrategy(size, highWaterMark) {
  if (size !== undefined && typeof size !== "function") throw new TypeError("size parameter must be a function");

  const newHighWaterMark = $toNumber(highWaterMark);

  if (newHighWaterMark !== newHighWaterMark || newHighWaterMark < 0)
    throw new RangeError("highWaterMark value is negative or not a number");

  return { size: size, highWaterMark: newHighWaterMark };
}

import type Dequeue from "internal/fifo";
$linkTimeConstant;
export function createFIFO<T>(): Dequeue<T> {
  const Dequeue = require("internal/fifo");
  return new Dequeue();
}

export function newQueue() {
  return { content: $createFIFO(), size: 0 };
}

export function dequeueValue(queue) {
  const record = queue.content.shift();
  queue.size -= record.size;
  // As described by spec, below case may occur due to rounding errors.
  if (queue.size < 0) queue.size = 0;
  return record.value;
}

export function enqueueValueWithSize(queue, value, size) {
  size = $toNumber(size);
  if (!isFinite(size) || size < 0) throw new RangeError("size has an incorrect value");

  queue.content.push({ value, size });
  queue.size += size;
}

export function peekQueueValue(queue) {
  return queue.content.peek()?.value;
}

export function resetQueue(queue) {
  $assert("content" in queue);
  $assert("size" in queue);
  queue.content.clear();
  queue.size = 0;
}

export function extractSizeAlgorithm(strategy) {
  const sizeAlgorithm = strategy.size;

  if (sizeAlgorithm === undefined) return () => 1;

  if (typeof sizeAlgorithm !== "function") throw new TypeError("strategy.size must be a function");

  return chunk => {
    return sizeAlgorithm(chunk);
  };
}

export function extractHighWaterMark(strategy, defaultHWM) {
  const highWaterMark = strategy.highWaterMark;

  if (highWaterMark === undefined) return defaultHWM;

  if (highWaterMark !== highWaterMark || highWaterMark < 0)
    throw new RangeError("highWaterMark value is negative or not a number");

  return $toNumber(highWaterMark);
}

export function extractHighWaterMarkFromQueuingStrategyInit(init: { highWaterMark?: number }) {
  if (!$isObject(init)) throw new TypeError("QueuingStrategyInit argument must be an object.");
  const { highWaterMark } = init;
  if (highWaterMark === undefined) throw new TypeError("QueuingStrategyInit.highWaterMark member is required.");

  return $toNumber(highWaterMark);
}

export function createFulfilledPromise(value) {
  const promise = $newPromise();
  $fulfillPromise(promise, value);
  return promise;
}

export function toDictionary(value, defaultValue, errorMessage) {
  if ($isUndefinedOrNull(value)) return defaultValue;
  if (!$isObject(value)) throw $ERR_INVALID_ARG_TYPE(errorMessage);
  return value;
}

export function inspectWebStream(recv, name, fields, depth, options, inspectFn) {
  if (depth !== null && depth < 0) {
    return recv;
  }
  const nextOptions = Object.assign({}, options, {
    depth: options?.depth == null ? null : options.depth - 1,
  });
  const fn = typeof inspectFn === "function" ? inspectFn : Bun.inspect;
  return `${name} ${fn(fields, nextOptions)}`;
}

export function inspectReadableStream(this: any, depth, options, inspectFn) {
  const state = $getByIdDirectPrivate(this, "state");
  if (state === undefined) return this;
  const stateStr = state === $streamReadable ? "readable" : state === $streamClosed ? "closed" : "errored";
  const controller = $getByIdDirectPrivate(this, "readableStreamController");
  const supportsBYOB = !!($isObject(controller) && $getByIdDirectPrivate(controller, "pendingPullIntos") !== undefined);
  return $inspectWebStream(
    this,
    "ReadableStream",
    {
      locked: $getByIdDirectPrivate(this, "reader") !== undefined,
      state: stateStr,
      supportsBYOB,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectWritableStream(this: any, depth, options, inspectFn) {
  const state = $getByIdDirectPrivate(this, "state");
  if (state === undefined) return this;
  return $inspectWebStream(
    this,
    "WritableStream",
    {
      locked: $getByIdDirectPrivate(this, "writer") !== undefined,
      state,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectTransformStream(this: any, depth, options, inspectFn) {
  const readable = $getByIdDirectPrivate(this, "readable");
  if (readable === undefined) return this;
  return $inspectWebStream(
    this,
    "TransformStream",
    {
      readable,
      writable: $getByIdDirectPrivate(this, "writable"),
      backpressure: $getByIdDirectPrivate(this, "backpressure"),
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectReadableStreamDefaultReader(this: any, depth, options, inspectFn) {
  const closedPromiseCapability = $getByIdDirectPrivate(this, "closedPromiseCapability");
  if (!closedPromiseCapability) return this;
  const readRequests = $getByIdDirectPrivate(this, "readRequests");
  return $inspectWebStream(
    this,
    "ReadableStreamDefaultReader",
    {
      stream: $getByIdDirectPrivate(this, "ownerReadableStream"),
      readRequests: readRequests ? readRequests.size() : 0,
      close: closedPromiseCapability.promise,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectReadableStreamBYOBReader(this: any, depth, options, inspectFn) {
  const closedPromiseCapability = $getByIdDirectPrivate(this, "closedPromiseCapability");
  if (!closedPromiseCapability) return this;
  const readIntoRequests = $getByIdDirectPrivate(this, "readIntoRequests");
  return $inspectWebStream(
    this,
    "ReadableStreamBYOBReader",
    {
      stream: $getByIdDirectPrivate(this, "ownerReadableStream"),
      readIntoRequests: readIntoRequests ? readIntoRequests.size() : 0,
      close: closedPromiseCapability.promise,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectReadableStreamBYOBRequest(this: any, depth, options, inspectFn) {
  const controller = $getByIdDirectPrivate(this, "associatedReadableByteStreamController");
  if (!controller) return this;
  return $inspectWebStream(
    this,
    "ReadableStreamBYOBRequest",
    {
      view: $getByIdDirectPrivate(this, "view"),
      controller,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectWritableStreamDefaultWriter(this: any, depth, options, inspectFn) {
  const closedPromise = $getByIdDirectPrivate(this, "closedPromise");
  if (!closedPromise) return this;
  const stream = $getByIdDirectPrivate(this, "stream");
  return $inspectWebStream(
    this,
    "WritableStreamDefaultWriter",
    {
      stream,
      close: closedPromise.promise,
      ready: $getByIdDirectPrivate(this, "readyPromise")?.promise,
      desiredSize: stream ? this.desiredSize : null,
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectWritableStreamDefaultController(this: any, depth, options, inspectFn) {
  const stream = $getByIdDirectPrivate(this, "stream");
  if (stream === undefined) return this;
  return $inspectWebStream(
    this,
    "WritableStreamDefaultController",
    { stream },
    depth,
    options,
    inspectFn,
  );
}

export function inspectTransformStreamDefaultController(this: any, depth, options, inspectFn) {
  const stream = $getByIdDirectPrivate(this, "stream");
  if (stream === undefined) return this;
  return $inspectWebStream(
    this,
    "TransformStreamDefaultController",
    { stream },
    depth,
    options,
    inspectFn,
  );
}

export function inspectByteLengthQueuingStrategy(this: any, depth, options, inspectFn) {
  const highWaterMark = $getByIdDirectPrivate(this, "highWaterMark");
  if (highWaterMark === undefined) return this;
  return $inspectWebStream(
    this,
    "ByteLengthQueuingStrategy",
    { highWaterMark },
    depth,
    options,
    inspectFn,
  );
}

export function inspectCountQueuingStrategy(this: any, depth, options, inspectFn) {
  const highWaterMark = $getByIdDirectPrivate(this, "highWaterMark");
  if (highWaterMark === undefined) return this;
  return $inspectWebStream(
    this,
    "CountQueuingStrategy",
    { highWaterMark },
    depth,
    options,
    inspectFn,
  );
}

export function inspectTextEncoderStream(this: any, depth, options, inspectFn) {
  const transform = $getByIdDirectPrivate(this, "textEncoderStreamTransform");
  if (!transform) throw $ERR_INVALID_THIS("TextEncoderStream");
  return $inspectWebStream(
    this,
    "TextEncoderStream",
    {
      encoding: "utf-8",
      readable: $getByIdDirectPrivate(transform, "readable"),
      writable: $getByIdDirectPrivate(transform, "writable"),
    },
    depth,
    options,
    inspectFn,
  );
}

export function inspectTextDecoderStream(this: any, depth, options, inspectFn) {
  const transform = $getByIdDirectPrivate(this, "textDecoderStreamTransform");
  if (!transform) throw $ERR_INVALID_THIS("TextDecoderStream");
  const decoder = $getByIdDirectPrivate(this, "textDecoderStreamDecoder");
  return $inspectWebStream(
    this,
    "TextDecoderStream",
    {
      encoding: decoder.encoding,
      fatal: decoder.fatal,
      ignoreBOM: decoder.ignoreBOM,
      readable: $getByIdDirectPrivate(transform, "readable"),
      writable: $getByIdDirectPrivate(transform, "writable"),
    },
    depth,
    options,
    inspectFn,
  );
}
