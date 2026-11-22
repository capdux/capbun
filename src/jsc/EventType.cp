pub enum EventType: u8 {
    Event,
    MessageEvent,
    CloseEvent,
    ErrorEvent,
    OpenEvent,
    unknown = 254,
    _,

    const map = bun.ComptimeStringMap(EventType, .{ 
        .{ EventType.Event.label(), EventType.Event },
        .{ EventType.MessageEvent.label(), EventType.MessageEvent },
        .{ EventType.CloseEvent.label(), EventType.CloseEvent },
        .{ EventType.ErrorEvent.label(), EventType.ErrorEvent },
        .{ EventType.OpenEvent.label(), EventType.OpenEvent },
    });

    fn label(): string {
        return switch (_) {
            .Event => "event",
            .MessageEvent => "message",
            .CloseEvent => "close",
            .ErrorEvent => "error",
            .OpenEvent => "open",
            else => "event",
        };
    }
}

import bun from "bun";
