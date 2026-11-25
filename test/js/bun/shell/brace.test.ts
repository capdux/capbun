import { $ } from "bun";
import { describe, expect, test } from "bun:test";
import { bunEnv, bunExe, tempDir } from "harness";

describe("$.braces", () => {
  test("no-op", () => {
    const result = $.braces(`echo 123`);
    expect(result).toEqual(["echo 123"]);
  });

  test("2", () => {
    const result = $.braces(`echo {123,456}`);
    expect(result).toEqual(["echo 123", "echo 456"]);
  });

  test("3", () => {
    const result = $.braces(`echo {123,456,789}`);
    expect(result).toEqual(["echo 123", "echo 456", "echo 789"]);
  });

  test("nested", () => {
    const result = $.braces(`echo {123,{456,789}}`);
    expect(result).toEqual(["echo 123", "echo 456", "echo 789"]);
  });

  test("nested 2", () => {
    const result = $.braces(`echo {123,{456,789},abc}`);
    expect(result).toEqual(["echo 123", "echo 456", "echo 789", "echo abc"]);
  });

  test("nested sibling product", () => {
    expect($.braces(`{{d,e}{g,h}}`)).toEqual(["dg", "dh", "eg", "eh"]);
  });

  test("nested sibling product with surrounding text", () => {
    expect($.braces(`pre{{a,b}{c,d}}post`)).toEqual(["preacpost", "preadpost", "prebcpost", "prebdpost"]);
  });

  test("nested sibling product mixed with variants", () => {
    expect($.braces(`{a,{b,c}{d,e},f}`)).toEqual(["a", "bd", "be", "cd", "ce", "f"]);
  });

  test("nested sibling product triple", () => {
    expect($.braces(`{{a,b}{c,d}{e,f}}`)).toEqual(["ace", "acf", "ade", "adf", "bce", "bcf", "bde", "bdf"]);
  });

  test("very deeply nested", () => {
    const result = $.braces(`{1,{2,{3,{4,{5,{6,{7,{8,{9,{10,{11,{12,{13,{14,{15,{16,{17}}}}}}}}}}}}}}}}}`);
    expect(result).toEqual([
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
      "10",
      "11",
      "12",
      "13",
      "14",
      "15",
      "16",
      "{17}",
    ]);
  });

  test("empty string", () => {
    expect($.braces("")).toEqual([""]);
    expect($.braces("", { parse: true })).toBeString();
    expect($.braces("", { tokenize: true })).toBeString();
  });

  test("unicode", () => {
    const result = $.braces(`lol {😂,🫵,🤣}`);
    expect(result).toEqual(["lol 😂", "lol 🫵", "lol 🤣"]);
  });
});

describe("$.braces input bounds", () => {
  describe("exactly 65536 variants", () => {
    async function expandInChild(script: string) {
      await using proc = Bun.spawn({
        cmd: [bunExe(), "-e", script],
        env: bunEnv,
        stdout: "pipe",
        stderr: "pipe",
      });
      const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
      return { stdout: stdout.trim(), stderr, exitCode };
    }

    test.concurrent("shell word with 16 flat groups (2^16)", async () => {
      const result = await expandInChild(`
        const word = Buffer.alloc(16 * "{a,b}".length, "{a,b}").toString();
        const words = (await Bun.$\`echo \${{ raw: word }}\`.text()).trimEnd().split(" ");
        console.log(JSON.stringify([words.length, words[0], words.at(-1)]));
      `);
      expect(result).toEqual({
        stdout: JSON.stringify([65536, "aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb"]),
        stderr: "",
        exitCode: 0,
      });
    });

    test.concurrent("$.braces() with 8 nested groups (4^8)", async () => {
      const result = await expandInChild(`
        const word = Buffer.alloc(8 * "{{a,b},{c,d}}".length, "{{a,b},{c,d}}").toString();
        const out = Bun.$.braces(word);
        console.log(JSON.stringify([out.length, out[0], out.at(-1)]));
      `);
      expect(result).toEqual({
        stdout: JSON.stringify([65536, "aaaaaaaa", "dddddddd"]),
        stderr: "",
        exitCode: 0,
      });
    });
  });
});

// A `{...}` group with no top-level comma is literal text in bash. The brace
// lexer used to tokenize it as Open/Close regardless, so a nested `{}` became
// a zero-variant expansion and the expander dropped the rest of the word.
describe("comma-less brace group is literal (bash 5.2)", () => {
  const cases: [string, string[]][] = [
    // Regressions: the `{}` (and the tail after it) was truncated.
    ["x{a,{}}y", ["xay", "x{}y"]],
    ["p{q{},r}s", ["pq{}s", "prs"]],
    ["{a,b{}}z", ["az", "b{}z"]],
    ["{a,{}}z", ["az", "{}z"]],
    ["a{{,}}b", ["a{}b", "a{}b"]],
    // `{foo}` with no comma is literal at any depth.
    ["{a,{b}}", ["a", "{b}"]],
    ["{a{b,c}}", ["{ab}", "{ac}"]],
    // A comma outside every `{...}` does not make one expand.
    ["{foo},x", ["{foo},x"]],
    ["{a},{b}", ["{a},{b}"]],
    // Controls that were already correct.
    ["a{b,{c,d}}e", ["abe", "ace", "ade"]],
    ["{a,b}", ["a", "b"]],
  ];

  for (const [input, expected] of cases) {
    test(`$.braces(${JSON.stringify(input)})`, () => {
      expect($.braces(input)).toEqual(expected);
    });
  }

  test("shell: literal {} inside an expanding group keeps the tail", async () => {
    const script = `
      const { $ } = require("bun");
      $.nothrow();
      const cases = ${JSON.stringify([...cases, ["}{,", ["}{,"]]])};
      for (const [input] of cases) {
        const { stdout } = await $\`echo \${{ raw: input }}\`.quiet();
        console.log(JSON.stringify([input, stdout.toString().slice(0, -1)]));
      }
    `;
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", script],
      env: bunEnv,
      stdout: "pipe",
      stderr: "inherit",
    });
    const [stdout, exitCode] = await Promise.all([proc.stdout.text(), proc.exited]);
    const lines = stdout
      .trim()
      .split("\n")
      .map(l => JSON.parse(l));
    expect(lines).toEqual([...cases.map(([input, expected]) => [input, expected.join(" ")]), ["}{,", "}{,"]]);
    expect(exitCode).toBe(0);
  });

  test("a word with a comma-less brace group and a glob keeps its pattern", async () => {
    using dir = tempDir("shell-brace-literal-glob", { "a.txt": "" });
    const { stderr, exitCode } = await $`echo {x},*.txt`.cwd(String(dir)).nothrow().quiet();
    expect({ stderr: stderr.toString(), exitCode }).toEqual({
      stderr: "bun: no matches found: {x},*.txt\n",
      exitCode: 1,
    });
  });
});
