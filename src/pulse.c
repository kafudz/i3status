// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <pulse/pulseaudio.h>
#include "i3status.h"
#include "queue.h"

#define APP_NAME "i3status"
#define APP_ID "org.i3wm"

typedef struct index_info_s {
    char *name;
    uint32_t idx;
    int volume;
    char description[MAX_SINK_DESCRIPTION_LEN];
    TAILQ_ENTRY(index_info_s)
    entries;
} index_info_t;

static pa_threaded_mainloop *main_loop = NULL;
static pa_context *context = NULL;
static pa_mainloop_api *api = NULL;
static bool context_ready = false;
static bool mainloop_thread_running = false;
static uint32_t default_sink_idx = DEFAULT_SINK_INDEX;
TAILQ_HEAD(tailhead, index_info_s)
cached_info =
    TAILQ_HEAD_INITIALIZER(cached_info);
static pthread_mutex_t pulse_mutex = PTHREAD_MUTEX_INITIALIZER;

static void pulseaudio_error_log(pa_context *c) {
    fprintf(stderr,
            "i3status: PulseAudio: %s\n",
            pa_strerror(pa_context_errno(c)));
}

static bool pulseaudio_free_operation(pa_context *c, pa_operation *o) {
    if (o)
        pa_operation_unref(o);
    else
        pulseaudio_error_log(c);
    /* return false if the operation failed */
    return o;
}

/*
 * save the info for the specified sink index
 * returning true if the value was changed
 */
static bool save_info(uint32_t sink_idx, int new_volume, const char *new_description, const char *name) {
    index_info_t *entry;

    /* if this is NULL, gracefully handle and replace with empty-string */
    if (!new_description) {
        new_description = "";
        fprintf(stderr, "i3status: PulseAudio: NULL new_description provided\n");
    }

    pthread_mutex_lock(&pulse_mutex);

    TAILQ_FOREACH(entry, &cached_info, entries) {
        if (name) {
            if (!entry->name || strcmp(entry->name, name)) {
                continue;
            }
        } else {
            if (entry->idx != sink_idx) {
                continue;
            }
        }

        bool changed = false;

        if (new_volume != entry->volume) {
            entry->volume = new_volume;
            changed = true;
        }

        /* compare at most sizeof - 1 bytes: the stored copy is truncated and
         * NUL-terminated, so comparing the full size would report a change on
         * every event for descriptions longer than the buffer */
        if (strncmp(entry->description, new_description, sizeof(entry->description) - 1)) {
            strncpy(entry->description, new_description, sizeof(entry->description) - 1);
            entry->description[sizeof(entry->description) - 1] = '\0';
            changed = true;
        }

        pthread_mutex_unlock(&pulse_mutex);
        return changed;
    }

    /* index not found, store it */
    entry = malloc(sizeof(*entry));
    if (!entry) {
        pthread_mutex_unlock(&pulse_mutex);
        fprintf(stderr, "i3status: PulseAudio: malloc failed in save_info\n");
        return false;
    }

    entry->idx = sink_idx;
    entry->volume = new_volume;
    entry->description[0] = '\0';
    strncpy(entry->description, new_description, sizeof(entry->description) - 1);
    entry->description[sizeof(entry->description) - 1] = '\0';

    if (name) {
        entry->name = strdup(name);
        if (!entry->name) {
            free(entry);
            pthread_mutex_unlock(&pulse_mutex);
            fprintf(stderr, "i3status: PulseAudio: strdup failed in save_info\n");
            return false;
        }
    } else {
        entry->name = NULL;
    }

    /* insert only after all fields are fully initialized */
    TAILQ_INSERT_HEAD(&cached_info, entry, entries);
    pthread_mutex_unlock(&pulse_mutex);
    return true;
}

