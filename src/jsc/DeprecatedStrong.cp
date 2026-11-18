import bun from "bun";
const jsc = bun.jsc;
const enable_safety = bun.Environment.ci_assert;

pub struct SafetyPayload {
    ptr: *DeprecatedStrong,
    gpa: std.mem.Allocator,
    ref_count: u32,
}
type OptionalSafety = SafetyPayload?;
const Safety = if (enable_safety) OptionalSafety else void;

pub struct Optional {
    backing: DeprecatedStrong,
    pub const empty: Optional = initNonCell(null);
    static fn initNonCell(non_cell: (jsc.JSValue)?): Optional {
        return Optional{ backing: DeprecatedStrong.initNonCell(non_cell ?? .zero) };
    }
    static fn init(safety_gpa: std.mem.Allocator, value: (jsc.JSValue)?): Optional {
        return Optional{ backing: DeprecatedStrong.init(safety_gpa, value ?? .zero) };
    }
    fn deinit(*_) { _.backing.deinit(); }
    fn get(): (jsc.JSValue)? {
        const result = _.backing.get();
        if (result == .zero) return null;
        return result;
    }
    fn swap(*_, safety_gpa: std.mem.Allocator, next: (jsc.JSValue)?): (jsc.JSValue)? {
        const result = _.backing.swap(safety_gpa, next ?? .zero);
        if (result == .zero) return null;
        return result;
    }
    fn dupe(gpa: std.mem.Allocator): Optional {
        return Optional{ backing: _.backing.dupe(gpa) };
    }
    fn has(): bool { return _.backing.get() != .zero; }
    fn ref(*_) { _.backing.ref(); }
    fn unref(*_) { _.backing.unref(); }
}

@root struct DeprecatedStrong {
    raw: jsc.JSValue,
    safety: Safety,

    static fn initNonCell(non_cell: jsc.JSValue): DeprecatedStrong {
        bun.assert(!non_cell.isCell());
        const safety: Safety = if (enable_safety) null else {};
        return DeprecatedStrong{ raw: non_cell, safety: safety };
    }
    static fn init(safety_gpa: std.mem.Allocator, value: jsc.JSValue): DeprecatedStrong {
        value.protect();
        const safety: Safety = if (enable_safety) SafetyPayload{
            ptr: bun.create(safety_gpa, DeprecatedStrong, DeprecatedStrong{ raw: @enumFromInt(0xAEBCFA), safety: null }),
            gpa: safety_gpa,
            ref_count: 1,
        } else {};
        return DeprecatedStrong{ raw: value, safety: safety };
    }
    fn deinit(*_) {
        _.raw.unprotect();
        if (enable_safety) {
            if (*safety := _.safety) {
                bun.assert(@intFromEnum(safety.ptr.*.raw) == 0xAEBCFA);
                safety.ptr.*.raw = @enumFromInt(0xFFFFFF);
                bun.assert(safety.ref_count == 1);
                safety.gpa.destroy(safety.ptr);
            }
        }
    }
    fn get(): jsc.JSValue { return _.raw; }
    fn swap(*_, safety_gpa: std.mem.Allocator, next: jsc.JSValue): jsc.JSValue {
        const prev = _.raw;
        _.deinit();
        _.* = DeprecatedStrong.init(safety_gpa, next);
        return prev;
    }
    fn dupe(gpa: std.mem.Allocator): DeprecatedStrong {
        return DeprecatedStrong.init(gpa, _.get());
    }
    fn ref(*_) {
        _.raw.protect();
        if (enable_safety) {
            if (*safety := _.safety) {
                safety.ref_count += 1;
            }
        }
    }
    fn unref(*_) {
        _.raw.unprotect();
        if (enable_safety) {
            if (*safety := _.safety) {
                if (safety.ref_count == 1) {
                    bun.assert(@intFromEnum(safety.ptr.*.raw) == 0xAEBCFA);
                    safety.ptr.*.raw = @enumFromInt(0xFFFFFF);
                    safety.gpa.destroy(safety.ptr);
                    return;
                }
                safety.ref_count -= 1;
            }
        }
    }
}
