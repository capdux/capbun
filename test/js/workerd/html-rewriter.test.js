import { afterAll, beforeAll, describe, expect, it } from "bun:test";
import fs from "fs";
import { gcTick, tls, tmpdirSync } from "harness";
import path, { join } from "path";
import { setImmediate as setImmediatePromise } from "timers/promises";
var setTimeoutAsync = (fn, delay) => {
  return new Promise((resolve, reject) => {
    setTimeout(() => {
      try {
        resolve(fn());
      } catch (e) {
        reject(e);
      }
    }, delay);
  });
};

describe("HTMLRewriter", () => {
  it("error handling", () => {
    expect(() => new HTMLRewriter().transform(Symbol("ok"))).toThrow();
  });

  it("error inside element handler", () => {
    expect(() =>
      new HTMLRewriter()
        .on("div", {
          element(element) {
            throw new Error("test");
          },
        })
        .transform(new Response("<div>hello</div>")),
    ).toThrow("test");
  });

  it("error inside element handler (string)", () => {
    expect(() =>
      new HTMLRewriter()
        .on("div", {
          element(element) {
            throw new Error("test");
          },
        })
        .transform("<div>hello</div>"),
    ).toThrow("test");
  });

  it("fast async error inside element handler", () => {
    let caught = false;
    try {
      new HTMLRewriter()
        .on("div", {
          async element(element) {
            await setImmediatePromise();
            throw new Error("test");
          },
        })
        .transform(new Response("<div>hello</div>"));
      expect.unreachable();
    } catch (e) {
      caught = true;
      expect(e.message).toBe("test");
    } finally {
      expect(caught).toBeTrue();
    }
  });

  it("slow async error inside element handler", () => {
    let caught = false;
    try {
      new HTMLRewriter()
        .on("div", {
          async element(element) {
            await Bun.sleep(1);
            throw new Error("test");
          },
        })
        .transform(new Response("<div>hello</div>"));
      expect.unreachable();
    } catch (e) {
      caught = true;
      expect(e.message).toBe("test");
    } finally {
      expect(caught).toBeTrue();
    }
  });

  // A direct stream's pull() receives the pump controller itself, so user code
  // can hand it to an API that roots its argument with gcProtect (another
  // rewriter's .on() does). The pipe never protects the controller, so when a
  // failing rewrite detaches it, the detach must not gcUnprotect it either:
  // that cancelled the other holder's root and the next GC collected the
  // controller out from under it. The unreferenced run shows the controller
  // is otherwise collectable, so "alive" below really is the holder's root.
  it("detaching the pump controller of a failed rewrite keeps a gcProtect held elsewhere on it", async () => {
    const fixture = /* js */ `
      const holder = new HTMLRewriter();
      async function failedRewrite(handToHolder) {
        let ref;
        const input = new Response(
          new ReadableStream({
            type: "direct",
            pull(controller) {
              ref = new WeakRef(controller);
              if (handToHolder) holder.on("*", controller);
              // The element handler below throws inside this write, so the pipe
              // fails and detaches the controller while it is still attached.
              controller.write("<p>x</p>");
            },
          }),
        );
        const out = new HTMLRewriter()
          .on("p", {
            element() {
              throw new Error("handler threw");
            },
          })
          .transform(input);
        const outcome = await out.text().then(
          () => "resolved",
          err => "rejected with " + err.message,
        );
        return { ref, outcome };
      }
      for (const handToHolder of [false, true]) {
        const { ref, outcome } = await failedRewrite(handToHolder);
        for (let i = 0; i < 3; i++) {
          await new Promise(resolve => setImmediate(resolve));
          Bun.gc(true);
        }
        const liveness = ref.deref() === undefined ? "collected" : "alive";
        console.log((handToHolder ? "handed to holder.on()" : "unreferenced") + ": " + outcome + ", controller " + liveness);
      }
    `;
    await using proc = Bun.spawn({
      cmd: [bunExe(), "-e", fixture],
      env: bunEnv,
      stdout: "pipe",
      stderr: "pipe",
    });
    const [stdout, stderr, exitCode] = await Promise.all([proc.stdout.text(), proc.stderr.text(), proc.exited]);
    expect(stdout).toBe(
      [
        "unreferenced: rejected with handler threw, controller collected",
        "handed to holder.on(): rejected with handler threw, controller alive",
        "",
      ].join("\n"),
    );
    expect(stderr).toBe("");
    expect(exitCode).toBe(0);
  });

  it("HTMLRewriter: async replacement", async () => {
    await gcTick();
    const res = new HTMLRewriter()
      .on("div", {
        async element(element) {
          await setTimeoutAsync(() => {
            element.setInnerContent("<span>replace</span>", { html: true });
          }, 5);
        },
      })
      .transform(new Response("<div>example.com</div>"));
    await gcTick();
    expect(await res.text()).toBe("<div><span>replace</span></div>");
    await gcTick();
  });

  it("HTMLRewriter handles Symbol invalid type error", async () => {
    expect(() => new HTMLRewriter().transform(new Response(Symbol("ok")))).toThrow();
    expect(() => new HTMLRewriter().transform(Symbol("ok"))).toThrow();
  });

  it("HTMLRewriter: async replacement using fetch + Bun.serve", async () => {
    await gcTick();
    let content;
    {
      using contentServer = Bun.serve({
        port: 0,
        fetch(req) {
          return new Response("<h1>Hello from content server</h1>", {
            headers: { "Content-Type": "text/html" },
          });
        },
      });

      using server = Bun.serve({
        port: 0,
        fetch(req) {
          return new HTMLRewriter()
            .on("div", {
              async element(element) {
                content = await fetch(`http://localhost:${contentServer.port}/`).then(res => res.text());
                element.setInnerContent(content, { html: true });
              },
            })
            .transform(new Response("<div>example.com</div>"));
        },
      });

      await gcTick();
      const url = `http://localhost:${server.port}`;
      expect(await fetch(url).then(res => res.text())).toBe(`<div>${content}</div>`);
      await gcTick();
    }
  });

  for (let input of [new Response("<div>hello</div>"), "<div>hello</div>"]) {
    it("supports element handlers with input " + input.constructor.name, async () => {
      var rewriter = new HTMLRewriter();
      rewriter.on("div", {
        element(element) {
          element.setInnerContent("<blink>it worked!</blink>", { html: true });
        },
      });
      var output = rewriter.transform(input);
      expect(typeof input === "string" ? output : await output.text()).toBe("<div><blink>it worked!</blink></div>");
    });
  }

  it("(from file) supports element handlers", async () => {
    var rewriter = new HTMLRewriter();
    rewriter.on("div", {
      element(element) {
        element.setInnerContent("<blink>it worked!</blink>", { html: true });
      },
    });
    const filePath = join(tmpdirSync(), "html-rewriter.txt.js");
    await Bun.write(filePath, "<div>hello</div>");
    var output = rewriter.transform(new Response(Bun.file(filePath)));
    expect(await output.text()).toBe("<div><blink>it worked!</blink></div>");
  });

  it("supports attribute iterator", async () => {
    var rewriter = new HTMLRewriter();
    var expected = [
      ["first", ""],
      ["second", "alrihgt"],
      ["third", "123"],
      ["fourth", "5"],
      ["fifth", "helloooo"],
    ];
    rewriter.on("div", {
      element(element2) {
        for (let attr of element2.attributes) {
          const stack = expected.shift();
          expect(stack[0]).toBe(attr[0]);
          expect(stack[1]).toBe(attr[1]);
        }
      },
    });
    var input = new Response('<div first second="alrihgt" third="123" fourth=5 fifth=helloooo>hello</div>');
    var output = rewriter.transform(input);
    expect(await output.text()).toBe('<div first second="alrihgt" third="123" fourth=5 fifth=helloooo>hello</div>');
    expect(expected.length).toBe(0);
  });

  it("attribute iterator is detached after handler returns", async () => {
    // The lol-html attribute iterator borrows from the element's attribute
    // buffer, which is freed when the handler returns. Previously we leaked
    // the raw iterator pointer to JS, so calling .next() after the transform
    // read freed memory. Now the iterator is detached and reports done.
    let leaked;
    let partiallyConsumed;
    const inside = [];
    await new HTMLRewriter()
      .on("div", {
        element(el) {
          // A fresh iterator leaked without being touched.
          leaked = el.attributes;
          // A second iterator fully consumed inside the handler must still work.
          for (const pair of el.attributes) inside.push(pair);
          // A third iterator partially consumed then leaked.
          partiallyConsumed = el.attributes;
          partiallyConsumed.next();
        },
      })
      .transform(new Response('<div a="1" b="2" c="3"></div>'))
      .text();

    expect(inside).toEqual([
      ["a", "1"],
      ["b", "2"],
      ["c", "3"],
    ]);

    expect(leaked.next()).toEqual({ done: true, value: undefined });
    expect(partiallyConsumed.next()).toEqual({ done: true, value: undefined });
    // for..of over a detached iterator should simply not iterate.
    expect([...leaked]).toEqual([]);
  });

  it("attribute iterator is detached when attributes are mutated", async () => {
    // setAttribute pushes onto the backing Vec<Attribute> (possible realloc);
    // removeAttribute shifts elements. Either invalidates a live slice::Iter.
    let afterSet, afterRemove;
    let fresh = [];
    await new HTMLRewriter()
      .on("div", {
        element(el) {
          const it1 = el.attributes;
          el.setAttribute("x", "9");
          afterSet = it1.next();

          const it2 = el.attributes;
          el.removeAttribute("a");
          afterRemove = it2.next();

          // An iterator obtained after the mutations still sees the final state.
          fresh = [...el.attributes];
        },
      })
      .transform(new Response('<div a="1" b="2" c="3"></div>'))
      .text();

    expect(afterSet).toEqual({ done: true, value: undefined });
    expect(afterRemove).toEqual({ done: true, value: undefined });
    expect(fresh).toEqual([
      ["b", "2"],
      ["c", "3"],
      ["x", "9"],
    ]);
  });

  it("handles element specific mutations", async () => {
    // prepend/append
    let res = new HTMLRewriter()
      .on("p", {
        element(element) {
          element.prepend("<span>prepend</span>");
          element.prepend("<span>prepend html</span>", { html: true });
          element.append("<span>append</span>");
          element.append("<span>append html</span>", { html: true });
        },
      })
      .transform(new Response("<p>test</p>"));
    expect(await res.text()).toBe(
      [
        "<p>",
        "<span>prepend html</span>",
        "&lt;span&gt;prepend&lt;/span&gt;",
        "test",
        "&lt;span&gt;append&lt;/span&gt;",
        "<span>append html</span>",
        "</p>",
      ].join(""),
    );

    // setInnerContent
    res = new HTMLRewriter()
      .on("p", {
        element(element) {
          element.setInnerContent("<span>replace</span>");
        },
      })
      .transform(new Response("<p>test</p>"));
    expect(await res.text()).toBe("<p>&lt;span&gt;replace&lt;/span&gt;</p>");
    res = new HTMLRewriter()
      .on("p", {
        element(element) {
          element.setInnerContent("<span>replace</span>", { html: true });
        },
      })
      .transform(new Response("<p>test</p>"));
    expect(await res.text()).toBe("<p><span>replace</span></p>");

    // removeAndKeepContent
    res = new HTMLRewriter()
      .on("p", {
        element(element) {
          element.removeAndKeepContent();
        },
      })
      .transform(new Response("<p>test</p>"));
    expect(await res.text()).toBe("test");
  });

  it("handles element class properties", async () => {
    class Handler {
      constructor(content) {
        this.content = content;
      }

      // noinspection JSUnusedGlobalSymbols
      element(element) {
        element.setInnerContent(this.content);
      }
    }
    const res = new HTMLRewriter().on("p", new Handler("new")).transform(new Response("<p>test</p>"));
    expect(await res.text()).toBe("<p>new</p>");
  });

  const commentsMutationsInput = "<p><!--test--></p>";
  const commentsMutationsExpected = {
    beforeAfter: [
      "<p>",
      "&lt;span&gt;before&lt;/span&gt;",
      "<span>before html</span>",
      "<!--test-->",
      "<span>after html</span>",
      "&lt;span&gt;after&lt;/span&gt;",
      "</p>",
    ].join(""),
    replace: "<p>&lt;span&gt;replace&lt;/span&gt;</p>",
    replaceHtml: "<p><span>replace</span></p>",
    remove: "<p></p>",
  };

  const commentPropertiesMacro = async func => {
    const res = func(new HTMLRewriter(), comment => {
      expect(comment.removed).toBe(false);
      expect(comment.text).toBe("test");
      comment.text = "new";
      expect(comment.text).toBe("new");
    }).transform(new Response("<p><!--test--></p>"));
    expect(await res.text()).toBe("<p><!--new--></p>");
  };

  it("HTMLRewriter: handles comment properties", () =>
    commentPropertiesMacro((rw, comments) => {
      rw.on("p", { comments });
      return rw;
    }));

  it("selector tests", async () => {
    const checkSelector = async (selector, input, expected) => {
      const res = new HTMLRewriter()
        .on(selector, {
          element(element) {
            element.setInnerContent("new");
          },
        })
        .transform(new Response(input));
      expect(await res.text()).toBe(expected);
    };

    await checkSelector("*", "<h1>1</h1><p>2</p>", "<h1>new</h1><p>new</p>");
    await checkSelector("p", "<h1>1</h1><p>2</p>", "<h1>1</h1><p>new</p>");
    await checkSelector(
      "p:nth-child(2)",
      "<div><p>1</p><p>2</p><p>3</p></div>",
      "<div><p>1</p><p>new</p><p>3</p></div>",
    );
    await checkSelector(
      "p:first-child",
      "<div><p>1</p><p>2</p><p>3</p></div>",
      "<div><p>new</p><p>2</p><p>3</p></div>",
    );
    await checkSelector(
      "p:nth-of-type(2)",
      "<div><p>1</p><h1>2</h1><p>3</p><h1>4</h1><p>5</p></div>",
      "<div><p>1</p><h1>2</h1><p>new</p><h1>4</h1><p>5</p></div>",
    );
    await checkSelector(
      "p:first-of-type",
      "<div><h1>1</h1><p>2</p><p>3</p></div>",
      "<div><h1>1</h1><p>new</p><p>3</p></div>",
    );
    await checkSelector(
      "p:not(:first-child)",
      "<div><p>1</p><p>2</p><p>3</p></div>",
      "<div><p>1</p><p>new</p><p>new</p></div>",
    );
    await checkSelector("p.red", '<p class="red">1</p><p>2</p>', '<p class="red">new</p><p>2</p>');
    await checkSelector("h1#header", '<h1 id="header">1</h1><h1>2</h1>', '<h1 id="header">new</h1><h1>2</h1>');
    await checkSelector("p[data-test]", "<p data-test>1</p><p>2</p>", "<p data-test>new</p><p>2</p>");
    await checkSelector(
      'p[data-test="one"]',
      '<p data-test="one">1</p><p data-test="two">2</p>',
      '<p data-test="one">new</p><p data-test="two">2</p>',
    );
    await checkSelector(
      'p[data-test="one" i]',
      '<p data-test="one">1</p><p data-test="OnE">2</p><p data-test="two">3</p>',
      '<p data-test="one">new</p><p data-test="OnE">new</p><p data-test="two">3</p>',
    );
    await checkSelector(
      'p[data-test="one" s]',
      '<p data-test="one">1</p><p data-test="OnE">2</p><p data-test="two">3</p>',
      '<p data-test="one">new</p><p data-test="OnE">2</p><p data-test="two">3</p>',
    );
    await checkSelector(
      'p[data-test~="two"]',
      '<p data-test="one two three">1</p><p data-test="one two">2</p><p data-test="one">3</p>',
      '<p data-test="one two three">new</p><p data-test="one two">new</p><p data-test="one">3</p>',
    );
    await checkSelector(
      'p[data-test^="a"]',
      '<p data-test="a1">1</p><p data-test="a2">2</p><p data-test="b1">3</p>',
      '<p data-test="a1">new</p><p data-test="a2">new</p><p data-test="b1">3</p>',
    );
    await checkSelector(
      'p[data-test$="1"]',
      '<p data-test="a1">1</p><p data-test="a2">2</p><p data-test="b1">3</p>',
      '<p data-test="a1">new</p><p data-test="a2">2</p><p data-test="b1">new</p>',
    );
    await checkSelector(
      'p[data-test*="b"]',
      '<p data-test="abc">1</p><p data-test="ab">2</p><p data-test="a">3</p>',
      '<p data-test="abc">new</p><p data-test="ab">new</p><p data-test="a">3</p>',
    );
    await checkSelector(
      'p[data-test|="a"]',
      '<p data-test="a">1</p><p data-test="a-1">2</p><p data-test="a2">3</p>',
      '<p data-test="a">new</p><p data-test="a-1">new</p><p data-test="a2">3</p>',
    );
    await checkSelector(
      "div span",
      "<div><h1><span>1</span></h1><span>2</span><b>3</b></div>",
      "<div><h1><span>new</span></h1><span>new</span><b>3</b></div>",
    );
    await checkSelector(
      "div > span",
      "<div><h1><span>1</span></h1><span>2</span><b>3</b></div>",
      "<div><h1><span>1</span></h1><span>new</span><b>3</b></div>",
    );
  });

  it("supports deleting innerContent", async () => {
    expect(
      await new HTMLRewriter()
        .on("div", {
          element(elem) {
            // https://github.com/oven-sh/bun/issues/2323
            elem.setInnerContent("");
          },
        })
        .transform(new Response("<div>content</div>"))
        .text(),
    ).toEqual("<div></div>");
  });

  it("supports deleting innerHTML", async () => {
    expect(
      await new HTMLRewriter()
        .on("div", {
          element(elem) {
            // https://github.com/oven-sh/bun/issues/2323
            elem.setInnerContent("", { html: true });
          },
        })
        .transform(new Response("<div><span>content</span></div>"))
        .text(),
    ).toEqual("<div></div>");
  });

  it("it supports lastInTextNode", async () => {
    let lastInTextNode;

    await new HTMLRewriter()
      .on("p", {
        text(text) {
          lastInTextNode ??= text.lastInTextNode;
        },
      })
      .transform(new Response("<p>Lorem ipsum!</p>"))
      .text();

    expect(lastInTextNode).toBeBoolean();
  });

  it("it supports selfClosing", async () => {
    const selfClosing = {};
    await new HTMLRewriter()
      .on("*", {
        element(el) {
          selfClosing[el.tagName] = el.selfClosing;
        },
      })

      .transform(new Response("<p>Lorem ipsum!<br></p><div />"))
      .text();

    expect(selfClosing).toEqual({
      p: false,
      br: false,
      div: true,
    });
  });

  it("it supports canHaveContent", async () => {
    const canHaveContent = {};
    await new HTMLRewriter()
      .on("*", {
        element(el) {
          canHaveContent[el.tagName] = el.canHaveContent;
        },
      })
      .transform(new Response("<p>Lorem ipsum!<br></p><div /><svg><circle /></svg>"))
      .text();

    expect(canHaveContent).toEqual({
      p: true,
      br: false,
      div: true,
      svg: true,
      circle: false,
    });
  });
});