static void store_info_from_sink_cb(pa_context *c,
                                    const pa_sink_info *info,
                                    int eol,
                                    void *userdata) {
    if (eol < 0) {
        if (pa_context_errno(c) == PA_ERR_NOENTITY)
            return;

        pulseaudio_error_log(c);
        return;
    }

    if (eol > 0)
        return;

    int avg_vol = pa_cvolume_avg(&info->volume);
    int vol_perc = roundf((float)avg_vol * 100 / PA_VOLUME_NORM);
    int composed_volume = COMPOSE_VOLUME_MUTE(vol_perc, info->mute);
    const char *desc = (info->active_port != NULL) ? info->active_port->description : info->description;

    /* if this is the default sink we must try to save it twice: once with
     * DEFAULT_SINK_INDEX as the index, and another with its proper value.
     * The bitwise OR is intentional: it guarantees the second save_info()
     * is always evaluated, even if the first one returned true. */
    if ((info->index == default_sink_idx && /* only written from this thread */
         save_info(DEFAULT_SINK_INDEX, composed_volume, desc, NULL)) |
        save_info(info->index, composed_volume, desc, info->name)) {
        /* if the volume, mute flag or description changed, wake the main thread */
        pthread_kill(main_thread, SIGUSR1);
    }
}

static void get_sink_info(pa_context *c, uint32_t idx, const char *name) {
    pa_operation *o;

    if (name || idx == DEFAULT_SINK_INDEX) {
        o = pa_context_get_sink_info_by_name(
            c, name ? name : "@DEFAULT_SINK@", store_info_from_sink_cb, NULL);
    } else {
        o = pa_context_get_sink_info_by_index(
            c, idx, store_info_from_sink_cb, NULL);
    }
    /* pulseaudio_free_operation() logs on NULL, so always call it */
    pulseaudio_free_operation(c, o);
}

static void store_default_sink_cb(pa_context *c,
                                  const pa_sink_info *i,
                                  int eol,
                                  void *userdata) {
    if (eol < 0) {
        if (pa_context_errno(c) != PA_ERR_NOENTITY)
            pulseaudio_error_log(c);
        return;
    }

    if (eol > 0)
        return;

    if (!i)
        return;

    /* default_sink_idx is read by the main thread under pulse_mutex, so it
     * must be written under it as well */
    pthread_mutex_lock(&pulse_mutex);
    bool changed = (default_sink_idx != i->index);
    if (changed)
        default_sink_idx = i->index;
    pthread_mutex_unlock(&pulse_mutex);

    if (changed) {
        /* default sink changed */
        store_info_from_sink_cb(c, i, eol, userdata);
    }
}

static void update_default_sink(pa_context *c) {
    pa_operation *o = pa_context_get_sink_info_by_name(
        c,
        "@DEFAULT_SINK@",
        store_default_sink_cb,
        NULL);
    pulseaudio_free_operation(c, o);
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                         uint32_t idx, void *userdata) {
    if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE)
        return;
    pa_subscription_event_type_t facility =
        t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SERVER:
            /* server change event, see if the default sink changed */
            update_default_sink(c);
            break;
        case PA_SUBSCRIPTION_EVENT_SINK:
            get_sink_info(c, idx, NULL);
            break;
        default:
            break;
    }
}

static void context_state_callback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        case PA_CONTEXT_TERMINATED:
        default:
            pthread_mutex_lock(&pulse_mutex);
            context_ready = false;
            pthread_mutex_unlock(&pulse_mutex);
            break;

        case PA_CONTEXT_READY: {
            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
            update_default_sink(c);

            pa_operation *o = pa_context_subscribe(
                c,
                PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER,
                NULL,
                NULL);
            if (!pulseaudio_free_operation(c, o))
                break;

            pthread_mutex_lock(&pulse_mutex);
            context_ready = true;
            pthread_mutex_unlock(&pulse_mutex);
        } break;

        case PA_CONTEXT_FAILED:
            /* server disconnected us, attempt to reconnect */
            pthread_mutex_lock(&pulse_mutex);
            context_ready = false;
            pthread_mutex_unlock(&pulse_mutex);
            pa_context_unref(context);
            context = NULL;
            break;
    }
}

/*
 * ask the server for the info of a sink we have not cached yet. `context` is
 * owned by the mainloop thread (it is cleared in context_state_callback), so
 * it must only be read while holding the mainloop lock.
 */
static void prime_sink_info(uint32_t sink_idx, const char *sink_name) {
    pa_threaded_mainloop_lock(main_loop);
    if (context)
        get_sink_info(context, sink_idx, sink_name);
    pa_threaded_mainloop_unlock(main_loop);
}

/*
 * returns the current volume in percent, which, as per PulseAudio,
 * may be > 100%
 */
