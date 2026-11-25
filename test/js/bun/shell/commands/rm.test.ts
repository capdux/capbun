/**
 * These tests are derived from the [deno_task_shell](https://github.com/denoland/deno_task_shell/) rm tests, which are developed and maintained by the Deno authors.
 * Copyright 2018-2023 the Deno authors.
 *
 * This code is licensed under the MIT License: https://opensource.org/licenses/MIT
 */
import { $ } from "bun";
import { beforeAll, describe, expect, setDefaultTimeout, test } from "bun:test";
import { bunEnv, bunExe, isWindows, tempDir, tempDirWithFiles } from "harness";
import { mkdirSync, writeFileSync } from "node:fs";
import path from "path";
import { createTestBuilder, sortedShellOutput } from "../util";
const TestBuilder = createTestBuilder(import.meta.path);

const fileExists = async (path: string): Promise<boolean> =>
  $`ls -d ${path}`.then(o => o.stdout.toString() === `${path}\n`);

$.nothrow();

beforeAll(() => {
  setDefaultTimeout(1000 * 60 * 5);
});

const BUN = process.argv0;
const DEV_NULL = process.platform === "win32" ? "NUL" : "/dev/null";

describe.concurrent("bunshell rm", () => {
  TestBuilder.command`echo ${packagejson()} > package.json; ${BUN} install --linker hoisted &> ${DEV_NULL}; rm -rf node_modules/`
    .ensureTempDir()
    .doesNotExist("node_modules")
    .runAsTest("node_modules");

  test("force", async () => {
    const files = {
      "existent.txt": "",
    };
    const tempdir = tempDirWithFiles("rmforce", files);

    expect(await $`rm -f ${tempdir}/non_existent.txt`.then(o => o.exitCode)).toBe(0);

    {
      const { stderr, exitCode } = await $`rm ${tempdir}/non_existent.txt`;
      expect(stderr.toString()).toEqual(`rm: ${tempdir}/non_existent.txt: No such file or directory\n`);
      expect(exitCode).toBe(1);
    }

    {
      expect(await fileExists(`${tempdir}/existent.txt`)).toBeTrue();
      const { stdout, exitCode } = await $`rm -v ${tempdir}/existent.txt`;
      expect(stdout.toString()).toEqual(`${tempdir}/existent.txt\n`);
      expect(exitCode).toBe(0);
      expect(await fileExists(`${tempdir}/existent.txt`)).toBeFalse();
    }
  });

  test("recursive", async () => {
    const files = {
      "existent.txt": "",
    };

    const tempdir = tempDirWithFiles("rmrecursive", files);

    // test on a file
    {
      expect(await fileExists(`${tempdir}/existent.txt`)).toBeTrue();
      const { stdout, stderr, exitCode } = await $`rm -rv ${tempdir}/existent.txt`;
      expect(stderr.length).toBe(0);
      expect(stdout.toString()).toEqual(`${tempdir}/existent.txt\n`);
      expect(exitCode).toBe(0);
      expect(await fileExists(`${tempdir}/existent.txt`)).toBeFalse();
    }

    // test on a directory
    {
      let subDir = path.join(tempdir, "folder", "sub");
      mkdirSync(subDir, { recursive: true });
      let subFile = path.join(subDir, "file.txt");
      writeFileSync(subFile, "test");
      const { stdout, exitCode } = await $`rm -rv ${path.join(tempdir, "folder")}`;
      expect(sortedShellOutput(stdout.toString())).toEqual(
        sortedShellOutput(`${subFile}\n${subDir}\n${path.join(tempdir, "folder")}\n`),
      );
      expect(exitCode).toBe(0);

      expect(await fileExists(subDir)).toBeFalse();
      expect(await fileExists(subFile)).toBeFalse();
      {
        const { stdout, stderr, exitCode } = await $`ls ${tempdir}`;
        console.log("NICE", stdout.toString(), exitCode);
        console.log("NICE", stderr.toString());
      }
      expect(await fileExists(tempdir)).toBeTrue();
    }

    // test with cwd
    {
      const tmpdir = TestBuilder.tmpdir();
      const { stdout, stderr } =
        await $`mkdir foo; touch ./foo/lol ./foo/nice ./foo/lmao; mkdir foo/bar; touch ./foo/bar/great; touch ./foo/bar/wow; rm -rfv foo/`.cwd(
          tmpdir,
        );
      expect(sortedShellOutput(stdout.toString())).toEqual(
        sortedShellOutput(
          `foo/lol
foo/nice
foo/lmao
foo/bar
foo/bar/great
foo/bar/wow
foo/
`,
        ),
      );
    }
  });

  test("dir", async () => {
    const files = {
      "existent.txt": "",
      "sub_dir": {},
      "sub_dir_files/file.txt": "",
    };

    const tempdir = tempDirWithFiles("rmdir", files);

    {
      const { stdout, stderr, exitCode } = await $`rm -d ${tempdir}/existent.txt`;
      expect(exitCode).toBe(0);
      expect(await fileExists(`${tempdir}/existent.txt`)).toBeFalse();
    }

    {
      const { stdout, stderr, exitCode } = await $`rm -d ${tempdir}/sub_dir`;
      console.log(stderr.toString());
      expect(exitCode).toBe(0);
      expect(await fileExists(`${tempdir}/sub_dir`)).toBeFalse();
    }

    {
      const { stdout, stderr, exitCode } = await $`rm -d ${tempdir}/sub_dir_files`;
      console.log(exitCode, "STDOUT", stdout.toString());
      expect(stderr.toString()).toEqual(`rm: ${tempdir}/sub_dir_files: Directory not empty\n`);
      expect(exitCode).toBe(1);
      expect(await fileExists(`${tempdir}/sub_dir_files`)).toBeTrue();
    }
  });
});