// By not segfaulting, this test passes
it("#3334 regression", async () => {
  for (let i = 0; i < 10; i++) {
    const headers = new Headers({
      "content-type": "text/html",
    });
    const response = new Response("<div>content</div>", { headers });

    const result = await new HTMLRewriter()
      .on("div", {
        element(elem) {
          elem.setInnerContent("new");
        },
      })
      .transform(response)
      .text();
    expect(result).toEqual("<div>new</div>");
  }
  Bun.gc(true);
});

it("#3489", async () => {
  var el;
  await new HTMLRewriter()
    .on("p", {
      element(element) {
        el = element.getAttribute("id");
      },
    })
    .transform(new Response('<p id="Šžõäöü"></p>'))
    .text();
  expect(el).toEqual("Šžõäöü");
});

it("get attribute - ascii", async () => {
  for (let i = 0; i < 10; i++) {
    var el;
    await new HTMLRewriter()
      .on("p", {
        element(element) {
          el = element.getAttribute("id");
        },
      })
      .transform(new Response(`<p id="asciii"></p>`))
      .text();
    expect(el).toEqual("asciii");
  }
});

it("#3520", async () => {
  const pairs = [];

  await new HTMLRewriter()
    .on("p", {
      element(element) {
        for (const pair of element.attributes) {
          pairs.push(pair);
        }
      },
    })
    .transform(new Response('<p šž="Õäöü" ab="Õäöü" šž="Õäöü" šž="dc" šž="🕵🏻"></p>'))
    .text();

  expect(pairs).toEqual([
    ["šž", "Õäöü"],
    ["ab", "Õäöü"],
    ["šž", "Õäöü"],
    ["šž", "dc"],
    ["šž", "🕵🏻"],
  ]);
});

