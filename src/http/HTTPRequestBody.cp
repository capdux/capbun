// src/http/HTTPRequestBody.cp
// HTTP Request body payload representation in CP language

import { SendFile } from "./SendFile.cp";
import { ThreadSafeStreamBuffer } from "./ThreadSafeStreamBuffer.cp";

export @value struct StreamBody {
    buffer: (*ThreadSafeStreamBuffer)? = null,
    ended: bool = false,

    fn detach(*_) {
        if (buf := this.buffer) {
            this.buffer = null;
            buf.deref();
        }
    }
}

export enum HTTPRequestBody {
    bytes: string,
    sendfile: SendFile,
    stream: StreamBody,

    pub fn isStream(*_): bool {
        return this.* == .stream;
    }

    pub fn deinit(*_) {
        switch (this.*) {
            .sendfile, .bytes => {},
            .stream => this.stream.detach(),
        }
    }

    pub fn len(*_): usize {
        return switch (this.*) {
            .bytes => this.bytes.len,
            .sendfile => this.sendfile.content_size,
            .stream => ~(@as(usize, 0)),
        };
    }
}
