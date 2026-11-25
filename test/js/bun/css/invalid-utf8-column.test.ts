import { expect, test } from "bun:test";
import { bunEnv, bunExe, tempDir } from "harness";
import { join } from "node:path";

async function buildColumn(bytes: number[]): Promise<{ stdout: string; stderr: string; exitCode: number }> {
  using dir = tempDir("css-invalid-utf8", {});
  const css = join(String(dir), "in.css");
  await Bun.write(css, new Uint8Array(bytes));
  await using proc = Bun.spawn({
    cmd: [
      bunExe(),
      "-e",
      `const r = await Bun.build({ entrypoints: [${JSON.stringify(css)}], throw: false });
       const p = r.logs[0]?.position;
       console.log(JSON.stringify({ line: p?.line, column: p?.column }));`,
    ],
    env: bunEnv,
    stdout: "pipe",
    stderr: "pipe",
  });
  const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
  return { stdout: stdout.trim(), stderr: stderr.trim(), exitCode };
}

test.concurrent("lone 0xF0 byte reports column 2, not 3, and does not panic", async () => {
  expect(await buildColumn([0xf0])).toEqual({
    stdout: `{"line":1,"column":2}`,
    stderr: "",
    exitCode: 0,
  });
});

test.concurrent("three stray 0xF0 bytes report column 4, not 7", async () => {
  expect(await buildColumn([0xf0, 0xf0, 0xf0])).toEqual({
    stdout: `{"line":1,"column":4}`,
    stderr: "",
    exitCode: 0,
  });
});

test.concurrent("stray 0xF0 on a non-first line does not skew the column", async () => {
  expect(await buildColumn([...Buffer.from("a{}\n"), 0xf0])).toEqual({
    stdout: `{"line":2,"column":2}`,
    stderr: "",
    exitCode: 0,
  });
});

test.concurrent("valid 4-byte UTF-8 still counts as two UTF-16 columns", async () => {
  expect(await buildColumn([0xf0, 0x9f, 0x98, 0x80])).toEqual({
    stdout: `{"line":1,"column":3}`,
    stderr: "",
    exitCode: 0,
  });
});