const fixture_html = path.join(import.meta.dir, "../web/fetch/fixture.html");
const fixture_html_content = fs.readFileSync(fixture_html);
const fixture_html_gz = path.join(import.meta.dir, "../web/fetch/fixture.html.gz");
const fixture_html_gz_content = fs.readFileSync(fixture_html_gz);
function getStream(type, fixture) {
  const data = fixture === "gz" ? fixture_html_gz_content : fixture_html_content;
  const half = parseInt(data.length / 2, 10);

  if (type === "direct") {
    return new ReadableStream({
      type: "direct",
      async pull(controller) {
        controller.write(data.slice(0, half));
        await controller.flush();
        controller.write(data.slice(half));
        await controller.flush();
        controller.close();
      },
    });
  }

  return new ReadableStream({
    async pull(controller) {
      controller.enqueue(data.slice(0, half));
      await Bun.sleep(15);
      controller.enqueue(data.slice(half));
      await Bun.sleep(15);
      controller.close();
    },
  });
}
function createServer(tls) {
  return Bun.serve({
    port: 0,
    tls,
    async fetch(req) {
      const is_compressed = req.url.endsWith("/gzip");

      let payload;
      if (req.url.indexOf("chunked") !== -1) {
        if (req.url.indexOf("direct")) {
          payload = getStream("direct", is_compressed ? "gz" : "default");
        } else {
          payload = getStream("default", is_compressed ? "gz" : "default");
        }
      } else if (req.url.indexOf("file") !== -1) {
        payload = is_compressed ? Bun.file(fixture_html_gz) : Bun.file(fixture_html);
      } else {
        payload = is_compressed ? fixture_html_gz_content : fixture_html_content;
      }

      let headers = {
        "content-type": "text/html",
      };

      if (is_compressed) {
        headers["content-encoding"] = "gzip";
      }

      return new Response(payload, { headers });
    },
  });
}
let http_server;
let https_server;
beforeAll(() => {
  http_server = createServer();
  https_server = createServer({
    ...tls,
  });
});

