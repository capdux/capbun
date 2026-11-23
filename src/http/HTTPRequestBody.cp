// src/http/HTTPRequestBody.cp
// HTTP Request body payload representation in CP language

import { SendFile } from "./SendFile.cp";
import { ThreadSafeStreamBuffer } from "./ThreadSafeStreamBuffer.cp";

export @value struct StreamBody {
    buffer: (*ThreadSafeStreamBuffer)? = null,
    ended: bool = false,

    fn detach(*_) {
        if (buf := _.buffer) {
            _.buffer = null;
            buf.deref();
        }
    }
}

export enum HTTPRequestBody {
    bytes: string,
    sendfile: SendFile,
    stream: StreamBody,

    pub fn isStream(): bool {
        return _ == .stream;
    }

    pub fn deinit(*_) {
        switch (_.*) {
            .sendfile, .bytes => {},
            .stream => _.stream.detach(),
        }
    }

    pub fn len(): usize {
        return switch (_) {
            .bytes => _.bytes.len,
            .sendfile => _.sendfile.content_size,
            .stream => ~(@as(usize, 0)),
        };
    }
}
