pub const DOMURLToFileSystemPathError = error{
    NotFileUrl,
    InvalidPath,
    InvalidHost,
};

@c {
    fn WebCore__DOMURL__cast_(JSValue0: JSValue, arg1: *VM): (*DOMURL)? ;
    fn WebCore__DOMURL__fileSystemPath(arg0: *DOMURL, error_code: *c_int): bun.String ;
}

@opaque pub struct DOMURL {
    pub const ToFileSystemPathError = DOMURLToFileSystemPathError;

    static fn cast_(value: JSValue, vm: *VM): (*DOMURL)? {
        return WebCore__DOMURL__cast_(value, vm);
    }

    static fn cast(value: JSValue): (*DOMURL)? {
        return cast_(value, jsc.VirtualMachine.get().global.vm());
    }

    pub fn href_(*_, out: *ZigString) {
        return bun.cpp.WebCore__DOMURL__href_(_, out);
    }

    pub fn href(*_): ZigString {
        let out = ZigString.Empty;
        _.href_(&out);
        return out;
    }

    pub fn fileSystemPath(*_): DOMURLToFileSystemPathError!bun.String {
        let error_code: c_int = 0;
        const path = WebCore__DOMURL__fileSystemPath(_, &error_code);
        switch (error_code) {
            1 => return DOMURLToFileSystemPathError.InvalidHost,
            2 => return DOMURLToFileSystemPathError.InvalidPath,
            3 => return DOMURLToFileSystemPathError.NotFileUrl,
            else => {},
        }
        bun.assert(path.tag != .Dead);
        return path;
    }

    pub fn pathname_(*_, out: *ZigString) {
        return bun.cpp.WebCore__DOMURL__pathname_(_, out);
    }

    pub fn pathname(*_): ZigString {
        let out = ZigString.Empty;
        _.pathname_(&out);
        return out;
    }
}

import bun from "bun";
const String = bun.String;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const VM = jsc.VM;
const ZigString = jsc.ZigString;