int volume_pulseaudio(uint32_t sink_idx, const char *sink_name) {
    pthread_mutex_lock(&pulse_mutex);
    if (!context_ready || default_sink_idx == DEFAULT_SINK_INDEX) {
        pthread_mutex_unlock(&pulse_mutex);
        return -1;
    }

    const index_info_t *entry;
    TAILQ_FOREACH(entry, &cached_info, entries) {
        if (sink_name) {
            if (!entry->name || strcmp(entry->name, sink_name)) {
                continue;
            }
        } else {
            if (entry->idx != sink_idx) {
                continue;
            }
        }
        int vol = entry->volume;
        pthread_mutex_unlock(&pulse_mutex);
        return vol;
    }
    pthread_mutex_unlock(&pulse_mutex);

    /* first time requires a prime callback call because we only get updates
     * when the description or volume actually changes, but we need it to be
     * correct even if it never changes */
    prime_sink_info(sink_idx, sink_name);
    /* show 0 while we don't have this information */
    return 0;
}

bool description_pulseaudio(uint32_t sink_idx, const char *sink_name, char buffer[MAX_SINK_DESCRIPTION_LEN]) {
    pthread_mutex_lock(&pulse_mutex);
    if (!context_ready || default_sink_idx == DEFAULT_SINK_INDEX) {
        pthread_mutex_unlock(&pulse_mutex);
        return false;
    }

    const index_info_t *entry;
    TAILQ_FOREACH(entry, &cached_info, entries) {
        if (sink_name) {
            if (!entry->name || strcmp(entry->name, sink_name)) {
                continue;
            }
        } else {
            if (entry->idx != sink_idx) {
                continue;
            }
        }
        strncpy(buffer, entry->description, MAX_SINK_DESCRIPTION_LEN - 1);
        buffer[MAX_SINK_DESCRIPTION_LEN - 1] = '\0';
        pthread_mutex_unlock(&pulse_mutex);
        return true;
    }
    pthread_mutex_unlock(&pulse_mutex);

    /* first time requires a prime callback call because we only get updates
     * when the description or volume actually changes, but we need it to be
     * correct even if it never changes */
    prime_sink_info(sink_idx, sink_name);
    /* show empty string while we don't have this information */
    buffer[0] = '\0';
    return true;
}

/*
 * create and connect a new context, starting the mainloop thread if needed.
 * the caller holds the mainloop lock if the thread is already running.
 */
static bool pulse_create_context(void) {
    pa_proplist *proplist = pa_proplist_new();
    if (!proplist)
        return false;
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, APP_NAME);
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, APP_ID);
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, I3STATUS_VERSION);
    context = pa_context_new_with_proplist(api, APP_NAME, proplist);
    pa_proplist_free(proplist);
    if (!context)
        return false;
    pa_context_set_state_callback(context,
                                  context_state_callback,
                                  NULL);
    if (pa_context_connect(context,
                           NULL,
                           PA_CONTEXT_NOFAIL | PA_CONTEXT_NOAUTOSPAWN,
                           NULL) < 0) {
        pulseaudio_error_log(context);
        pa_context_unref(context);
        context = NULL;
        return false;
    }
    if (!mainloop_thread_running) {
        if (pa_threaded_mainloop_start(main_loop) < 0) {
            pulseaudio_error_log(context);
            pa_context_disconnect(context);
            pa_context_unref(context);
            context = NULL;
            pa_threaded_mainloop_free(main_loop);
            main_loop = NULL;
            api = NULL;
            return false;
        }
        mainloop_thread_running = true;
    }
    return true;
}

/*
 *  detect and, if necessary, initialize the PulseAudio API
 */
bool pulse_initialize(void) {
    if (!main_loop) {
        main_loop = pa_threaded_mainloop_new();
        if (!main_loop)
            return false;
    }
    if (!api) {
        api = pa_threaded_mainloop_get_api(main_loop);
        if (!api)
            return false;
    }
    if (!context) {
        /* after a disconnect the mainloop thread is already running, so
         * creating the new context must happen under the mainloop lock */
        bool locked = mainloop_thread_running;
        if (locked)
            pa_threaded_mainloop_lock(main_loop);
        bool ok = pulse_create_context();
        if (locked)
            pa_threaded_mainloop_unlock(main_loop);
        return ok;
    }
    return true;
}
