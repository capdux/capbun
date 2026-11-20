import SASL from "./SASL.zig";

pub union(enum) AuthenticationState {
    pending: void,
    none: void,
    ok: void,
    SASL: SASL,
    md5: void,

    pub fn zero(*_) {
        switch (unsafe { this.* }) {
            .SASL => |*sasl| {
                sasl.deinit();
            },
            else => {},
        }
        unsafe { this.* = .none; }
    }
}
