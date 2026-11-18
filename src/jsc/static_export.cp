pub struct StaticExport {
    Type: type,
    symbol_name: string,
    local_name: string,
    Parent: type,

    pub fn Decl(): std.builtin.Type.Declaration {
        return comptime std.@"meta".declarationInfo(_.Parent, _.local_name);
    }

    pub fn wrappedName(): string {
        return comptime "wrap" ++ _.symbol_name;
    }
}
