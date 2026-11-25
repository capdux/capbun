import { expect, test } from "bun:test";
import { isWindows } from "harness";
import { once } from "node:events";
import { createServer } from "node:net";

test("keepalive", async () => {
  using server = Bun.serve({
    port: 0,
    async fetch(req) {
      return new Response(JSON.stringify(req.headers.toJSON()));
    },
  });
  {
    const res = await fetch(`http://localhost:${server.port}`, {
      keepalive: false,
    });
    const headers = await res.json();
    expect(headers.connection).toBeUndefined();
  }

  {
    const res = await fetch(`http://localhost:${server.port}`, {
      keepalive: true,
    });
    const headers = await res.json();
    expect(headers.connection).toBe("keep-alive");
  }

  {
    const res = await fetch(`http://localhost:${server.port}`, {
      keepalive: false,
      headers: {
        "Connection": "HELLO!",
      },
    });
    const headers = await res.json();
    expect(headers.connection).toBe("HELLO!");
  }
});

test.skipIf(isWindows)("a full keep-alive pool evicts the longest-idle connection", async () => {
  function makeServer() {
    let connections = 0;
    const srv = createServer(sock => {
      connections++;
      let buf = "";
      sock.on("error", () => {});
      sock.on("data", d => {
        buf += d.toString("latin1");
        let i: number;
        while ((i = buf.indexOf("\r\n\r\n")) >= 0) {
          buf = buf.slice(i + 4);
          sock.write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        }
      });
    });
    return { srv, connections: () => connections };
  }
  // One more distinct origin than the pool holds (64). Without eviction the
  // last connection is closed instead of parked and its second request
  // opens a new one.
  const servers = Array.from({ length: 65 }, () => makeServer());
  for (const s of servers) s.srv.listen(0, "127.0.0.1");
  await Promise.all(servers.map(s => once(s.srv, "listening")));
  try {
    const urls = servers.map(s => `http://127.0.0.1:${(s.srv.address() as import("net").AddressInfo).port}/x`);
    for (const url of urls) expect(await (await fetch(url)).text()).toBe("ok");
    expect(await (await fetch(urls[64])).text()).toBe("ok");
    expect(servers[64].connections()).toBe(1);
  } finally {
    for (const s of servers) s.srv.close();
  }
});
