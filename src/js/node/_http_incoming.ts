const Readable = require("internal/streams/readable");

const {
  abortedSymbol,
  eofInProgress,
  kHandle,
  noBodySymbol,
  typeSymbol,
  NodeHTTPIncomingRequestType,
  fakeSocketSymbol,
  isAbortError,
  emitErrorNextTickIfErrorListenerNT,
  kEmptyObject,
  getIsNextIncomingMessageHTTPS,
  setIsNextIncomingMessageHTTPS,
  NodeHTTPBodyReadState,
  emitEOFIncomingMessage,
  bodyStreamSymbol,
  statusMessageSymbol,
  statusCodeSymbol,
  webRequestOrResponse,
  NodeHTTPResponseAbortEvent,
  STATUS_CODES,
  assignHeadersFast,
  setRequestTimeout,
  headersTuple,
  webRequestOrResponseHasBodyValue,
  getCompleteWebRequestOrResponseBodyValueAsArrayBuffer,
  kAbortController,
} = require("internal/http");

const { FakeSocket } = require("internal/http/FakeSocket");

const kHeaders = Symbol("kHeaders");
const kRawHeaders = Symbol("kRawHeaders");
const kRawHeadersDirty = Symbol("kRawHeadersDirty");

var defaultIncomingOpts = { type: "request" };
const nop = () => {};

function assignHeadersSlow(object, req) {
  const headers = req.headers;
  var outHeaders = Object.create(null);
  const rawHeaders: string[] = [];
  var i = 0;
  for (let key in headers) {
    var originalKey = key;
    var value = headers[originalKey];

    key = key.toLowerCase();

    if (key !== "set-cookie") {
      value = String(value);
      $putByValDirect(rawHeaders, i++, originalKey);
      $putByValDirect(rawHeaders, i++, value);
      outHeaders[key] = value;
    } else {
      if ($isJSArray(value)) {
        outHeaders[key] = value.slice();

        for (let entry of value) {
          $putByValDirect(rawHeaders, i++, originalKey);
          $putByValDirect(rawHeaders, i++, entry);
        }
      } else {
        value = String(value);
        outHeaders[key] = [value];
        $putByValDirect(rawHeaders, i++, originalKey);
        $putByValDirect(rawHeaders, i++, value);
      }
    }
  }
  object.rawHeaders = rawHeaders;
  object.headers = outHeaders;
}

function assignHeaders(object, req) {
  // This fast path is an 8% speedup for a "hello world" node:http server, and a 7% speedup for a "hello world" express server
  if (assignHeadersFast(req, object, headersTuple)) {
    const headers = $getInternalField(headersTuple, 0);
    const rawHeaders = $getInternalField(headersTuple, 1);
    $putInternalField(headersTuple, 0, undefined);
    $putInternalField(headersTuple, 1, undefined);
    object.rawHeaders = rawHeaders;
    object.headers = headers;
    return true;
  } else {
    assignHeadersSlow(object, req);
    return false;
  }
}

function onIncomingMessagePauseNodeHTTPResponse(this: IncomingMessage) {
  const handle = this[kHandle];
  if (handle && !this.destroyed) {
    handle.pause();
  }
}

function onIncomingMessageResumeNodeHTTPResponse(this: IncomingMessage) {
  const handle = this[kHandle];
  if (handle && !this.destroyed) {
    const resumed = handle.resume();
    if (resumed && resumed !== true) {
      const bodyReadState = handle.hasBody;
      if ((bodyReadState & NodeHTTPBodyReadState.done) !== 0) {
        emitEOFIncomingMessage(this);
      }
      this.push(resumed);
    }
  }
}

