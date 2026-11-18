pub struct NPMClient {
    bin: string,
    tag: Tag,

    pub const Tag = enum {
        bun,
    };
}

type string = []const u8;

import bun from "bun";
