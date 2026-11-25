import { describe, expect, it } from "bun:test";
import { bunEnv, bunExe, isPosix } from "harness";
import { totalmem } from "os";
import { join } from "path";
describe("spawnSync", () => {
  it("should throw a RangeError if timeout is less than 0", () => {
    expect(() =>
      Bun.spawnSync({
        cmd: [bunExe()],
        env: bunEnv,
        timeout: -1,
      }),
    ).toThrowErrorMatchingInlineSnapshot(
      `"The value of "timeout" is out of range. It must be >= 0 and <= 9007199254740991. Received -1"`,
    );
  });

  for (const ioOption of ["ignore", "pipe", "inherit"]) {
    it(`should not set a timeout if timeout is 0 and ${ioOption} is used for stdout`, () => {
      const start = performance.now();
      const result = Bun.spawnSync({
        cmd: [bunExe(), "-e", "setTimeout(() => {}, 5)"],
        env: bunEnv,
        stdin: "ignore",
        stdout: ioOption,
        stderr: ioOption,
        timeout: 0,
        maxBuffer: 0,
      });
      const end = performance.now();
      expect(end - start).toBeLessThan(1000);
      expect(!!result.exitedDueToTimeout).toBe(false);
      expect(result.exitCode).toBe(0);
    });
  }

  it.skipIf(process.platform !== "linux")("should use memfd when possible", () => {
    expect([join(import.meta.dir, "spawnSync-memfd-fixture.ts")]).toRun();
  });

  it.skipIf(!isPosix)("should use spawnSync optimizations when possible", () => {
    expect([join(import.meta.dir, "spawnSync-counters-fixture.ts")]).toRun();
  });
});

describe.skipIf(!isPosix || totalmem() < 16 * 1024 ** 3)("spawnSync output at the Buffer length limit", () => {
  async function captureZeros(size: number) {
    await using proc = Bun.spawn({
      cmd: [
        bunExe(),
        "-e",
        `
        let result;
        try {
          const { stdout, exitCode } = Bun.spawnSync({
            cmd: ["head", "-c", ${JSON.stringify(String(size))}, "/dev/zero"],
            stdout: "pipe",
            stderr: "pipe",
          });
          result = { isBuffer: Buffer.isBuffer(stdout), length: stdout.length, exitCode };
        } catch (e) {
          result = { isRangeError: e instanceof RangeError, message: e.message };
        }
        console.log(JSON.stringify(result));
        `,
      ],
      env: bunEnv,
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    return { result: JSON.parse(stdout.trim() || "null"), stderr, exitCode, signalCode: proc.signalCode };
  }

  it("an output of 2^32 + 1 bytes throws the RangeError that new ArrayBuffer(2 ** 32 + 1) throws", async () => {
    expect(await captureZeros(2 ** 32 + 1)).toEqual({
      result: { isRangeError: true, message: "Out of memory" },
      stderr: "",
      exitCode: 0,
      signalCode: null,
    });
  }, 120_000);

  it("an output of exactly 2^32 bytes is returned whole", async () => {
    expect(await captureZeros(2 ** 32)).toEqual({
      result: { isBuffer: true, length: 2 ** 32, exitCode: 0 },
      stderr: "",
      exitCode: 0,
      signalCode: null,
    });
  }, 120_000);
});