afterAll(() => {
  http_server?.stop(true);
  https_server?.stop(true);
});

const request_types = ["/", "/gzip", "/chunked/gzip", "/chunked", "/file", "/file/gzip"];
["http", "https"].forEach(protocol => {
  request_types.forEach(path => {
    it(`works with ${protocol} fetch using ${path}`, async () => {
      const server = protocol === "http" ? http_server : https_server;
      const server_origin = server.url.origin;
      const res = await fetch(`${server_origin}${path}`, { tls: { rejectUnauthorized: false } });
      let calls = 0;
      const rw = new HTMLRewriter();
      rw.on("h1", {
        text() {
          calls++;
        },
      });

      const transformed = rw.transform(res);
      if (transformed instanceof Error) throw transformed;
      const body = await transformed.text();
      let trimmed = body?.trim();
      expect(body).toBe(fixture_html_content.toString("utf8"));
      expect(trimmed).toEndWith("</html>");
      expect(trimmed).toStartWith("<!DOCTYPE html>");
      expect(calls).toBeGreaterThan(0);
    });
  });
});

const payloads = [
  {
    name: "direct",
    data: getStream("direct", "none"),
    test: it.todo,
  },
  {
    name: "default",
    data: getStream("default", "none"),
    test: it.todo,
  },
  {
    name: "file",
    data: Bun.file(fixture_html),
    test: it,
  },
  {
    name: "blob",
    data: new Blob([fixture_html_content]),
    test: it,
  },
  {
    name: "buffer",
    data: fixture_html_content,
    test: it,
  },
  {
    name: "string",
    data: fixture_html_content.toString("utf8"),
    test: it,
  },
];

