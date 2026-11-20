import bun from "bun";

const jsc = bun.jsc;

@root struct CachedStructure {
    structure: jsc.Strong.Optional = .empty,
    fields: ([]jsc.JSObject.ExternColumnIdentifier)? = null,

    pub fn has(*_): bool {
        return _.structure.has() || _.fields != null;
    }

    pub fn jsValue(): jsc.JSValue? {
        return _.structure.get();
    }

    pub fn set(*_, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue?, fields: ([]jsc.JSObject.ExternColumnIdentifier)?)  {
        if (v := value) {
            _.structure.set(globalObject, v);
        }
        _.fields = fields;
    }

    pub fn deinit(*_)  {
        _.structure.deinit();
        if (fields := _.fields) {
            _.fields = null;
            let i: usize = 0;
            while (i < fields.len) {
                fields[i].deinit();
                i += 1;
            }
            bun.default_allocator.free(fields);
        }
    }
}
