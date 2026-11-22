pub enum NPMClientTag {
    bun,
}

pub struct NPMClient {
    bin: string,
    tag: Tag,

    pub const Tag = NPMClientTag;
}

type string = []const u8;

import bun from "bun";
