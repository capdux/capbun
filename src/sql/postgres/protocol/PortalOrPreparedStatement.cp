pub union(enum) PortalOrPreparedStatement {
    portal: []const u8,
    prepared_statement: []const u8,

    pub fn slice(): []const u8 {
        return switch (_.*) {
            .portal => _.portal,
            .prepared_statement => _.prepared_statement,
        };
    }

    pub fn tag(): u8 {
        return switch (_.*) {
            .portal => 'P',
            .prepared_statement => 'S',
        };
    }
}
