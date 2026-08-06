/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/

// Proof that opening a dataset and driving it to a conclusion never blocks -- that dew_access works
// in a module built WITHOUT ASYNCIFY.
//
// Why that property gets its own target: dew_access is meant to be embeddable in a module that has no
// ASYNCIFY -- a decode worker, a service worker, anything driven from inside a JS callback that
// cannot tolerate a stack unwind. ASYNCIFY is a whole-program transform (~40-50% code size, viral,
// un-re-enterable from a callback), so "does not require it" has to be held rather than assumed.
//
// HOW THIS CHECKS, and why it is not a link test. The obvious idea -- "a blocking read calls
// emscripten_sleep, which without -sASYNCIFY is a link error" -- is FALSE, and believing it produced
// an earlier version of this file that passed while testing nothing. Emscripten links an aborting
// *stub*:
//
//     var _emscripten_sleep = () => abort("Please compile your program with async support ...")
//
// so the reference resolves quietly and the module still imports emscripten_sleep. Worse, that import
// is present no matter what we do: storage_backend_t's synchronous exists()/read_index() are VIRTUAL,
// and object_backend_t's overrides route through run_on_loop_blocking. A retained vtable keeps them
// reachable even though the access path only ever calls the _async forms. So the presence of the
// symbol proves nothing in either direction.
//
// What DOES discriminate is running it. The stub aborts, loudly and identifiably, the instant
// anything blocks. So the probe opens a dataset for real and drives it to a terminal state from an
// ordinary JS poll loop (access_noasyncify_smoke.mjs). If the open path ever goes back to waiting on
// a read instead of co_awaiting it, the module aborts instead of reaching the error state, and the
// smoke test fails.
//
// The dataset URL is unreachable on purpose. The point is not that a fetch succeeds -- it is that
// dew_dataset_create RETURNS while the read is still outstanding, that the completion arrives through
// the pump's wake, and that dew_pump_poll dispatches it on the caller's thread. A failing read
// exercises exactly that sequence, hermetically, with no network and no fixture data. The harness
// stubs XMLHttpRequest to fail asynchronously, which is what makes "returned before the read
// resolved" observable rather than merely likely.

#include <emscripten/emscripten.h>

#include <dew/access/query.h>
#include <dew/core/error.h>
#include <dew/core/pump.h>

#include <string.h>

namespace
{
dew_pump_t *g_pump = nullptr;
dew_dataset_t *g_dataset = nullptr;
int g_wake_count = 0;

// The wake contract's whole point: this runs on whatever thread the completion landed on, and under
// wasm possibly re-entrantly, so it may only signal. Bumping a counter is the smallest legal thing.
void on_wake(void *user_ptr)
{
  (void)user_ptr;
  g_wake_count++;
}
} // namespace

extern "C"
{

  // 1 if the dataset handle came back. Reaching the return at all is already most of the result: a
  // blocking open would have aborted inside this call.
  EMSCRIPTEN_KEEPALIVE int dew_probe_open(void)
  {
    g_pump = dew_pump_create();
    if (!g_pump)
      return 0;
    dew_pump_set_wake_callback(g_pump, on_wake, nullptr);

    // s3:// is the only scheme under Emscripten that reaches emscripten_fetch (mem:// is in-process,
    // and dir:// / az:// are compiled out), so it is the one that exercises a real deferred read. The
    // endpoint is a dead port with throwaway credentials: the harness stubs XHR anyway, so nothing
    // leaves the process, and a bad host keeps it that way even if the stub is ever removed.
    const char *url = "s3://dew-probe-bucket/dataset";
    const char *connection = "endpoint=http://127.0.0.1:1;region=us-east-1;access_key_id=probe;secret_access_key=probe;path_style=true";
    dew_error_t *error = nullptr;
    g_dataset = dew_dataset_create(url, (uint32_t)strlen(url), connection, (uint32_t)strlen(connection), nullptr, g_pump, &error);
    if (!g_dataset)
    {
      if (error)
        dew_error_destroy(error);
      return 0;
    }
    return 1;
  }

  // dew_dataset_opening(0) / dew_dataset_ready(1) / dew_dataset_error(2). Must be `opening` right
  // after dew_probe_open -- that is the assertion that create did not quietly run the open to
  // completion before returning.
  EMSCRIPTEN_KEEPALIVE int dew_probe_state(void)
  {
    return g_dataset ? (int)dew_dataset_state(g_dataset) : -1;
  }

  EMSCRIPTEN_KEEPALIVE int dew_probe_poll(void)
  {
    return g_pump ? (int)dew_pump_poll(g_pump) : 0;
  }

  // The dataset's error text, so a failing smoke run can say WHY rather than only that it failed.
  // Returns a pointer into a static buffer, valid until the next call.
  EMSCRIPTEN_KEEPALIVE const char *dew_probe_error(void)
  {
    static char buffer[512];
    buffer[0] = '\0';
    if (!g_dataset)
      return buffer;
    dew_error_t *error = nullptr;
    dew_dataset_get_error(g_dataset, &error);
    if (!error)
      return buffer;
    int code = 0;
    const char *text = nullptr;
    size_t length = 0;
    dew_error_get_info(error, &code, &text, &length);
    if (text)
    {
      if (length >= sizeof(buffer))
        length = sizeof(buffer) - 1;
      memcpy(buffer, text, length);
      buffer[length] = '\0';
    }
    dew_error_destroy(error);
    return buffer;
  }

  EMSCRIPTEN_KEEPALIVE int dew_probe_wake_count(void)
  {
    return g_wake_count;
  }

  // 0 under wasm without ASYNCIFY -- the branch a portable caller takes, and a cheap check that the
  // build knows what it is.
  EMSCRIPTEN_KEEPALIVE int dew_probe_can_block(void)
  {
    return (int)dew_access_can_block();
  }

  EMSCRIPTEN_KEEPALIVE void dew_probe_close(void)
  {
    if (g_dataset)
      dew_dataset_close(g_dataset);
    g_dataset = nullptr;
    if (g_pump)
      dew_pump_destroy(g_pump);
    g_pump = nullptr;
  }

  int main()
  {
    // Deliberately empty: the harness drives the entry points above, because the poll loop has to
    // return to the JS event loop between passes. That IS the thing being proven -- it is what a host
    // without ASYNCIFY must do, and that dew_access can be driven that way is the point of the target.
    return 0;
  }
}
