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
#ifndef DEW_CORE_PUMP_H
#define DEW_CORE_PUMP_H

/* The bridge between dewfall's internal threads and the host's own loop.
 *
 * Work inside dewfall finishes on internal threads, but user callbacks must never run there -- they
 * would race the host's state and would have no defined thread under wasm. So dewfall never calls
 * user code spontaneously. Instead a subsystem SIGNALS that it has something to deliver, and the
 * host calls dew_pump_poll, which dispatches everything on the host's own thread.
 *
 * ONE PUMP DRAINS EVERYTHING REGISTERED WITH IT. That is not a convenience, it is what makes the
 * "at most one wake" guarantee sound. If several subsystems shared a wake signal but each had its
 * own poll, this would happen: subsystem A fires the wake, the host polls A only, and B -- whose own
 * fire was suppressed because the signal was already raised -- is never drained. The wake and the
 * drain have to have the same scope.
 *
 * Typical use:
 *
 *     dew_pump_t *pump = dew_pump_create();
 *     dew_pump_set_wake_callback(pump, on_wake, host);   // on_wake just schedules a tick
 *     dew_dataset_t *ds = dew_dataset_create(url, len, NULL, 0, NULL, pump, &error);
 *     ...
 *     // on the host's thread, whenever on_wake asked for it:
 *     dew_pump_poll(pump);
 */

#include <stdint.h>

#include <dew/core/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_pump_t;

/* Signals that dew_pump_poll has work to dispatch.
 *
 * Fires from an ARBITRARY internal thread, and under wasm possibly re-entrantly from inside another
 * dew_* call, because there the event loop is cooperative rather than a separate thread. It may
 * therefore only SIGNAL: post to your loop, write to a pipe, set an atomic, call
 * requestAnimationFrame. It must not call back into dewfall -- dew_pump_poll included -- and it must
 * not destroy the pump.
 *
 * Armed once: after firing, it stays silent until dew_pump_poll runs, so a burst of completions
 * produces exactly one wake. */
//= py.skip
typedef void (*dew_wake_callback_t)(void *user_ptr);

//= py.skip
DEW_CORE_EXPORT struct dew_pump_t *dew_pump_create(void);
//= py.skip
DEW_CORE_EXPORT void dew_pump_destroy(struct dew_pump_t *pump);

/* Replaces any previous callback and rearms. Pass NULL to stop being woken; polling still works. */
//= py.skip
DEW_CORE_EXPORT void dew_pump_set_wake_callback(struct dew_pump_t *pump, dew_wake_callback_t callback, void *user_ptr);

/* Rearm the wake, then dispatch everything pending. Returns the number of events dispatched.
 *
 * Host thread only, and it is where every subsystem callback runs.
 *
 * The rearm happens BEFORE the dispatch, deliberately. Rearming afterwards would lose wakeups: a
 * completion landing mid-dispatch would call the wake, find it still raised from the wake that
 * caused this poll, be suppressed -- and then the rearm would clear the signal, leaving queued work
 * with nothing to announce it. Rearming first costs the occasional redundant wake instead, which is
 * the right way round. */
//= py.skip
DEW_CORE_EXPORT uint32_t dew_pump_poll(struct dew_pump_t *pump);

/* Events waiting to be dispatched. A host that drives the pump from a timer can use this to decide
 * whether another tick is worth scheduling. */
//= py.skip
DEW_CORE_EXPORT uint32_t dew_pump_pending_count(struct dew_pump_t *pump);

#ifdef __cplusplus
}
#endif
#endif /* DEW_CORE_PUMP_H */
