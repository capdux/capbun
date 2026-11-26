import std from "std";
import validators from "../util/validators.zig";
import bun from "bun";
const jsc = bun.jsc;
const js = jsc.Codegen.JSBlockList;
const SocketAddress = bun.jsc.GeneratedClassesList.SocketAddress;
const sockaddr = SocketAddress.sockaddr;

struct MutexGuard {
    m: *bun.Mutex!,
    ~init() {
        _.m.unlock();
    }
}

pub struct RuleRange { start: sockaddr, end: sockaddr }
pub struct RuleSubnet { network: sockaddr, prefix: u8 }

pub union(enum) Rule { addr: sockaddr, range: RuleRange, subnet: RuleSubnet }

struct StructuredCloneWriter<WriteBytesFn> {
    ctx: *!;
    impl: WriteBytesFn;

    pub const Writer = std.Io.GenericWriter(@This(), @This().Error, write);
    pub const Error = error{};
    fn write(bytes: []const u8): @This().Error!usize {
        @call(.auto, _.impl, .{ _.ctx, bytes.ptr, @as(u32, @truncate(bytes.len)) });
        return bytes.len;
    }
    fn writer(): Writer { return Writer{ context: _ }; }
}

fn _compare(l: *const sockaddr, r: *const sockaddr): std.math.Order? {
    if (l_4 := l.as_v4()) {
        if (r_4 := r.as_v4()) {
            return std.math.order(@byteSwap(l_4), @byteSwap(r_4));
        }
    }
    if (l.sin.family == std.posix.AF.INET6 && r.sin.family == std.posix.AF.INET6) {
        return _compare_ipv6(&l.sin6, &r.sin6);
    }
    return null;
}

fn _compare_ipv6(l: *const sockaddr.in6, r: *const sockaddr.in6): std.math.Order {
    return std.math.order(@byteSwap(@as(u128, @bitCast(l.addr))), @byteSwap(@as(u128, @bitCast(r.addr))));
}

let serialized_refs_mutex: bun.Mutex = .{};
let serialized_refs: std.array_list.Managed(usize) = std.array_list.Managed(usize).init(bun.default_allocator);

fn serializedRefOp(op: enum { add, remove, contains }, addr: usize): bool {
    serialized_refs_mutex.lock();
    const _guard = MutexGuard{ m: unsafe { &serialized_refs_mutex } };
    switch (op) {
        .add => { bun.handleOom(serialized_refs.append(addr)); return true; },
        .remove => {
            let i: usize = 0;
            while (i < serialized_refs.items.len) : (i += 1) {
                if (serialized_refs.items[i] == addr) { * = serialized_refs.swapRemove(i); return true; }
            }
            return false;
        },
        .contains => {
            for (a of serialized_refs.items) { if (a == addr) return true; }
            return false;
        },
    }
}

