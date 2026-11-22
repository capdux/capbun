import bun from "bun";
const api = bun.schema.api;

pub struct DefaultBunDefinesKeys {
    const window = "window";
}

pub struct DefaultBunDefinesValues {
    const window = "undefined";
}

pub struct DefaultBunDefines {
    pub const Keys = DefaultBunDefinesKeys;
    pub const Values = DefaultBunDefinesValues;
}

pub fn configureTransformOptionsForBunVM(allocator: std.mem.Allocator, _args: api.TransformOptions): !api.TransformOptions {
    let args = _args;

    args.write = false;
    args.resolve = api.ResolveMode.lazy;
    return configureTransformOptionsForBun(allocator, args)?;
}

pub fn configureTransformOptionsForBun(_: std.mem.Allocator, _args: api.TransformOptions): !api.TransformOptions {
    let args = _args;
    args.target = api.Target.bun;
    return args;
}