payloads.forEach(type => {
  type.test(`works with payload of type ${type.name}`, async () => {
    let calls = 0;
    const rw = new HTMLRewriter();
    rw.on("h1", {
      text() {
        calls++;
      },
    });
    const transformed = rw.transform(new Response(type.data));
    if (transformed instanceof Error) throw transformed;
    const body = await transformed.text();
    let trimmed = body?.trim();
    expect(body).toBe(fixture_html_content.toString("utf8"));
    expect(trimmed).toEndWith("</html>");
    expect(trimmed).toStartWith("<!DOCTYPE html>");
    expect(calls).toBeGreaterThan(0);
  });
});

// `on()` stores one (selector, handlers) record per call; `transform()` turns
// the records collected so far into a fresh lol-html rewriter each time.
describe("on() registrations", () => {
  it("each selector runs the handlers it was registered with, also after a rejected on()", () => {
    const rw = new HTMLRewriter();
    const count = 24;
    for (let i = 0; i < count; i++) {
      if (i === count / 2) {
        expect(() => rw.on("p[", { element() {} })).toThrow(expect.objectContaining({ name: "HTMLRewriterError" }));
        expect(() => rw.on(`p[data-i="${i}"]`, { element: "not a function" })).toThrow("element must be a function");
      }
      if (i % 2 === 0) {
        rw.on(`p[data-i="${i}"]`, { element: el => el.setInnerContent(`element ${i}`) });
      } else {
        rw.on(`p[data-i="${i}"]`, {
          text(chunk) {
            if (chunk.text) chunk.replace(`text ${i}`);
          },
        });
      }
    }

    const indices = Array.from({ length: count }, (_, i) => count - 1 - i);
    const input = indices.map(i => `<p data-i="${i}">x</p>`).join("");
    const expected = indices.map(i => `<p data-i="${i}">${i % 2 === 0 ? "element" : "text"} ${i}</p>`).join("");
    expect(rw.transform(input)).toBe(expected);
    expect(rw.transform(input)).toBe(expected);
  });

  it("on() from inside a handler applies to the next transform, not the running one", () => {
    const rw = new HTMLRewriter();
    let grown = false;
    rw.on("div", {
      element(el) {
        el.setInnerContent("div");
        if (!grown) {
          grown = true;
          for (let i = 0; i < 64; i++) {
            rw.on("span", { element: span => span.setAttribute("n", String(i)) });
          }
        }
      },
    });
    rw.on("span", { element: el => el.setInnerContent("span") });

    const input = "<div>1</div><span>2</span><div>3</div><span>4</span>";
    expect(rw.transform(input)).toBe("<div>div</div><span>span</span><div>div</div><span>span</span>");
    expect(rw.transform(input)).toBe('<div>div</div><span n="63">span</span><div>div</div><span n="63">span</span>');
  });
});

