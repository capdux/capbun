// Hardcoded module "node:https"
const http = require("node:http");
const { urlToHttpOptions } = require("internal/url");
const { isTlsSymbol, filterEnvForProxies } = require("internal/http");
const { convertALPNProtocols } = require("node:tls");

const ArrayPrototypeShift = Array.prototype.shift;
const ObjectAssign = Object.assign;
const ArrayPrototypeUnshift = Array.prototype.unshift;

function request(...args) {
  let options = {};

  if (typeof args[0] === "string") {
    const urlStr = ArrayPrototypeShift.$call(args);
    options = urlToHttpOptions(new URL(urlStr));
  } else if (args[0] instanceof URL) {
    options = urlToHttpOptions(ArrayPrototypeShift.$call(args));
  }

  if (args[0] && typeof args[0] !== "function") {
    ObjectAssign.$call(null, options, ArrayPrototypeShift.$call(args));
  }

  options._defaultAgent = https.globalAgent;
  ArrayPrototypeUnshift.$call(args, options);

  return new http.ClientRequest(...args);
}

function get(input, options, cb) {
  const req = request(input, options, cb);
  req.end();
  return req;
}

function Agent(options) {
  if (!(this instanceof Agent)) return new Agent(options);

  http.Agent.$apply(this, [options]);
  this.defaultPort = 443;
  this.protocol = "https:";
  this.maxCachedSessions = this.options.maxCachedSessions;
  if (this.maxCachedSessions === undefined) this.maxCachedSessions = 100;
}
$toClass(Agent, "Agent", http.Agent);
Agent.prototype.createConnection = http.createConnection;

function Server(opts, requestListener) {
  if (!(this instanceof Server)) return new Server(opts, requestListener);
  if (typeof opts === "function") {
    requestListener = opts;
    opts = undefined;
  }
  opts = opts != null ? { ...opts } : {};
  if (opts.ALPNCallback) {
    this.ALPNCallback = opts.ALPNCallback;
  } else if (!opts.ALPNProtocols) {
    opts.ALPNProtocols = ["http/1.1"];
  }
  if (opts.ALPNProtocols) {
    convertALPNProtocols(opts.ALPNProtocols, this);
    opts.ALPNProtocols = this.ALPNProtocols;
  }
  this[isTlsSymbol] = true;
  http.Server.$call(this, opts, requestListener);
  return this;
}
$toClass(Server, "Server", http.Server);

function createServer(opts, requestListener) {
  return new Server(opts, requestListener);
}

var https = {
  Agent,
  globalAgent: new Agent({
    keepAlive: true,
    scheduling: "lifo",
    timeout: 5000,
    proxyEnv: process.env.NODE_USE_ENV_PROXY === "1" ? filterEnvForProxies(process.env) : undefined,
  }),
  Server,
  createServer,
  get,
  request,
};
export default https;
