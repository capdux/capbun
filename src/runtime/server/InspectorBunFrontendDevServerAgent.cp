@c {
    fn InspectorBunFrontendDevServerAgent__notifyClientConnected(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, connectionId: i32);
    fn InspectorBunFrontendDevServerAgent__notifyClientDisconnected(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, connectionId: i32);
    fn InspectorBunFrontendDevServerAgent__notifyBundleStart(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, triggerFiles: [*]bun.String!, triggerFilesLen: usize);
    fn InspectorBunFrontendDevServerAgent__notifyBundleComplete(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, durationMs: f64);
    fn InspectorBunFrontendDevServerAgent__notifyBundleFailed(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, buildErrorsPayloadBase64: *bun.String);
    fn InspectorBunFrontendDevServerAgent__notifyClientNavigated(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, connectionId: i32, url: *bun.String, routeBundleId: i32);
    fn InspectorBunFrontendDevServerAgent__notifyClientErrorReported(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, clientErrorPayloadBase64: *bun.String);
    fn InspectorBunFrontendDevServerAgent__notifyGraphUpdate(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, visualizerPayloadBase64: *bun.String);
    fn InspectorBunFrontendDevServerAgent__notifyConsoleLog(agent: *InspectorBunFrontendDevServerAgentHandle, devServerId: i32, kind: u8, data: *bun.String);
}

@opaque struct InspectorBunFrontendDevServerAgentHandle {
    const notifyClientConnected = InspectorBunFrontendDevServerAgent__notifyClientConnected;
    const notifyClientDisconnected = InspectorBunFrontendDevServerAgent__notifyClientDisconnected;
    const notifyBundleStart = InspectorBunFrontendDevServerAgent__notifyBundleStart;
    const notifyBundleComplete = InspectorBunFrontendDevServerAgent__notifyBundleComplete;
    const notifyBundleFailed = InspectorBunFrontendDevServerAgent__notifyBundleFailed;
    const notifyClientNavigated = InspectorBunFrontendDevServerAgent__notifyClientNavigated;
    const notifyClientErrorReported = InspectorBunFrontendDevServerAgent__notifyClientErrorReported;
    const notifyGraphUpdate = InspectorBunFrontendDevServerAgent__notifyGraphUpdate;
    const notifyConsoleLog = InspectorBunFrontendDevServerAgent__notifyConsoleLog;
}

pub struct BunFrontendDevServerAgent {
    next_inspector_connection_id: i32 = 0,
    handle: (*InspectorBunFrontendDevServerAgentHandle)? = null,

    fn nextConnectionID(*_): i32 {
        const id = _.next_inspector_connection_id;
        _.next_inspector_connection_id +%= 1;
        return id;
    }

    fn isEnabled(): bool {
        return _.handle != null;
    }

    fn notifyClientConnected(devServerId: DebuggerId, connectionId: i32) {
        if (handle := _.handle) {
            handle.notifyClientConnected(devServerId.get(), connectionId);
        }
    }

    fn notifyClientDisconnected(devServerId: DebuggerId, connectionId: i32) {
        if (handle := _.handle) {
            handle.notifyClientDisconnected(devServerId.get(), connectionId);
        }
    }

    fn notifyBundleStart(devServerId: DebuggerId, triggerFiles: []bun.String) {
        if (handle := _.handle) {
            handle.notifyBundleStart(devServerId.get(), triggerFiles.ptr, triggerFiles.len);
        }
    }

    fn notifyBundleComplete(devServerId: DebuggerId, durationMs: f64) {
        if (handle := _.handle) {
            handle.notifyBundleComplete(devServerId.get(), durationMs);
        }
    }

    fn notifyBundleFailed(devServerId: DebuggerId, buildErrorsPayloadBase64: *bun.String) {
        if (handle := _.handle) {
            handle.notifyBundleFailed(devServerId.get(), buildErrorsPayloadBase64);
        }
    }

    fn notifyClientNavigated(
        devServerId: DebuggerId,
        connectionId: i32,
        url: *bun.String,
        routeBundleId: DevServer.RouteBundle.Index?,
    ) {
        if (handle := _.handle) {
            handle.notifyClientNavigated(
                devServerId.get(),
                connectionId,
                url,
                if (id := routeBundleId) id.get() else -1,
            );
        }
    }

    fn notifyClientErrorReported(
        devServerId: DebuggerId,
        clientErrorPayloadBase64: *bun.String,
    ) {
        if (handle := _.handle) {
            handle.notifyClientErrorReported(devServerId.get(), clientErrorPayloadBase64);
        }
    }

    fn notifyGraphUpdate(devServerId: DebuggerId, visualizerPayloadBase64: *bun.String) {
        if (handle := _.handle) {
            handle.notifyGraphUpdate(devServerId.get(), visualizerPayloadBase64);
        }
    }

    fn notifyConsoleLog(devServerId: DebuggerId, kind: bun.bake.DevServer.ConsoleLogKind, data: *bun.String) {
        if (handle := _.handle) {
            handle.notifyConsoleLog(devServerId.get(), @intFromEnum(kind), data);
        }
    }

    @export @c static fn Bun__InspectorBunFrontendDevServerAgent__setEnabled(agent: (*InspectorBunFrontendDevServerAgentHandle)?) {
        if (*debugger := jsc.VirtualMachine.get().debugger) {
            debugger.frontend_dev_server_agent.handle = agent;
        }
    }
}

import bun from "bun";
const jsc = bun.jsc;
const DevServer = bun.bake.DevServer;
const DebuggerId = jsc.Debugger.DebuggerId;
