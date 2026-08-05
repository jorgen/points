// Smoke test for dew_access_probe: drives a dataset open to a terminal state from a plain JS poll
// loop, in a module built WITHOUT ASYNCIFY.
//
//   node src/wasm/access_noasyncify_smoke.mjs <path-to-dew_access_probe.mjs>
//
// The assertion that carries the target is that this reaches the end at all. Emscripten resolves
// emscripten_sleep to an aborting stub rather than failing the link, so a blocking read is invisible
// at build time and loud at run time -- it kills the module with "Please compile your program with
// async support". Every early-exit path below therefore reports the abort text when it has one.
//
// Hermetic by construction: XMLHttpRequest is stubbed to fail on a later turn of the event loop, so
// no network is touched and "dew_dataset_create returned before the read resolved" is *observable*
// rather than merely probable -- the read cannot possibly have completed inside create.

import { createRequire } from "node:module";

const modulePath = process.argv[2];
if (!modulePath) {
  console.error("usage: node access_noasyncify_smoke.mjs <path-to-dew_access_probe.mjs>");
  process.exit(2);
}

// ---------------------------------------------------------------------------------------------
// A minimal XMLHttpRequest that always fails, asynchronously. Emscripten's FETCH backend is written
// against XHR and node has none; supplying our own also removes the network from the test.
let xhrOpened = 0;
let xhrFailed = 0;
globalThis.XMLHttpRequest = class {
  constructor() {
    this.readyState = 0;
    this.status = 0;
    this.statusText = "";
    this.response = null;
    this.responseType = "";
    this.withCredentials = false;
  }
  open(method, url, async) {
    xhrOpened++;
    this._async = async;
    this.readyState = 1;
  }
  setRequestHeader() {}
  getAllResponseHeaders() {
    return "";
  }
  send() {
    // The load-bearing detail: fail on a LATER turn. If the engine were blocking inside
    // dew_dataset_create waiting for this, it would spin emscripten_sleep and abort before this
    // callback could ever run.
    setTimeout(() => {
      this.readyState = 4;
      this.status = 0;
      this.statusText = "probe: unreachable by design";
      xhrFailed++;
      if (this.onerror) this.onerror({ type: "error" });
    }, 0);
  }
  abort() {}
};

const fail = (msg) => {
  console.error(`FAIL: ${msg}`);
  process.exit(1);
};
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));

// ---------------------------------------------------------------------------------------------
const require = createRequire(import.meta.url);
const { default: factory } = await import(require.resolve(modulePath));

let aborted = null;
const mod = await factory({
  // abort() is how the emscripten_sleep stub announces itself. Capture the text instead of letting
  // it take the process down, so the failure names its own cause.
  onAbort: (what) => {
    aborted = String(what);
  },
  print: () => {},
  printErr: (line) => {
    if (/async support|asyncify/i.test(line)) aborted = line;
  },
});

const abortNote = () => (aborted ? ` (module aborted: ${aborted})` : "");
const errorText = () => {
  try {
    return mod.UTF8ToString(mod._dew_probe_error());
  } catch {
    return "<unavailable>";
  }
};

// abort() calls onAbort and THEN throws, so every call into the module has to be guarded or the
// asyncify abort would surface as an unhandled exception with no explanation attached.
const call = (name, ...args) => {
  try {
    return mod[`_${name}`](...args);
  } catch (e) {
    if (!aborted) aborted = String(e);
    fail(`${name}() trapped: ${aborted}`);
  }
};

if (call("dew_probe_can_block") !== 0)
  fail(`dew_access_can_block() should be 0 in a non-ASYNCIFY build, got ${call("dew_probe_can_block")}`);

if (call("dew_probe_open") !== 1) fail(`dew_dataset_create did not return a handle${abortNote()}`);
if (aborted) fail(`module aborted during open: ${aborted}`);

// THE assertion for deferred open: create returned while the read was still outstanding. The XHR
// stub cannot have completed yet -- it fails on a later turn -- so anything other than `opening`
// means create ran the open to completion inline.
const initial = call("dew_probe_state");
if (initial !== 0)
  fail(
    `expected state 'opening'(0) immediately after create, got ${initial} ` +
      `[fetches issued=${xhrOpened} failed=${xhrFailed}] err="${errorText()}" -- state 2 with 0 ` +
      `fetches means the url was rejected before any IO; state 2 with a fetch means create ran the ` +
      `open inline`
  );

// Drive it exactly as a host without ASYNCIFY must: poll, then yield to the event loop.
let state = initial;
let polls = 0;
for (; polls < 500 && state === 0; polls++) {
  call("dew_probe_poll");
  if (aborted) fail(`module aborted while polling (poll ${polls}): ${aborted}`);
  await tick();
  state = call("dew_probe_state");
}

if (state === 0) fail(`dataset never left 'opening' after ${polls} polls${abortNote()}`);
// The fetch was stubbed to fail, so `error`(2) is the correct terminal state. `ready` would mean the
// stub never ran and the test proved nothing.
if (state !== 2) fail(`expected terminal state 'error'(2) from an unreachable url, got ${state}`);
if (xhrOpened === 0) fail("the open path never issued a fetch -- the test did not exercise the read");
if (xhrFailed === 0) fail("the stubbed fetch never completed -- the test did not exercise a completion");

// The wake is what tells a real host to poll. Silence here means a host would sit idle forever.
if (call("dew_probe_wake_count") === 0) fail("no wake was delivered for the open completion");

call("dew_probe_close");
if (aborted) fail(`module aborted during close: ${aborted}`);

console.log(
  `PASS: opened async, ${polls} poll(s), ${call("dew_probe_wake_count")} wake(s), ` +
    `${xhrOpened} fetch(es) issued and failed, terminal state=error, no ASYNCIFY`
);