function packagejson() {
  return `{
  "name": "dummy",
  "dependencies": {
    "@biomejs/biome": "^1.5.3",
    "@vscode/debugadapter": "^1.61.0",
    "esbuild": "^0.17.15",
    "eslint": "^8.20.0",
    "eslint-config-prettier": "^8.5.0",
    "mitata": "^0.1.3",
    "peechy": "0.4.34",
    "prettier": "3.2.2",
    "react": "next",
    "react-dom": "next",
    "source-map-js": "^1.0.2",
    "typescript": "^5.0.2"
  },
  "devDependencies": {
    "@types/react": "^18.0.25",
    "@typescript-eslint/eslint-plugin": "^5.31.0",
    "@typescript-eslint/parser": "^5.31.0"
  },
  "version": "0.0.0"
}`;
}

test("operands longer than the path scratch buffers are reported, not a crash", async () => {
  using dir = tempDir("rm-long-operand", { "short.txt": "" });
  const long = Buffer.alloc(1100, "a").toString();
  const longer = Buffer.alloc(8192, "b").toString();
  const absolute = path.join(String(dir), long);
  const upToRoot = Buffer.alloc(6000, "../").toString();

  const fixture = /* ts */ `
    import { $ } from "bun";
    import { existsSync } from "node:fs";
    $.nothrow();
    const { LONG, LONGER, ABSOLUTE, UP_TO_ROOT } = process.env;
    const run = async (...args: string[]) => {
      const { exitCode, stderr } = await $\`rm \${args}\`.quiet();
      return { exitCode, stderr: stderr.toString() };
    };
    const results = {
      relative: await run(LONG!),
      absolute: await run(ABSOLUTE!),
      overJoinBuffer: await run(LONGER!),
      mixed: { ...(await run(LONG!, "short.txt")), shortRemoved: !existsSync("short.txt") },
      upToRoot: UP_TO_ROOT ? await run(UP_TO_ROOT) : undefined,
    };
    console.log(JSON.stringify(results));
  `;
  await using proc = Bun.spawn({
    cmd: [bunExe(), "-e", fixture],
    env: {
      ...bunEnv,
      LONG: long,
      LONGER: longer,
      ABSOLUTE: absolute,
      ...(isWindows ? {} : { UP_TO_ROOT: upToRoot }),
    },
    cwd: String(dir),
    stdout: "pipe",
    stderr: "pipe",
  });
  const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
  expect(stderr).toBe("");

  const tooLong = (operand: string) =>
    isWindows ? expect.stringMatching(/^rm: /) : `rm: ${operand}: File name too long\n`;
  expect(JSON.parse(stdout)).toEqual({
    relative: { exitCode: 1, stderr: tooLong(long) },
    absolute: { exitCode: 1, stderr: tooLong(absolute) },
    overJoinBuffer: { exitCode: 1, stderr: tooLong(longer) },
    mixed: { exitCode: 1, stderr: tooLong(long), shortRemoved: true },
    ...(isWindows ? {} : { upToRoot: { exitCode: 1, stderr: 'rm: "/" may not be removed\n' } }),
  });
  expect(exitCode).toBe(0);
});

