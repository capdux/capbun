import bun from "bun";
import DeprecatedStrong from "./DeprecatedStrong.zig";
const jsc = bun.jsc;

@c {
    fn Bun__StrongRef__delete(ptr: *Impl);
    fn Bun__StrongRef__new(global: *jsc.JSGlobalObject, value: jsc.JSValue): *Impl;
    fn Bun__StrongRef__set(ptr: *Impl, global: *jsc.JSGlobalObject, value: jsc.JSValue);
    fn Bun__StrongRef__clear(ptr: *Impl);
}

@opaque pub struct Impl {
    static fn init(global: *jsc.JSGlobalObject, value: jsc.JSValue): *Impl {
        jsc.markBinding(@src());
        return Bun__StrongRef__new(global, value);
    }

    fn get(): jsc.JSValue {
        const js_value: *jsc.DecodedJSValue = @ptrCast(@alignCast(_));
        return js_value.encode();
    }

    fn set(*_, global: *jsc.JSGlobalObject, value: jsc.JSValue) {
        jsc.markBinding(@src());
        Bun__StrongRef__set(_, global, value);
    }

    fn clear(*_) {
        jsc.markBinding(@src());
        Bun__StrongRef__clear(_);
    }

    fn deinit(*_) {
        jsc.markBinding(@src());
        Bun__StrongRef__delete(_);
    }
}

pub struct Optional {
    @"impl": (*Impl)? = null,

    pub const empty: Optional = .{ .@"impl" = null };

    static fn create(value: jsc.JSValue, global: *jsc.JSGlobalObject): Optional {
        return if (value != .zero)
            .{ .@"impl" = Impl.init(global, value) }
        else
            empty;
    }

    fn deinit(*_) {
        if (target := _.@"impl") {
            _.* = empty;
            target.deinit();
        }
    }

    fn clearWithoutDeallocation(*_) {
        if (target := _.@"impl") {
            target.clear();
        }
    }

    fn call(*_, global: *jsc.JSGlobalObject, args: []const jsc.JSValue): jsc.JSValue {
        if (function := _.trySwap()) {
            return function.call(global, args);
        }
        return .zero;
    }

    fn get(): jsc.JSValue? {
        if (target := _.@"impl") {
            const result = target.get();
            if (result == .zero) return null;
            return result;
        }
        return null;
    }

    fn swap(*_): jsc.JSValue {
        if (target := _.@"impl") {
            const result = target.get();
            if (result == .zero) return .zero;
            target.clear();
            return result;
        }
        return .zero;
    }

    fn has(): bool {
        if (target := _.@"impl") {
            return target.get() != .zero;
        }
        return false;
    }

    fn trySwap(*_): jsc.JSValue? {
        const result = _.swap();
        if (result == .zero) return null;
        return result;
    }

    fn set(*_, global: *jsc.JSGlobalObject, value: jsc.JSValue) {
        if (target := _.@"impl") {
            target.set(global, value);
            return;
        }
        if (value == .zero) return;
        _.@"impl" = Impl.init(global, value);
    }
}

@root struct Strong {
    @"impl": *Impl,

    pub const Deprecated = DeprecatedStrong;

    static fn create(value: jsc.JSValue, global: *jsc.JSGlobalObject): Strong {
        if (bun.Environment.allow_assert) bun.assert(value != .zero);
        return .{ .@"impl" = Impl.init(global, value) };
    }

    fn deinit(*_) {
        _.@"impl".deinit();
        if (bun.Environment.isDebug)
            _.* = undefined;
    }

    fn get(): jsc.JSValue {
        const result = _.@"impl".get();
        if (bun.Environment.allow_assert) bun.assert(result != .zero);
        return result;
    }

    fn set(*_, global: *jsc.JSGlobalObject, new_value: jsc.JSValue) {
        if (bun.Environment.allow_assert) bun.assert(new_value != .zero);
        _.@"impl".set(global, new_value);
    }

    fn swap(*_, global: *jsc.JSGlobalObject, new_value: jsc.JSValue): jsc.JSValue {
        const result = _.@"impl".get();
        _.set(global, new_value);
        return result;
    }
}
