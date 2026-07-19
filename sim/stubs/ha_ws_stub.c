/* Interactive stand-in for the HA WebSocket client (main/ha/ha_ws.c) in the
 * simulator.
 *
 * The real client is a model file the sim never builds. This stub impersonates
 * a healthy, subscribed client: it owns fake state for every stateful
 * dashboard slot, and ha_ws_call() echoes each service call back as the
 * VIEW_EVENT_HA_ENTITY / VIEW_EVENT_HA_MEDIA the real subscription would
 * deliver — so chips, switches, the slider and the media card all round-trip
 * when you click them.
 *
 * Set SIM_MQTT_MODE=1 to impersonate a WS-disabled device instead (the panel
 * degrades to the MQTT read-only path via ha_dash.c's legacy bridge). */

#include "ha_ws_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "ha_dash.h"
#include "ha_ws.h"
#include "view_data.h"

/* ── Fake per-slot state ─────────────────────────────────────────────────── */

static bool s_on[DASH_SLOT_COUNT];
static int  s_brightness[DASH_SLOT_COUNT]; /* 0..255, lights only */

static struct {
    bool     playing;
    int      track;
    uint32_t track_started_ms;
} s_media;

static const struct {
    const char *title;
    const char *artist;
} s_tracks[] = {
    {"Weightless", "Marconi Union"},          /* Chill */
    {"My Girl", "The Temptations"},           /* Oldies */
    {"C.R.E.A.M.", "Wu-Tang Clan"},           /* 90s Hiphop */
};
#define TRACK_COUNT ((int)(sizeof(s_tracks) / sizeof(s_tracks[0])))
#define TRACK_ROTATE_MS 15000

/* ── Public ha_ws.h surface ──────────────────────────────────────────────── */

bool ha_ws_is_enabled(void)
{
    return getenv("SIM_MQTT_MODE") == NULL;
}

void ha_ws_status_get(ha_ws_status_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    if (ha_ws_is_enabled()) {
        out->status = HA_WS_STATUS_SUBSCRIBED;
        snprintf(out->url, sizeof(out->url), "ws://sim.local:8123/api/websocket");
    } else {
        out->status = HA_WS_STATUS_DISABLED;
    }
}

/* ── Echo helpers ────────────────────────────────────────────────────────── */

static void post_entity(int slot, const char *state, int brightness)
{
    struct view_data_ha_entity data = {.slot = (uint8_t)slot,
                                       .brightness = (int16_t)brightness};
    strncpy(data.state, state, sizeof(data.state) - 1);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ENTITY,
                      &data, sizeof(data), portMAX_DELAY);
}

static void post_toggle(int slot)
{
    bool light = dash_slots[slot].kind == DASH_KIND_LIGHT;
    post_entity(slot, s_on[slot] ? "on" : "off",
                (light && s_on[slot]) ? s_brightness[slot] : -1);
}

static void post_media(int slot)
{
    struct view_data_ha_media data = {.slot = (uint8_t)slot};
    snprintf(data.state, sizeof(data.state), "%s", s_media.playing ? "playing" : "paused");
    snprintf(data.title, sizeof(data.title), "%s", s_tracks[s_media.track].title);
    snprintf(data.artist, sizeof(data.artist), "%s", s_tracks[s_media.track].artist);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_MEDIA,
                      &data, sizeof(data), portMAX_DELAY);
}

static int media_slot(void)
{
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        if (dash_slots[i].kind == DASH_KIND_MEDIA) {
            return i;
        }
    }
    return -1;
}

static void start_track(int track)
{
    s_media.track = track;
    s_media.playing = true;
    s_media.track_started_ms = 0; /* re-stamped on the next tick */
    int slot = media_slot();
    if (slot >= 0) {
        post_media(slot);
    }
}

/* ── Service-call echo ───────────────────────────────────────────────────── */

esp_err_t ha_ws_call(const char *domain, const char *service,
                     const char *entity_id, const char *extra)
{
    printf("[sim] ha_ws_call %s.%s -> %s%s%s\n", domain, service, entity_id,
           extra ? " " : "", extra ? extra : "");

    int slot = dash_slot_by_entity(entity_id);

    if (strcmp(domain, "homeassistant") == 0 && slot >= 0) {
        s_on[slot] = strcmp(service, "turn_on") == 0;
        post_toggle(slot);
    } else if (strcmp(domain, "light") == 0 && slot >= 0) {
        int pct = -1;
        if (extra != NULL) {
            sscanf(extra, "{\"brightness_pct\":%d}", &pct);
        }
        s_on[slot] = true;
        if (pct >= 0) {
            s_brightness[slot] = (pct * 255 + 50) / 100;
        }
        post_toggle(slot);
    } else if (strcmp(domain, "media_player") == 0) {
        s_media.playing = !s_media.playing;
        int ms = media_slot();
        if (ms >= 0) {
            post_media(ms);
        }
    } else if (strcmp(domain, "script") == 0 && slot >= 0) {
        if (slot == SLOT_PRESET_CHILL) {
            start_track(0);
        } else if (slot == SLOT_PRESET_OLDIES) {
            start_track(1);
        } else if (slot == SLOT_PRESET_HIPHOP) {
            start_track(2);
        } else if (slot == SLOT_ALL_OFF) {
            /* The all-off script: every toggle/light drops out, echoed back
             * one entity at a time like a real HA state cascade. */
            for (int i = 0; i < DASH_SLOT_COUNT; i++) {
                if (dash_slots[i].kind == DASH_KIND_TOGGLE ||
                    dash_slots[i].kind == DASH_KIND_LIGHT) {
                    s_on[i] = false;
                    post_toggle(i);
                }
            }
        }
    }
    return ESP_OK;
}

/* ── Mock-driven hooks ───────────────────────────────────────────────────── */

void ha_ws_stub_seed(void)
{
    if (!ha_ws_is_enabled()) {
        return;
    }
    /* A lived-in initial scene: hall Xmas lights on (matches the reference
     * dashboard), LED strip on at 60%, everything else off, music paused. */
    for (int i = 0; i < DASH_SLOT_COUNT; i++) {
        switch ((dash_kind_t)dash_slots[i].kind) {
            case DASH_KIND_TOGGLE:
                s_on[i] = (i == SLOT_XMAS_HALL);
                post_toggle(i);
                break;
            case DASH_KIND_LIGHT:
                s_on[i] = true;
                s_brightness[i] = (60 * 255 + 50) / 100;
                post_toggle(i);
                break;
            default:
                break;
        }
    }
    s_media.playing = false;
    s_media.track = 0;
    int slot = media_slot();
    if (slot >= 0) {
        post_media(slot);
    }
}

void ha_ws_stub_tick(uint32_t now_ms)
{
    if (!ha_ws_is_enabled() || !s_media.playing) {
        return;
    }
    if (s_media.track_started_ms == 0) {
        s_media.track_started_ms = now_ms;
        return;
    }
    if (now_ms - s_media.track_started_ms >= TRACK_ROTATE_MS) {
        s_media.track_started_ms = now_ms;
        start_track((s_media.track + 1) % TRACK_COUNT);
    }
}
