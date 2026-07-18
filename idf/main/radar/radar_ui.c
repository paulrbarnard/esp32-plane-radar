#include "radar_ui.h"

#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include "aircraft_type_db.h"
#include "radar_storage.h"
#include "radar_ginfo.h"
#include "radar_photo.h"
#include "radar_range.h"

static lv_obj_t *range_label;
static lv_obj_t *aircraft_layer;
static lv_obj_t *detail_card;
static lv_obj_t *ginfo_label;
static lv_obj_t *registry_owner_label;
static lv_obj_t *registry_operator_label;
static lv_obj_t *registry_year_label;
static lv_obj_t *registry_source_label;
static lv_obj_t *photo_label;
static lv_obj_t *photo_image;
static char photo_registration[20];
/* LVGL retains a pointer to the image descriptor passed to lv_img_set_src(),
 * so it must outlive radar_ui_tick() and the asynchronous renderer. */
static radar_photo_t displayed_photo;
static lv_point_t aircraft_vectors[64][2];
static lv_point_t aircraft_icons[64][4][2];
static radar_aircraft_t aircraft_hits[64];
static radar_aircraft_t selected_aircraft;
enum {
    RADAR_SIZE = 480,
    RADAR_CENTER = RADAR_SIZE / 2,
    RADAR_EDGE = 16,
};

static const lv_point_t vertical_line[] = {{RADAR_CENTER, RADAR_EDGE}, {RADAR_CENTER, RADAR_SIZE - RADAR_EDGE}};
static const lv_point_t horizontal_line[] = {{RADAR_EDGE, RADAR_CENTER}, {RADAR_SIZE - RADAR_EDGE, RADAR_CENTER}};

static lv_color_t aircraft_color(radar_aircraft_kind_t kind)
{
    switch (kind) {
    case RADAR_AIRCRAFT_JET: return lv_color_hex(0x42C5F5);
    case RADAR_AIRCRAFT_PROP: return lv_color_hex(0xFFC857);
    case RADAR_AIRCRAFT_GLIDER: return lv_color_hex(0xC490FF);
    case RADAR_AIRCRAFT_HELICOPTER: return lv_color_hex(0x58E06C);
    default: return lv_color_hex(0xFFE34D);
    }
}

static const char *aircraft_kind_name(radar_aircraft_kind_t kind)
{
    switch (kind) {
    case RADAR_AIRCRAFT_JET: return "Jet";
    case RADAR_AIRCRAFT_PROP: return "Prop";
    case RADAR_AIRCRAFT_GLIDER: return "Glider";
    case RADAR_AIRCRAFT_HELICOPTER: return "Helicopter";
    default: return "Unknown";
    }
}

static lv_point_t rotate_icon_point(int x, int y, float heading, int forward, int right)
{
    return (lv_point_t){
        x + (int)lroundf(sinf(heading) * forward + cosf(heading) * right),
        y + (int)lroundf(-cosf(heading) * forward + sinf(heading) * right),
    };
}

static void add_icon_line(lv_obj_t *parent, lv_point_t points[2], lv_color_t color)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

static void draw_aircraft_icon(lv_obj_t *parent, size_t index, int x, int y, float heading,
                               radar_aircraft_kind_t kind, lv_color_t color)
{
    int lines = 0;
#define ICON_LINE(F1, R1, F2, R2) do { \
    aircraft_icons[index][lines][0] = rotate_icon_point(x, y, heading, F1, R1); \
    aircraft_icons[index][lines][1] = rotate_icon_point(x, y, heading, F2, R2); \
    add_icon_line(parent, aircraft_icons[index][lines], color); ++lines; \
} while (0)
    switch (kind) {
    case RADAR_AIRCRAFT_JET:
        ICON_LINE(9, 0, -8, 0); ICON_LINE(0, -7, 0, 7); ICON_LINE(-5, -3, -5, 3); break;
    case RADAR_AIRCRAFT_PROP:
        ICON_LINE(8, 0, -7, 0); ICON_LINE(0, -6, 0, 6); ICON_LINE(8, -3, 8, 3); break;
    case RADAR_AIRCRAFT_GLIDER:
        ICON_LINE(7, 0, -7, 0); ICON_LINE(0, -11, 0, 11); ICON_LINE(-5, -2, -5, 2); break;
    case RADAR_AIRCRAFT_HELICOPTER:
        ICON_LINE(5, 0, -5, 0); ICON_LINE(1, -10, 1, 10); ICON_LINE(-4, 0, -10, 0); break;
    default:
        ICON_LINE(6, 0, -6, 0); ICON_LINE(0, -5, 0, 5); break;
    }