it("content op string arguments survive a GC triggered while coercing a later argument", async () => {
  const fixture = /* js */ `
    const N = 20000;
    const mk = (ch, n) => {
      const s = ch.repeat(n);
      return s.slice(0, 1) + s.slice(1);
    };
    const churn = () => {
      Bun.gc(true);
      for (let i = 0; i < 50; i++) mk("SECRET", 20000);
      Bun.gc(true);
    };
    const content = ch => ({ toString: () => mk(ch, N) + "END" });
    const opts = {
      get html() {
        churn();
        return false;
      },
    };
    const out = new HTMLRewriter()
      .on("p", {
        element(el) {
          el.setInnerContent(content("I"), opts);
          el.before(content("B"), opts);
          el.setAttribute({ toString: () => mk("n", N) }, { toString: () => (churn(), "v") });
          el.onEndTag(end => {
            end.after(content("E"), opts);
          });
        },
      })
      .onDocument({
        comments(c) {
          c.replace(content("C"), opts);
        },
        text(t) {
          if (t.text === "txt") t.after(content("T"), opts);
        },
        end(end) {
          end.append(content("D"), opts);
        },
      })
      .transform("<p>x</p><div>txt</div><!-- c -->");
    const fragments = {
      "element.before": mk("B", N) + "END<p ",
      "element.setAttribute": "<p " + mk("n", N) + '="v">',
      "element.setInnerContent": '="v">' + mk("I", N) + "END</p>",
      "endTag.after": "</p>" + mk("E", N) + "END<div>",
      "text.after": "<div>txt" + mk("T", N) + "END</div>",
      "comment.replace": "</div>" + mk("C", N) + "END",
      "documentEnd.append": "END" + mk("D", N) + "END",
    };
    const result = {};
    for (const [op, fragment] of Object.entries(fragments)) result[op] = out.includes(fragment);
    result.exact =
      out ===
      mk("B", N) + "END<p " + mk("n", N) + '="v">' + mk("I", N) + "END</p>" + mk("E", N) + "END" +
      "<div>txt" + mk("T", N) + "END</div>" + mk("C", N) + "END" + mk("D", N) + "END";
    console.log(JSON.stringify(result));
  `;
  await using proc = Bun.spawn({
    cmd: [bunExe(), "-e", fixture],
    env: process.platform === "win32" ? bunEnv : { ...bunEnv, Malloc: "1", ASAN_OPTIONS: "detect_leaks=0" },
    stdout: "pipe",
    stderr: "inherit",
  });
  const [stdout, exitCode] = await Promise.all([proc.stdout.text(), proc.exited]);
  expect(JSON.parse(stdout.trim() || "{}")).toEqual({
    "element.before": true,
    "element.setAttribute": true,
    "element.setInnerContent": true,
    "endTag.after": true,
    "text.after": true,
    "comment.replace": true,
    "documentEnd.append": true,
    exact: true,
  });
  expect(exitCode).toBe(0);
});
