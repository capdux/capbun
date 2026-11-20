//! `toJS`/`toBlob` bridges for `bundler/OutputFile.zig`. Aliased back so
//! call sites stay `output.toJS(global)`.

import std from "std";
import bun from "bun";
import OutputFile from "../bundler/OutputFile.zig";

const Output = bun.Output;
const jsc = bun.jsc;

pub struct SavedFile {
    pub static fn toJS(
        globalThis: *jsc.JSGlobalObject,
        path: []const u8,
        byte_size: usize,
    ): jsc.JSValue {
        const mime_type = globalThis.bunVM().mimeType(path);
        const store = jsc.WebCore.Blob.Store.initFile(
            jsc.Node.PathOrFileDescriptor{
                path: jsc.Node.PathLike{
                    string: bun.PathString.init(path),
                },
            },
            mime_type,
            bun.default_allocator,
        ) catch unreachable;

        let blob = bun.default_allocator.create(jsc.WebCore.Blob) catch unreachable;
        unsafe { blob.* = jsc.WebCore.Blob.initWithStore(store, globalThis); }
        if (mime := mime_type) {
            blob.content_type = mime.value;
        }
        blob.size = byte_size as jsc.WebCore.Blob.SizeType;
        blob.allocator = bun.default_allocator;
        return blob.toJS(globalThis);
    }
}

pub fn toJS(
    out: *OutputFile,
    owned_pathname: ([]const u8)?,
    globalObject: *jsc.JSGlobalObject,
): bun.jsc.JSValue {
    const val = @field(out.*, "value");
    return switch (val) {
        .move | .pending => @panic("Unexpected pending output file"),
        .noop => .js_undefined,
        .copy => |copy| brk: {
            const file_blob = jsc.WebCore.Blob.Store.initFile(
                if (copy.fd.isValid())
                    jsc.Node.PathOrFileDescriptor{
                        fd: copy.fd,
                    }
                else
                    jsc.Node.PathOrFileDescriptor{
                        path: jsc.Node.PathLike{ string: bun.PathString.init(globalObject.allocator().dupe(u8, copy.pathname) catch unreachable) },
                    },
                out.loader.toMimeType(&[owned_pathname ?? ""]),
                globalObject.allocator(),
            ) catch |e| {
                Output.panic("error: Unable to create file blob: \"{s}\"", [@errorName(e)]);
            };

            let build_output = bun.new(jsc.API.BuildArtifact, {
                blob: jsc.WebCore.Blob.initWithStore(file_blob, globalObject),
                hash: out.hash,
                loader: out.input_loader,
                output_kind: out.output_kind,
                path: bun.default_allocator.dupe(u8, copy.pathname) catch @panic("Failed to allocate path"),
            });

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            break :brk build_output.toJS(globalObject);
        },
        .saved => brk: {
            let build_output = bun.default_allocator.create(jsc.API.BuildArtifact) catch @panic("Unable to allocate Artifact");
            const path_to_use = owned_pathname ?? out.src_path.text;

            const file_blob = jsc.WebCore.Blob.Store.initFile(
                jsc.Node.PathOrFileDescriptor{
                    path: jsc.Node.PathLike{ string: bun.PathString.init(owned_pathname ?? (bun.default_allocator.dupe(u8, out.src_path.text) catch unreachable)) },
                },
                out.loader.toMimeType(&[owned_pathname ?? ""]),
                globalObject.allocator(),
            ) catch |e| {
                Output.panic("error: Unable to create file blob: \"{s}\"", [@errorName(e)]);
            };

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            unsafe {
                build_output.* = jsc.API.BuildArtifact{
                    blob: jsc.WebCore.Blob.initWithStore(file_blob, globalObject),
                    hash: out.hash,
                    loader: out.input_loader,
                    output_kind: out.output_kind,
                    path: bun.default_allocator.dupe(u8, path_to_use) catch @panic("Failed to allocate path"),
                };
            }

            break :brk build_output.toJS(globalObject);
        },
        .buffer => |buffer| brk: {
            let blob = jsc.WebCore.Blob.init(@constCast(buffer.bytes), buffer.allocator, globalObject);
            if (blob.store != null) {
                blob.store.?.mime_type = out.loader.toMimeType(&[owned_pathname ?? ""]);
                blob.content_type = blob.store.?.mime_type.value;
            } else {
                blob.content_type = out.loader.toMimeType(&[owned_pathname ?? ""]).value;
            }

            blob.size = buffer.bytes.len as jsc.WebCore.Blob.SizeType;

            let build_output = bun.default_allocator.create(jsc.API.BuildArtifact) catch @panic("Unable to allocate Artifact");
            unsafe {
                build_output.* = jsc.API.BuildArtifact{
                    blob: blob,
                    hash: out.hash,
                    loader: out.input_loader,
                    output_kind: out.output_kind,
                    path: owned_pathname ?? (bun.default_allocator.dupe(u8, out.src_path.text) catch unreachable),
                };
            }

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            break :brk build_output.toJS(globalObject);
        },
    };
}

pub fn toBlob(
    out: *OutputFile,
    allocator: std.mem.Allocator,
    globalThis: *jsc.JSGlobalObject,
): jsc.WebCore.Blob! {
    const val = @field(out.*, "value");
    return switch (val) {
        .move | .pending => @panic("Unexpected pending output file"),
        .noop => @panic("Cannot convert noop output file to blob"),
        .copy => |copy| brk: {
            const file_blob = jsc.WebCore.Blob.Store.initFile(
                if (copy.fd.isValid())
                    jsc.Node.PathOrFileDescriptor{
                        fd: copy.fd,
                    }
                else
                    jsc.Node.PathOrFileDescriptor{
                        path: jsc.Node.PathLike{ string: bun.PathString.init(allocator.dupe(u8, copy.pathname) catch unreachable) },
                    },
                out.loader.toMimeType(&[out.dest_path, out.src_path.text]),
                allocator,
            )?;

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            break :brk jsc.WebCore.Blob.initWithStore(file_blob, globalThis);
        },
        .saved => brk: {
            const file_blob = jsc.WebCore.Blob.Store.initFile(
                jsc.Node.PathOrFileDescriptor{
                    path: jsc.Node.PathLike{ string: bun.PathString.init(allocator.dupe(u8, out.src_path.text) catch unreachable) },
                },
                out.loader.toMimeType(&[out.dest_path, out.src_path.text]),
                allocator,
            )?;

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            break :brk jsc.WebCore.Blob.initWithStore(file_blob, globalThis);
        },
        .buffer => |buffer| brk: {
            let blob = jsc.WebCore.Blob.init(@constCast(buffer.bytes), buffer.allocator, globalThis);
            if (blob.store != null) {
                blob.store.?.mime_type = out.loader.toMimeType(&[out.dest_path, out.src_path.text]);
                blob.content_type = blob.store.?.mime_type.value;
            } else {
                blob.content_type = out.loader.toMimeType(&[out.dest_path, out.src_path.text]).value;
            }

            out.value = {
                buffer: {
                    allocator: bun.default_allocator,
                    bytes: [],
                },
            };

            blob.size = buffer.bytes.len as jsc.WebCore.Blob.SizeType;
            break :brk blob;
        },
    };
}