#undef ICON_LINE
}

static void update_range_label(void)
{
    char text[12];
    radar_range_format_label(text, sizeof(text));
    lv_label_set_text(range_label, text);
}

void radar_ui_update_range(void)
{
    if (range_label && lv_obj_is_valid(range_label)) update_range_label();
}

static void close_detail(lv_event_t *event)
{
    (void)event;
    if (detail_card) {
        /* This callback can originate from a child of detail_card. Defer the
         * deletion until LVGL has completed dispatching the current press. */
        lv_obj_del_async(detail_card);
        detail_card = NULL;
        ginfo_label = NULL;
        registry_owner_label = NULL;
        registry_operator_label = NULL;
        registry_year_label = NULL;
        registry_source_label = NULL;
        photo_label = NULL;
        photo_image = NULL;
        photo_registration[0] = '\0';
        memset(&displayed_photo, 0, sizeof(displayed_photo));
        radar_photo_clear();
    }
}

static void start_live_photo(const char *registration)
{
    if (!detail_card || !registration || !registration[0]) return;
    if (!photo_label) {
        photo_label = lv_label_create(detail_card);
        lv_obj_set_style_text_color(photo_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(photo_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(photo_label, 145);
        lv_label_set_long_mode(photo_label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(photo_label, 20, 208);
    }
    char mark[20];
    snprintf(mark, sizeof(mark), "%s", registration);
    /* CAA's API returns a UK mark without its G- prefix (for example EZRX).
     * It is a UK-only source, so restore the prefix for Commons search. */
    if (!strchr(mark, '-') && strlen(mark) == 4) {
        for (size_t i = 0; i < 4; ++i) {
            if (mark[i] < 'A' || mark[i] > 'Z') break;
            if (i == 3) snprintf(mark, sizeof(mark), "G-%s", registration);
        }
    }
    lv_label_set_text(photo_label, "Photo: searching\nWikimedia Commons...");
    radar_photo_start(mark);
}

static void add_detail_line(lv_obj_t *parent, const char *text, int y, const lv_font_t *font)
{
    lv_obj_t *line = lv_label_create(parent);
    lv_label_set_text(line, text);
    lv_obj_set_style_text_color(line, lv_color_white(), 0);
    lv_obj_set_style_text_font(line, font, 0);
    lv_obj_set_width(line, 310);
    lv_label_set_long_mode(line, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(line, 20, y);
}

static void show_registry_detail(lv_event_t *event)
{
    const radar_aircraft_t *plane = lv_event_get_user_data(event);
    if (!plane) return;
    if (!radar_storage_present()) radar_storage_init();
    close_detail(NULL);

    detail_card = lv_obj_create(lv_scr_act());
    /* Keep all important content inside the round display's central safe area. */
    lv_obj_set_size(detail_card, 350, 310);
    lv_obj_set_pos(detail_card, 65, 95);
    lv_obj_set_style_radius(detail_card, 14, 0);
    lv_obj_set_style_bg_color(detail_card, lv_color_hex(0x081822), 0);
    lv_obj_set_style_bg_opa(detail_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(detail_card, lv_color_hex(0x106420), 0);
    lv_obj_set_style_border_width(detail_card, 2, 0);
    lv_obj_set_style_pad_all(detail_card, 0, 0);
    lv_obj_clear_flag(detail_card, LV_OBJ_FLAG_SCROLLABLE);

    char line[96];
    snprintf(line, sizeof(line), "%s", plane->callsign[0] ? plane->callsign : "Aircraft details");
    add_detail_line(detail_card, line, 12, &lv_font_montserrat_14);
    radar_aircraft_details_t details;
    const bool has_sd_details = radar_storage_load_details(plane->hex, &details);
    if (has_sd_details) {
        snprintf(line, sizeof(line), "Reg: %s", details.registration[0] ? details.registration : "unknown");
        add_detail_line(detail_card, line, 34, &lv_font_montserrat_14);
        add_detail_line(detail_card, "Operator", 58, &lv_font_montserrat_14);
        snprintf(line, sizeof(line), "%s", details.operator_name[0] ? details.operator_name : "unknown");
        registry_operator_label = lv_label_create(detail_card);
        lv_label_set_text(registry_operator_label, line);
        lv_obj_set_style_text_color(registry_operator_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(registry_operator_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(registry_operator_label, 310);
        lv_label_set_long_mode(registry_operator_label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(registry_operator_label, 20, 74);
        add_detail_line(detail_card, "Owner", 98, &lv_font_montserrat_14);
        snprintf(line, sizeof(line), "%s", details.owner[0] ? details.owner : "unknown");
        registry_owner_label = lv_label_create(detail_card);
        lv_label_set_text(registry_owner_label, line);
        lv_obj_set_style_text_color(registry_owner_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(registry_owner_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(registry_owner_label, 310);
        lv_label_set_long_mode(registry_owner_label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(registry_owner_label, 20, 114);
        snprintf(line, sizeof(line), "Built: %s", details.year[0] ? details.year : "unknown");
        registry_year_label = lv_label_create(detail_card);
        lv_label_set_text(registry_year_label, line);
        lv_obj_set_style_text_color(registry_year_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(registry_year_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(registry_year_label, 20, 138);
        snprintf(line, sizeof(line), "Data: %s", details.source[0] ? details.source : "SD / OpenSky");
        registry_source_label = lv_label_create(detail_card);
        lv_label_set_text(registry_source_label, line);
        lv_obj_set_style_text_color(registry_source_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(registry_source_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(registry_source_label, 145);
        lv_label_set_long_mode(registry_source_label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(registry_source_label, 190, 242);
        /* The CAA request is deliberately completed before the Commons TLS
         * request begins. Concurrent TLS handshakes are unreliable on this
         * small target and bring no benefit to this one-shot panel. */
        snprintf(photo_registration, sizeof(photo_registration), "%s", details.registration);
    } else {
        if (!radar_storage_present()) {
            add_detail_line(detail_card, "No SD card mounted — insert one or create a RADARDB card.",
                            94, &lv_font_montserrat_14);
        }
    }
    if (plane->hex[0]) {
        if (!has_sd_details) {
        ginfo_label = lv_label_create(detail_card);
        lv_label_set_text(ginfo_label, "CAA G-INFO: looking up...");
        lv_obj_set_style_text_color(ginfo_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(ginfo_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(ginfo_label, 20, 90);
        }
        radar_ginfo_start(plane->hex);
    }
    lv_obj_t *close = lv_btn_create(detail_card);
    lv_obj_set_size(close, 80, 56);
    lv_obj_set_pos(close, 260, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_add_event_cb(close, close_detail, LV_EVENT_PRESSED, NULL);
    lv_obj_t *x = lv_label_create(close);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_center(x);
    lv_obj_add_event_cb(detail_card, close_detail, LV_EVENT_PRESSED, NULL);
    lv_obj_move_foreground(detail_card);
}

static void show_aircraft_detail(lv_event_t *event)
{
    const radar_aircraft_t *plane = lv_event_get_user_data(event);
    if (!plane) return;
    selected_aircraft = *plane;
    plane = &selected_aircraft;
    if (!radar_storage_present()) radar_storage_init();
    close_detail(NULL);

    lv_obj_t *screen = lv_scr_act();
    detail_card = lv_obj_create(screen);
    /* Match the registry panel: both cards remain inside the round screen's
     * central safe area. */
    lv_obj_set_size(detail_card, 350, 310);
    lv_obj_set_pos(detail_card, 65, 95);
    lv_obj_set_style_radius(detail_card, 14, 0);
    lv_obj_set_style_bg_color(detail_card, lv_color_hex(0x081822), 0);
    lv_obj_set_style_bg_opa(detail_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(detail_card, lv_color_hex(0x106420), 0);
    lv_obj_set_style_border_width(detail_card, 2, 0);
    lv_obj_set_style_pad_all(detail_card, 0, 0);
    lv_obj_clear_flag(detail_card, LV_OBJ_FLAG_SCROLLABLE);

    char line[128];
    radar_type_info_t builtin_type_info;
    radar_storage_type_info_t sd_type_info;
    const bool type_from_sd = radar_storage_load_type(plane->type, &sd_type_info);
    const bool type_known = type_from_sd || radar_type_lookup(plane->type, &builtin_type_info);
    snprintf(line, sizeof(line), "%s", plane->callsign[0] ? plane->callsign : "Unknown");
    add_detail_line(detail_card, line, 12, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Type: %s", plane->type[0] ? plane->type : "unknown");
    add_detail_line(detail_card, line, 34, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Class: %s", aircraft_kind_name(plane->kind));
    add_detail_line(detail_card, line, 58, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Make: %s", type_known ? (type_from_sd ? sd_type_info.manufacturer : builtin_type_info.manufacturer) : "not in local catalogue");
    add_detail_line(detail_card, line, 82, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Model: %s", type_known ? (type_from_sd ? sd_type_info.model : builtin_type_info.model) : "unknown");
    add_detail_line(detail_card, line, 106, &lv_font_montserrat_14);
    if (type_known) {
        snprintf(line, sizeof(line), "%s, %u x %s",
                 type_from_sd ? sd_type_info.airframe : builtin_type_info.airframe,
                 (unsigned)(type_from_sd ? sd_type_info.engine_count : builtin_type_info.engine_count),
                 type_from_sd ? sd_type_info.engine_type : builtin_type_info.engine_type);
    } else {
        snprintf(line, sizeof(line), "Airframe and engines: unknown");
    }
    add_detail_line(detail_card, line, 130, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Altitude: %.0f ft", plane->altitude_feet);
    add_detail_line(detail_card, line, 154, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Speed: %.0f kt   Heading: %.0f deg", plane->speed_knots, plane->heading);
    add_detail_line(detail_card, line, 178, &lv_font_montserrat_14);
    snprintf(line, sizeof(line), "Position: %.4f, %.4f", plane->lat, plane->lon);
    add_detail_line(detail_card, line, 202, &lv_font_montserrat_14);

    lv_obj_t *more = lv_btn_create(detail_card);
    lv_obj_set_size(more, 170, 44);
    lv_obj_set_pos(more, 16, 250);
    lv_obj_set_style_bg_color(more, lv_color_hex(0x106420), 0);
    lv_obj_add_event_cb(more, show_registry_detail, LV_EVENT_CLICKED, &selected_aircraft);
    lv_obj_t *more_label = lv_label_create(more);
    lv_label_set_text(more_label, "MORE");
    lv_obj_center(more_label);

    lv_obj_t *close = lv_btn_create(detail_card);
    /* A generous invisible target around the compact visible X. */
    lv_obj_set_size(close, 80, 56);
    lv_obj_set_pos(close, 260, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_add_event_cb(close, close_detail, LV_EVENT_PRESSED, NULL);
    lv_obj_t *x = lv_label_create(close);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_center(x);
    /* The entire details card is also a close target, not just its X. */
    lv_obj_add_event_cb(detail_card, close_detail, LV_EVENT_PRESSED, NULL);
    lv_obj_move_foreground(detail_card);
}

void radar_ui_show_aircraft(const radar_aircraft_t *aircraft, size_t count)
{
    if (!aircraft_layer) return;
    lv_obj_clean(aircraft_layer);
    double stored_lat, stored_lon;
    radar_aircraft_center(&stored_lat, &stored_lon);
    const float center_lat = (float)stored_lat;
    const float center_lon = (float)stored_lon;
    const float km_per_lon = 111.32f * cosf(center_lat * (float)M_PI / 180.0f);
    const float outer_km = radar_range_current()->outer_km;
    for (size_t i = 0; i < count; ++i) {
        const float dx = (aircraft[i].lon - center_lon) * km_per_lon;
        const float dy = (aircraft[i].lat - center_lat) * 110.57f;
        if (sqrtf(dx * dx + dy * dy) > outer_km) continue;
        const int x = RADAR_CENTER + (int)lroundf(dx * 224.0f / outer_km);
        const int y = RADAR_CENTER - (int)lroundf(dy * 224.0f / outer_km);
        const float heading = aircraft[i].heading * (float)M_PI / 180.0f;
        const lv_color_t color = aircraft_color(aircraft[i].kind);
        const int vector_length = 12 + (int)fminf(28.0f, aircraft[i].speed_knots / 12.0f);
        aircraft_vectors[i][0] = (lv_point_t){x, y};
        aircraft_vectors[i][1] = (lv_point_t){
            x + (int)lroundf(sinf(heading) * vector_length),
            y - (int)lroundf(cosf(heading) * vector_length),
        };
        lv_obj_t *vector = lv_line_create(aircraft_layer);
        lv_line_set_points(vector, aircraft_vectors[i], 2);
        /* Track vectors convey motion only; keep classification colour on
         * the aircraft icon and callsign so the two signals are distinct. */
        lv_obj_set_style_line_color(vector, lv_color_hex(0x9A9A9A), 0);
        lv_obj_set_style_line_width(vector, 2, 0);
        lv_obj_clear_flag(vector, LV_OBJ_FLAG_CLICKABLE);
        draw_aircraft_icon(aircraft_layer, i, x, y, heading, aircraft[i].kind, color);
        if (aircraft[i].callsign[0]) {
            lv_obj_t *label = lv_label_create(aircraft_layer);
            lv_label_set_text(label, aircraft[i].callsign);
            lv_obj_set_style_text_color(label, color, 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(label, x + 7, y - 12);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        }
        /* A 100 px target makes aircraft selection reliable with a fingertip. */
        aircraft_hits[i] = aircraft[i];
        lv_obj_t *hit = lv_obj_create(aircraft_layer);
        lv_obj_set_size(hit, 100, 100);
        lv_obj_set_pos(hit, x - 50, y - 50);
        lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hit, 0, 0);
        lv_obj_set_style_pad_all(hit, 0, 0);
        lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(hit, show_aircraft_detail, LV_EVENT_CLICKED, &aircraft_hits[i]);
    }
}

static void add_ring(lv_obj_t *parent, int diameter)
{
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_set_size(ring, diameter, diameter);
    /* Do not rely on the screen object's content-area alignment. */
    lv_obj_set_pos(ring, (RADAR_SIZE - diameter) / 2, (RADAR_SIZE - diameter) / 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x106420), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
}

static void add_line(lv_obj_t *parent, const lv_point_t points[2])
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(0x106420), 0);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

void radar_ui_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_size(screen, RADAR_SIZE, RADAR_SIZE);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x040A1C), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    /* Range is configured through the web UI; a touch must not redraw or
     * alter the radar display. */
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    add_ring(screen, 448);
    add_ring(screen, 336);
    add_ring(screen, 224);
    add_ring(screen, 112);
    add_line(screen, vertical_line);
    add_line(screen, horizontal_line);

    const char *cardinal[] = {"N", "E", "S", "W"};
    const lv_point_t cardinal_pos[] = {{234, 4}, {460, 233}, {234, 460}, {4, 233}};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, cardinal[i]);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(label, cardinal_pos[i].x, cardinal_pos[i].y);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    }
    range_label = lv_label_create(screen);
    lv_obj_set_style_text_color(range_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(range_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(range_label, 400, 108);
    lv_obj_clear_flag(range_label, LV_OBJ_FLAG_CLICKABLE);
    update_range_label();
    aircraft_layer = lv_obj_create(screen);
    lv_obj_set_size(aircraft_layer, RADAR_SIZE, RADAR_SIZE);
    lv_obj_set_pos(aircraft_layer, 0, 0);
    lv_obj_set_style_bg_opa(aircraft_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(aircraft_layer, 0, 0);
    lv_obj_set_style_pad_all(aircraft_layer, 0, 0);
    lv_obj_clear_flag(aircraft_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void radar_ui_tick(void)
{
    radar_photo_t photo;
    if (radar_photo_take(&photo)) {
        displayed_photo = photo;
        if (photo_label && lv_obj_is_valid(photo_label)) {
            if (displayed_photo.found && detail_card) {
                /* lv_canvas owns its image descriptor. This avoids handing
                 * LVGL a separately managed variable descriptor while the
                 * renderer is asynchronously refreshing the RGB display. */
                photo_image = lv_canvas_create(detail_card);
                lv_canvas_set_buffer(photo_image, (void *)displayed_photo.image.data,
                                     displayed_photo.image.header.w, displayed_photo.image.header.h,
                                     LV_IMG_CF_TRUE_COLOR);
                lv_obj_set_pos(photo_image, 20, 188);
                lv_obj_clear_flag(photo_image, LV_OBJ_FLAG_CLICKABLE);
                lv_label_set_text(photo_label, "Photo: Wikimedia Commons");
                lv_obj_set_pos(photo_label, 190, 208);
            } else {
                lv_label_set_text(photo_label, photo.status[0] ? photo.status : "Photo: not found\non Wikimedia Commons");
            }
        }
    }
    radar_ginfo_t result;
    if (!radar_ginfo_take(&result)) return;
    static char line[224];
    if (registry_owner_label && lv_obj_is_valid(registry_owner_label)) {
        if (result.owner[0]) { lv_label_set_text(registry_owner_label, result.owner); }
        if (result.operator_name[0] && registry_operator_label && lv_obj_is_valid(registry_operator_label)) { lv_label_set_text(registry_operator_label, result.operator_name); }
        if (result.year[0] && registry_year_label && lv_obj_is_valid(registry_year_label)) { snprintf(line, sizeof(line), "Built: %s", result.year); lv_label_set_text(registry_year_label, line); }
        if ((result.owner[0] || result.operator_name[0] || result.year[0]) &&
            registry_source_label && lv_obj_is_valid(registry_source_label)) {
            lv_label_set_text(registry_source_label, "Data: CAA + SD registry");
        }
        start_live_photo(photo_registration[0] ? photo_registration : result.registration);
        return;
    }
    if (!ginfo_label || !lv_obj_is_valid(ginfo_label)) return;
    if (result.registration[0]) {
        snprintf(line, sizeof(line), "CAA Registration: %s\nCAA Owner: %s\nCAA Operator: %s",
                 result.registration,
                 result.owner[0] ? result.owner : "not listed",
                 result.operator_name[0] ? result.operator_name : "not listed");
    } else {
        snprintf(line, sizeof(line), "CAA G-INFO: no UK record");
    }
    lv_label_set_text(ginfo_label, line);
    start_live_photo(photo_registration[0] ? photo_registration : result.registration);
}