function IncomingMessage(req, options = defaultIncomingOpts) {
  this[abortedSymbol] = false;
  this[eofInProgress] = false;
  this._consuming = false;
  this._dumped = false;
  this.complete = false;
  this._closed = false;

  // (url, method, headers, rawHeaders, handle, hasBody)
  if (req === kHandle) {
    this[typeSymbol] = NodeHTTPIncomingRequestType.NodeHTTPResponse;
    this.url = arguments[1];
    this.method = arguments[2];
    this[kHeaders] = arguments[3];
    this[kRawHeaders] = arguments[4];
    this[kRawHeadersDirty] = false;
    this[kHandle] = arguments[5];
    this[noBodySymbol] = !arguments[6];
    this.socket = arguments[7];
    Readable.$call(this);

    if (this[noBodySymbol]) {
      emitEOFIncomingMessage(this);
    } else {
      // If there's a body, pay attention to pause/resume events
      this.on("pause", onIncomingMessagePauseNodeHTTPResponse);
      this.on("resume", onIncomingMessageResumeNodeHTTPResponse);
    }
  } else {
    this[noBodySymbol] = false;
    Readable.$call(this);
    var { [typeSymbol]: type } = options || {};

    this[webRequestOrResponse] = req;
    this[typeSymbol] = type;
    this[bodyStreamSymbol] = undefined;
    const statusText = (req as Response)?.statusText;
    this[statusMessageSymbol] = statusText !== "" ? statusText || null : "";
    this[statusCodeSymbol] = (req as Response)?.status || 200;

    if (type === NodeHTTPIncomingRequestType.FetchRequest || type === NodeHTTPIncomingRequestType.FetchResponse) {
      if (!assignHeaders(this, req)) {
        this[fakeSocketSymbol] = req;
      }
      this.socket = new FakeSocket(this);
    } else {
      // Node defaults url and method to null.
      this.url = "";
      this.method = null;
      this.rawHeaders = [];
      this.socket = req;
    }

    this[noBodySymbol] =
      type === NodeHTTPIncomingRequestType.FetchRequest // TODO: Add logic for checking for body on response
        ? requestHasNoBody(this.method, this)
        : false;

    if (getIsNextIncomingMessageHTTPS()) {
      this.socket.encrypted = true;
      setIsNextIncomingMessageHTTPS(false);
    }
  }

  this._readableState.readingMore = true;
}

function onDataIncomingMessage(
  this: import("node:http").IncomingMessage,
  chunk,
  isLast,
  aborted: NodeHTTPResponseAbortEvent,
) {
  if (aborted === NodeHTTPResponseAbortEvent.abort) {
    this.destroy();
    return;
  }

  if (chunk && !this._dumped) this.push(chunk);

  if (isLast) {
    emitEOFIncomingMessage(this);
  }
}

