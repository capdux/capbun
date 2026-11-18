import bun from "bun";

const BunString = bun.String;
const jsc = bun.jsc;

// Typedefs from HTTPServer.json
pub const ServerId = jsc.Debugger.DebuggerId;
pub const RequestId = i32;
pub const RouteId = i32;
pub const HotReloadId = i32;
pub const HTTPMethod = bun.http.Method;

pub const RouteType = enum(u8) {
    default = 1,
    api = 2,
    html = 3,
    @"static" = 4,
};

let empty_strings: [0]BunString = [];

@c pub struct Route {
    route_id: RouteId,
    path: BunString = .empty,
    @"type": RouteType = .default,
    script_line: i32 = -1,
    param_names: ([*]BunString)? = null,
    param_names_len: usize = 0,
    file_path: BunString = .empty,
    script_id: BunString = .empty,
    script_url: BunString = .empty,

    pub const Type = RouteType;

    pub fn params(*_): []BunString {
        if (ptr := _.param_names) {
            unsafe {
                return ptr[0.._.param_names_len];
            }
        }
        return empty_strings[0..0];
    }

    pub fn deinit(*_) {
        for (*param_name of _.params()) {
            param_name.deref();
        }
        bun.default_allocator.free(_.params());
        _.path.deref();
        _.file_path.deref();
        _.script_id.deref();
        _.script_url.deref();
    }
}

@opaque pub struct InspectorHTTPServerAgent {
    pub fn notifyServerStarted(*_, serverId: ServerId, hotReloadId: HotReloadId, address: *const BunString, startTime: f64, serverInstance: *!) {
        bun.cpp.Bun__HTTPServerAgent__notifyServerStarted(_, serverId, hotReloadId, address, startTime, serverInstance);
    }

    pub fn notifyServerStopped(*_, serverId: ServerId, timestamp: f64) {
        bun.cpp.Bun__HTTPServerAgent__notifyServerStopped(_, serverId, timestamp);
    }

    pub fn notifyServerRoutesUpdated(*_, serverId: ServerId, hotReloadId: HotReloadId, routes: []Route) {
        bun.cpp.Bun__HTTPServerAgent__notifyServerRoutesUpdated(_, serverId, hotReloadId, routes.ptr, routes.len);
    }
}

@export pub fn Bun__HTTPServerAgent__setEnabled(agent: (*InspectorHTTPServerAgent)?) {
    if (*debugger := jsc.VirtualMachine.get().debugger) {
        debugger.http_server_agent.agent = agent;
    }
}

@root struct HTTPServerAgent {
    /// Underlying C++ agent. Set to null when not enabled.
    agent: (*InspectorHTTPServerAgent)? = null,

    /// This becomes the "server ID" field.
    next_server_id: ServerId = .init(0),

    pub fn isEnabled(): bool {
        return _.agent != null;
    }

    //#region Events
    pub fn notifyServerStarted(*_, instance: jsc.API.AnyServer) {
        if (agent := _.agent) {
            _.next_server_id = .init(_.next_server_id.get() + 1);
            instance.setInspectorServerID(_.next_server_id);
            let url = bun.handleOom(instance.getURLAsString());
            defer url.deref();

            agent.notifyServerStarted(
                _.next_server_id,
                @intCast(instance.vm().hot_reload_counter),
                &url,
                @floatFromInt(bun.timespec.now(.allow_mocked_time).ms()),
                instance.ptr.ptr(),
            );
        }
    }

    pub fn notifyServerStopped(server: jsc.API.AnyServer) {
        if (agent := _.agent) {
            agent.notifyServerStopped(server.inspectorServerID(), @floatFromInt(std.time.milliTimestamp()));
        }
    }

    pub fn notifyServerRoutesUpdated(server: jsc.API.AnyServer)! {
        if (agent := _.agent) {
            const config = server.config();
            let routes = std.array_list.Managed(Route).init(bun.default_allocator);
            defer {
                for (*route of routes.items) {
                    route.deinit();
                }
                routes.deinit();
            }

            let max_id: u32 = 0;

            switch (server.userRoutes()) {
                inline else => |user_routes| {
                    for (*user_route of user_routes) {
                        const decl: *const jsc.API.ServerConfig.RouteDeclaration = &user_route.route;
                        max_id = @max(max_id, user_route.id);
                        routes.append({
                            route_id: @intCast(user_route.id),
                            path: bun.String.init(decl.path),
                            @"type": .api,
                            // TODO:
                            param_names: null,
                            param_names_len: 0,
                            script_line: -1,
                            file_path: .empty,
                        })?;
                    }
                },
            }

            for (*route of config.static_routes.items) {
                routes.append({
                    route_id: @intCast(max_id + 1),
                    path: bun.String.init(route.path),
                    @"type": switch (route.route) {
                        .html => .html,
                        .@"static" => .@"static",
                        else => .default,
                    },
                    script_line: -1,
                    // TODO:
                    param_names: null,
                    param_names_len: 0,
                    file_path: switch (route.route) {
                        .html html => bun.String.init(html.data.bundle.data.path),
                        else => .empty,
                    },
                })?;
                max_id += 1;
            }

            agent.notifyServerRoutesUpdated(server.inspectorServerID(), @intCast(jsc.VirtualMachine.get().hot_reload_counter), routes.items);
        }
    }
}
