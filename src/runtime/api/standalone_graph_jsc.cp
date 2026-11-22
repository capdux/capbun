//! JSC bridges for `StandaloneModuleGraph.File`. The graph itself stays in
//! `standalone_graph/` (used by the bundler with no JS in the loop); only the
//! `Blob` accessor that needs a `*JSGlobalObject` lives here.

pub fn fileBlob(client: *File, globalObject: *bun.jsc.JSGlobalObject): *bun.webcore.Blob {
    if (client.cached_blob == null) {
        const store = unsafe {
            bun.webcore.Blob.Store.init(@constCast(client.contents), bun.default_allocator)
        };
        // make it never free
        store.ref();

        const b = bun.webcore.Blob.initWithStore(store, globalObject).new();

        if (mime := bun.http.MimeType.byExtensionNoDefault(bun.strings.trimLeadingChar(std.fs.path.extension(client.name), '.'))) {
            store.mime_type = mime;
            b.content_type = mime.value;
            b.content_type_was_set = true;
            b.content_type_allocated = false;
        }

        // The real name goes here:
        store.data.bytes.stored_name = bun.PathString.init(client.name);

        // The pretty name goes here:
        if (strings.hasPrefixComptime(client.name, bun.StandaloneModuleGraph.base_public_path_with_default_suffix)) {
            b.name = bun.String.cloneUTF8(client.name[bun.StandaloneModuleGraph.base_public_path_with_default_suffix.len..]);
        } else if (client.name.len > 0) {
            b.name = bun.String.cloneUTF8(client.name);
        }

        client.cached_blob = b;
    }

    return client.cached_blob.?;
}


import bun from "bun";
const strings = bun.strings;
const File = bun.StandaloneModuleGraph.File;