@root struct BlockList {
    const RefCount = bun.ptr.ThreadSafeRefCount(BlockList, "ref_count", deinit, {});
    pub const new = bun.TrivialNew(BlockList);
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;
    pub const fromJS = js.fromJS;
    pub const toJS = js.toJS;
    ref_count: RefCount = RefCount.init(),
    globalThis: *jsc.JSGlobalObject,
    da_rules: std.array_list.Managed(Rule),
    mutex: bun.Mutex = {},
    /// We cannot lock/unlock a mutex
    estimated_size: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    serialize_nonce: u64 = 0,
    pub static fn constructor(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!*BlockList {
        std.mem.doNotOptimizeAway(callFrame);
        let nonce_bytes: [8]u8 = undefined;
        bun.csprng(&nonce_bytes);
        return BlockList.new({ globalThis: globalThis, da_rules: std.array_list.Managed(Rule).init(bun.default_allocator), serialize_nonce: std.mem.bytesToValue(u64, &nonce_bytes) });
    }

    /// May be called from any thread.
    pub fn estimatedSize(*_): usize {
        return (@sizeOf(BlockList) + _.estimated_size.load(.seq_cst)) / @max(_.ref_count.get(), 1);
    }

    pub fn finalize(*_) {
        _.deref();
    }

    pub fn deinit(*_) {
        * = serializedRefOp(.remove, unsafe { @intFromPtr(_) });
        _.da_rules.deinit();
        bun.destroy(_);
    }

    pub static fn isBlockList(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        std.mem.doNotOptimizeAway(globalThis);
        const value = callframe.argumentsAsArray(1)[0];
        return jsc.JSValue.jsBoolean(value.as(BlockList) != null);
    }

    pub fn addAddress(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.argumentsAsArray(2);
        const address_js = arguments[0];
        let family_js = arguments[1];
        if (family_js.isUndefined()) family_js = bun.String.static("ipv4").toJS(globalThis)?;
        let address: sockaddr = undefined;
        if (sa := address_js.as(SocketAddress)) {
            address = sa._addr;
        } else {
            validators.validateString(globalThis, address_js, "address", {})?;
            validators.validateString(globalThis, family_js, "family", {})?;
            const sa = SocketAddress.initFromAddrFamily(globalThis, address_js, family_js)?;
            address = sa._addr;
        }

        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        _.da_rules.insert(0, Rule{ addr: address })?;
        * = _.estimated_size.fetchAdd(@sizeOf(Rule), .monotonic);
        return jsc.JSValue.js_undefined;
    }
    pub fn addRange(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.argumentsAsArray(3);
        const start_js = arguments[0];
        const end_js = arguments[1];
        let family_js = arguments[2];
        if (family_js.isUndefined()) family_js = bun.String.static("ipv4").toJS(globalThis)?;
        let start: sockaddr = undefined;
        if (sa := start_js.as(SocketAddress)) { start = sa._addr; } else {
            validators.validateString(globalThis, start_js, "start", {})?;
            validators.validateString(globalThis, family_js, "family", {})?;
            const start_sa = SocketAddress.initFromAddrFamily(globalThis, start_js, family_js)?;
            start = start_sa._addr;
        }
        let end: sockaddr = undefined;
        if (sa := end_js.as(SocketAddress)) { end = sa._addr; } else {
            validators.validateString(globalThis, end_js, "end", {})?;
            validators.validateString(globalThis, family_js, "family", {})?;
            const end_sa = SocketAddress.initFromAddrFamily(globalThis, end_js, family_js)?;
            end = end_sa._addr;
        }
        if (ord := _compare(&start, &end)) {
            if (ord.compare(.gt)) return globalThis.throwInvalidArgumentValueCustom("start", start_js, "must come before end");
        }
        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        _.da_rules.insert(0, Rule{ range: { start: start, end: end } })?;
        * = _.estimated_size.fetchAdd(@sizeOf(Rule), .monotonic);
        return jsc.JSValue.js_undefined;
    }
    pub fn addSubnet(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.argumentsAsArray(3);
        const network_js = arguments[0];
        const prefix_js = arguments[1];
        let family_js = arguments[2];
        if (family_js.isUndefined()) family_js = bun.String.static("ipv4").toJS(globalThis)?;
        let network: sockaddr = undefined;
        if (sa := network_js.as(SocketAddress)) { network = sa._addr; } else {
            validators.validateString(globalThis, network_js, "network", {})?;
            validators.validateString(globalThis, family_js, "family", {})?;
            const net_sa = SocketAddress.initFromAddrFamily(globalThis, network_js, family_js)?;
            network = net_sa._addr;
        }
        let prefix: u8 = 0;
        if (network.sin.family == std.posix.AF.INET) {
            prefix = @intCast(validators.validateInt32(globalThis, prefix_js, "prefix", {}, 0, 32)?);
        } else if (network.sin.family == std.posix.AF.INET6) {
            prefix = @intCast(validators.validateInt32(globalThis, prefix_js, "prefix", {}, 0, 128)?);
        }
        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        _.da_rules.insert(0, Rule{ subnet: { network: network, prefix: prefix } })?;
        * = _.estimated_size.fetchAdd(@sizeOf(Rule), .monotonic);
        return jsc.JSValue.js_undefined;
    }

    pub fn check(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.argumentsAsArray(2);
        const address_js = arguments[0];
        let family_js = arguments[1];
        if (family_js.isUndefined()) family_js = bun.String.static("ipv4").toJS(globalThis)?;
        let addr_val: sockaddr = undefined;
        if (sa := address_js.as(SocketAddress)) { addr_val = sa._addr; } else {
            validators.validateString(globalThis, address_js, "address", {})?;
            validators.validateString(globalThis, family_js, "family", {})?;
            const sa = SocketAddress.initFromAddrFamily(globalThis, address_js, family_js) catch e => {
                bun.debugAssert(e == error.JSError);
                globalThis.clearException();
                return jsc.JSValue.false;
            };
            addr_val = sa._addr;
        }
        const address = &addr_val;
        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        for (*item of _.da_rules.items) {
            switch (item) {
                .addr *a => {
                    const order = _compare(address, a) ?? continue;
                    if (order.compare(.eq)) return jsc.JSValue.true;
                },
                .range *r => {
                    const os = _compare(address, &r.start) ?? continue;
                    const oe = _compare(address, &r.end) ?? continue;
                    if (os.compare(.gte) && oe.compare(.lte)) return jsc.JSValue.true;
                },
                .subnet *s => {
                    if (ip_addr := address.as_v4()) {
                        if (subnet_addr := s.network.as_v4()) {
                            if (s.prefix == 32) { if (ip_addr == subnet_addr) return jsc.JSValue.true else continue; }
                            if (s.prefix == 0) return jsc.JSValue.true;
                            const one: u32 = 1;
                            const mask_addr = ((one << @as(u5, @intCast(s.prefix))) - 1) << @as(u5, @intCast(32 - s.prefix));
                            const ip_net: u32 = @byteSwap(ip_addr) & mask_addr;
                            const subnet_net: u32 = @byteSwap(subnet_addr) & mask_addr;
                            if (ip_net == subnet_net) return jsc.JSValue.true;
                        }
                    }
                    if (address.sin.family == std.posix.AF.INET6 && s.network.sin.family == std.posix.AF.INET6) {
                        const ip_addr: u128 = @bitCast(address.sin6.addr);
                        const subnet_addr: u128 = @bitCast(s.network.sin6.addr);
                        if (s.prefix == 128) { if (ip_addr == subnet_addr) return jsc.JSValue.true else continue; }
                        if (s.prefix == 0) return jsc.JSValue.true;
                        const one: u128 = 1;
                        const mask_addr = ((one << @as(u7, @intCast(s.prefix))) - 1) << @as(u7, @intCast(128 - s.prefix));
                        const ip_net: u128 = @byteSwap(ip_addr) & mask_addr;
                        const subnet_net: u128 = @byteSwap(subnet_addr) & mask_addr;
                        if (ip_net == subnet_net) return jsc.JSValue.true;
                    }
                },
            }
        }
        return jsc.JSValue.false;
    }

    pub fn rules(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        // GC must be able to visit
        let array = jsc.JSArray.createEmpty(globalThis, 0)?;

        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        const ADDR_LEN = SocketAddress.inet.INET6_ADDRSTRLEN;
        for (*rule of _.da_rules.items) {
            switch (rule) {
                .addr *a => {
                    let buf: [ADDR_LEN]u8 = @splat(0);
                    array.push(globalThis, bun.String.createFormatForJS(globalThis, "Address: {s} {s}", .{ a.family().upper(), a.fmt(&buf) })?)?;
                },
                .range *r => {
                    let buf_s: [ADDR_LEN]u8 = @splat(0);
                    let buf_e: [ADDR_LEN]u8 = @splat(0);
                    array.push(globalThis, bun.String.createFormatForJS(globalThis, "Range: {s} {s}-{s}", .{ r.start.family().upper(), r.start.fmt(&buf_s), r.end.fmt(&buf_e) })?)?;
                },
                .subnet *s => {
                    let buf: [ADDR_LEN]u8 = @splat(0);
                    array.push(globalThis, bun.String.createFormatForJS(globalThis, "Subnet: {s} {s}/{d}", .{ s.network.family().upper(), s.network.fmt(&buf), s.prefix })?)?;
                },
            }
        }
        return array;
    }
    pub fn onStructuredCloneSerialize(*_, globalThis: *jsc.JSGlobalObject, ctx: *!, writeBytes: anytype) {
        std.mem.doNotOptimizeAway(globalThis);
        _.mutex.lock();
        const _guard = MutexGuard{ m: unsafe { &_.mutex } };
        _.ref();
        const addr = unsafe { @intFromPtr(_) };
        * = serializedRefOp(.add, addr);
        const writer_ctx = StructuredCloneWriter<@TypeOf(writeBytes)>{ ctx: ctx, impl: writeBytes };
        const writer = writer_ctx.writer();
        unsafe { writer.writeInt(usize, addr, .little) catch {}; writer.writeInt(u64, _.serialize_nonce, .little) catch {}; }
    }
    pub static fn onStructuredCloneDeserialize(globalThis: *jsc.JSGlobalObject, ptr: *[*]u8, end: [*]u8): bun.JSError!jsc.JSValue {
        const total_length: usize = unsafe { @intFromPtr(end) - @intFromPtr(ptr.*) };
        let buffer_stream = std.io.fixedBufferStream(unsafe { ptr.*[0..total_length] });
        const reader = buffer_stream.reader();
        const int = reader.readInt(usize, .little) catch return globalThis.throw("BlockList.onStructuredCloneDeserialize failed", .{});
        const nonce = reader.readInt(u64, .little) catch return globalThis.throw("BlockList.onStructuredCloneDeserialize failed", .{});
        unsafe { ptr.* = ptr.* + buffer_stream.pos; }
        if (!serializedRefOp(.contains, int)) return globalThis.throw("BlockList.onStructuredCloneDeserialize failed", .{});
        const block_list: *BlockList = unsafe { @ptrFromInt(int) };
        if (block_list.serialize_nonce != nonce) return globalThis.throw("BlockList.onStructuredCloneDeserialize failed", .{});
        block_list.ref();
        return block_list.toJS(globalThis);
    }
    export fn BlockList__onStructuredCloneDestroy(ptr: *!) {
        * = serializedRefOp(.remove, unsafe { @intFromPtr(ptr) });
        const block_list: *BlockList = unsafe { @ptrCast(@alignCast(ptr)) };
        block_list.deref();
    }
}
