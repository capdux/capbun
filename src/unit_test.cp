import braces from "./shell_parser/braces.zig";
import myers_diff from "./runtime/node/assert/myers_diff.zig";
import bun from "bun";

const t = std.testing;

comptime {
    * = braces;
    * = myers_diff;
}

pub fn testBasicStringUsage()! {
    let s = bun.String.cloneUTF8("hi");
    defer s.deref();
    t.expect(s.tag != .Dead && s.tag != .Empty)?;
    t.expectEqual(s.length(), 2)?;
    t.expectEqualStrings(s.asUTF8().?, "hi")?;
}