// The recursive walk joined every entry onto its directory's path inside a
// fixed-size path buffer on the worker thread, so a tree deeper than PATH_MAX
// aborted the whole process instead of failing that entry. Files and
// directories take different joins, so one tree of each. Runs in a child
// process so the abort shows up as a failed assertion. Windows has a
// different path limit, and its shell rm is bounded differently.
test.skipIf(process.platform === "win32")(
  "recursive rm reports an entry deeper than PATH_MAX instead of crashing",
  async () => {
    // Linux PATH_MAX is 4096, every other POSIX platform Bun runs on has 1024.
    const PATH_MAX = process.platform === "linux" ? 4096 : 1024;
    using base = tempDir("rm-deep-walk", {});
    const fixture = /* ts */ `
      import { $ } from "bun";
      import { existsSync, mkdirSync, writeFileSync } from "node:fs";
      import { join } from "node:path";
      $.nothrow();

      const base = process.env.BASE!;
      // Long enough that a 100 byte entry inside the directory is past
      // PATH_MAX, short enough that the directory itself is still well inside.
      const deepLength = Number(process.env.PATH_MAX) - 64;
      const component = Buffer.alloc(100, "d").toString();
      // <base>/<tag>/ddd.../ddd... with an absolute path of deepLength bytes.
      function deepDir(tag: string): string {
        let dir = join(base, tag);
        while (Buffer.byteLength(dir) + 1 + component.length <= deepLength) dir = join(dir, component);
        const rest = deepLength - Buffer.byteLength(dir) - 1;
        if (rest > 0) dir = join(dir, Buffer.alloc(rest, "e").toString());
        mkdirSync(dir, { recursive: true });
        return dir;
      }

      // An entry whose full path is longer than PATH_MAX can only be created
      // relative to its directory.
      const fileDir = deepDir("file");
      const fileName = Buffer.alloc(100, "f").toString();
      process.chdir(fileDir);
      writeFileSync(fileName, "");

      const dirDir = deepDir("dir");
      const dirName = Buffer.alloc(100, "s").toString();
      process.chdir(dirDir);
      mkdirSync(dirName);

      process.chdir(base);
      mkdirSync(join(base, "plain", "sub"), { recursive: true });

      const run = async (operand: string) => {
        const { exitCode, stderr } = await $\`rm -rf \${operand}\`.cwd(base).quiet();
        return { exitCode, stderr: stderr.toString() };
      };
      console.log(
        JSON.stringify({
          file: { ...(await run(join(base, "file"))), entry: join(fileDir, fileName), dirKept: existsSync(fileDir) },
          dir: { ...(await run(join(base, "dir"))), entry: join(dirDir, dirName), dirKept: existsSync(dirDir) },
          plain: { ...(await run(join(base, "plain"))), removed: !existsSync(join(base, "plain")) },
        }),
      );
    `;
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", fixture],
      env: { ...bunEnv, BASE: String(base), PATH_MAX: String(PATH_MAX) },
      cwd: String(base),
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    expect(stderr).toBe("");
    const results = JSON.parse(stdout);
    // The entries are what rm could not remove, and this is why: a path of
    // PATH_MAX bytes has no room left for its NUL.
    expect(Buffer.byteLength(results.file.entry)).toBeGreaterThanOrEqual(PATH_MAX);
    expect(Buffer.byteLength(results.dir.entry)).toBeGreaterThanOrEqual(PATH_MAX);
    expect(results).toEqual({
      file: {
        exitCode: 1,
        stderr: `rm: ${results.file.entry}: File name too long\n`,
        entry: expect.stringMatching(/\/f{100}$/),
        dirKept: true,
      },
      dir: {
        exitCode: 1,
        stderr: `rm: ${results.dir.entry}: File name too long\n`,
        entry: expect.stringMatching(/\/s{100}$/),
        dirKept: true,
      },
      plain: { exitCode: 0, stderr: "", removed: true },
    });
    expect(exitCode).toBe(0);
  },
);