const IncomingMessagePrototype = {
  constructor: IncomingMessage,
  __proto__: Readable.prototype,
  httpVersion: "1.1",
  _construct(callback) {
    // TODO: streaming
    const type = this[typeSymbol];

    if (type === NodeHTTPIncomingRequestType.FetchResponse) {
      if (!webRequestOrResponseHasBodyValue(this[webRequestOrResponse])) {
        this.complete = true;
        this.push(null);
      }
    }

    callback();
  },
  // Call this instead of resume() if we want to just
  // dump all the data to /dev/null
  _dump() {
    if (!this._dumped) {
      this._dumped = true;
      // If there is buffered data, it may trigger 'data' events.
      // Remove 'data' event listeners explicitly.
      this.removeAllListeners("data");
      const handle = this[kHandle];
      if (handle) {
        handle.ondata = undefined;
      }
      this.resume();
    }
  },
  _read(_size) {
    if (!this._consuming) {
      this._readableState.readingMore = false;
      this._consuming = true;
    }

    const socket = this.socket;
    if (socket && socket.readable) {
      //https://github.com/nodejs/node/blob/13e3aef053776be9be262f210dc438ecec4a3c8d/lib/_http_incoming.js#L211-L213
      socket.resume();
    }

    if (this[eofInProgress]) {
      // There is a nextTick pending that will emit EOF
      return;
    }

    let internalRequest;
    if (this[noBodySymbol]) {
      emitEOFIncomingMessage(this);
      return;
    } else if ((internalRequest = this[kHandle])) {
      const bodyReadState = internalRequest.hasBody;

      if (
        (bodyReadState & NodeHTTPBodyReadState.done) !== 0 ||
        bodyReadState === NodeHTTPBodyReadState.none ||
        this._dumped
      ) {
        emitEOFIncomingMessage(this);
      }

      if ((bodyReadState & NodeHTTPBodyReadState.hasBufferedDataDuringPause) !== 0) {
        const drained = internalRequest.drainRequestBody();
        if (drained && !this._dumped) {
          this.push(drained);
        }
      }

      if (!internalRequest.ondata) {
        internalRequest.ondata = onDataIncomingMessage.bind(this);
        internalRequest.hasCustomOnData = false;
      }

      return true;
    } else if (this[bodyStreamSymbol] == null) {
      // If it's all available right now, we skip going through ReadableStream.
      let completeBody = getCompleteWebRequestOrResponseBodyValueAsArrayBuffer(this[webRequestOrResponse]);
      if (completeBody) {
        $assert(completeBody instanceof ArrayBuffer, "completeBody is not an ArrayBuffer");
        $assert(completeBody.byteLength > 0, "completeBody should not be empty");

        // They're ignoring the data. Let's not do anything with it.
        if (!this._dumped) {
          this.push(new Buffer(completeBody));
        }
        emitEOFIncomingMessage(this);
        return;
      }

      const reader = this[webRequestOrResponse].body?.getReader?.() as ReadableStreamDefaultReader;
      if (!reader) {
        emitEOFIncomingMessage(this);
        return;
      }

      this[bodyStreamSymbol] = reader;
      consumeStream(this, reader);
    }

    return;
  },
  _finish() {
    this.emit("prefinish");
  },
  _destroy: function IncomingMessage_destroy(err, cb) {
    const shouldEmitAborted = !this.readableEnded || !this.complete;

    if (shouldEmitAborted) {
      this[abortedSymbol] = true;
      // IncomingMessage emits 'aborted'.
      // Client emits 'abort'.
      this.emit("aborted");
    }

    // Suppress "AbortError" from fetch() because we emit this in the 'aborted' event
    if (isAbortError(err)) {
      err = undefined;
    }
    var nodeHTTPResponse = this[kHandle];
    if (nodeHTTPResponse) {
      this[kHandle] = undefined;
      nodeHTTPResponse.onabort = nodeHTTPResponse.ondata = undefined;
      if (!nodeHTTPResponse.finished && shouldEmitAborted) {
        nodeHTTPResponse.abort();
      }
      const socket = this.socket;
      if (socket && !socket.destroyed && shouldEmitAborted) {
        socket.destroy(err);
      }
    } else {
      const stream = this[bodyStreamSymbol];
      this[bodyStreamSymbol] = undefined;
      const streamState = stream?.$state;

      if (streamState === $streamReadable || streamState === $streamWaiting || streamState === $streamWritable) {
        stream?.cancel?.().catch(nop);
      }

      const socket = this.socket;
      if (socket && !socket.destroyed && shouldEmitAborted) {
        socket.destroy(err);
      }
    }
    const req = this.req;
    if (req && !this.complete) {
      req[kAbortController]?.abort?.();
    }

    if ($isCallable(cb)) {
      emitErrorNextTickIfErrorListenerNT(this, err, cb);
    }
  },
  get headers() {
    if (this[kRawHeadersDirty] || this[kHeaders] === undefined) {
      const headers = Object.create(null);
      const rawHeaders = this[kRawHeaders];
      if (Array.isArray(rawHeaders)) {
        for (let i = 0; i + 1 < rawHeaders.length; i += 2) {
          const key = String(rawHeaders[i]).toLowerCase();
          const value = String(rawHeaders[i + 1]);
          if (key === "set-cookie") {
            const existing = headers[key];
            if (existing !== undefined) {
              existing.push(value);
            } else {
              headers[key] = [value];
            }
          } else {
            const existing = headers[key];
            if (existing !== undefined) {
              headers[key] = existing + ", " + value;
            } else {
              headers[key] = value;
            }
          }
        }
      }
      this[kHeaders] = headers;
      this[kRawHeadersDirty] = false;
    }
    return this[kHeaders];
  },
  set headers(value) {
    this[kHeaders] = value;
    this[kRawHeadersDirty] = false;
  },
  get rawHeaders() {
    return this[kRawHeaders];
  },
  set rawHeaders(value) {
    this[kRawHeaders] = value;
    this[kRawHeadersDirty] = true;
  },
  get aborted() {
    return this[abortedSymbol];
  },
  set aborted(value) {
    this[abortedSymbol] = value;
  },
  get connection() {
    return this.socket;
  },
  get statusCode() {
    return this[statusCodeSymbol];
  },
  set statusCode(value) {
    if (!(value in STATUS_CODES)) return;
    this[statusCodeSymbol] = value;
  },
  get statusMessage() {
    return this[statusMessageSymbol];
  },
  set statusMessage(value) {
    this[statusMessageSymbol] = value;
  },
  get httpVersionMajor() {
    const version = this.httpVersion;
    if (version.startsWith("1.")) {
      return 1;
    }
    return 0;
  },
  set httpVersionMajor(value) {
    // noop
  },
  get httpVersionMinor() {
    const version = this.httpVersion;
    if (version.endsWith(".1")) {
      return 1;
    }
    return 0;
  },
  set httpVersionMinor(value) {
    // noop
  },
  get rawTrailers() {
    return [];
  },
  set rawTrailers(value) {
    // noop
  },
  get trailers() {
    return kEmptyObject;
  },
  set trailers(value) {
    // noop
  },
  setTimeout(msecs, callback) {
    void this.take;
    const req = this[kHandle] || this[webRequestOrResponse];

    if (req) {
      setRequestTimeout(req, Math.ceil(msecs / 1000));
      if (typeof callback === "function") this.once("timeout", callback);
    }
    return this;
  },
} satisfies typeof import("node:http").IncomingMessage.prototype;
IncomingMessage.prototype = IncomingMessagePrototype;
$setPrototypeDirect.$call(IncomingMessage, Readable);

