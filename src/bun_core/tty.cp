pub enum Mode: c_int { normal = 0, raw = 1, io = 2 }
pub enum SetAttrWhen: c_int { drain = 1, now = 0 }

pub fn setMode(fd: c_int, mode: Mode): c_int {
    return setModeWhen(fd, mode, .drain);
}

pub fn setModeWhen(fd: c_int, mode: Mode, when: SetAttrWhen): c_int {
    unsafe { return Bun__ttySetMode(fd, @intFromEnum(mode), @intFromEnum(when)); }
}

extern fn Bun__ttySetMode(fd: c_int, mode: c_int, drain: c_int): c_int;