function requestHasNoBody(method, req) {
  if ("GET" === method || "HEAD" === method || "TRACE" === method || "CONNECT" === method || "OPTIONS" === method)
    return true;
  const headers = req?.headers;
  const contentLength = headers?.["content-length"];
  if (!parseInt(contentLength, 10)) return true;

  return false;
}

async function consumeStream(self, reader: ReadableStreamDefaultReader) {
  var done = false,
    value,
    aborted = false;
  try {
    while (true) {
      const result = reader.readMany();
      if ($isPromise(result)) {
        ({ done, value } = await result);
      } else {
        ({ done, value } = result);
      }

      if (self.destroyed || (aborted = self[abortedSymbol])) {
        break;
      }
      if (!self._dumped) {
        for (var v of value) {
          self.push(v);
        }
      }

      if (self.destroyed || (aborted = self[abortedSymbol]) || done) {
        break;
      }
    }
  } catch (err) {
    if (aborted || self.destroyed) return;
    self.destroy(err);
  } finally {
    reader?.cancel?.().catch?.(nop);
  }

  if (!self.complete) {
    emitEOFIncomingMessage(self);
  }
}

function readStart(socket) {
  if (socket && !socket._paused && socket.readable) {
    socket.resume();
  }
}

function readStop(socket) {
  if (socket) {
    socket.pause();
  }
}

export { IncomingMessage, readStart, readStop };
