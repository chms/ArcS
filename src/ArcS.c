/* ArcS, yet another watchface based on arcs
 * Copyright (C) 2015  Christian M. Schmid
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "pebble.h"

#define MINUTE_CIRCLE_THICKNESS  4
#define HOUR_CIRCLE_THICKNESS    8
#define CIRCLE_SPACING_THICKNESS 6

#define LOGO_WIDTH   78
#define LOGO_HEIGHT  48

#define DIGITAL_CLOCK_DURATION 5000
#define HIDE_LOGO              true
#define DIGITAL_ALWAYS_ON      true

static Window *window;
static GBitmap *bitmap_logo;
static BitmapLayer *bitmap_layer_logo;
static Layer *layer_arcs, *layer_battery_bluetooth;

static TextLayer *text_layer_time;
static TextLayer *text_layer_date;

static GRect window_bounds;
static GPoint window_center;

static bool digital_clock_is_visible;

static int angle_90 = TRIG_MAX_ANGLE / 4;
static int angle_180 = TRIG_MAX_ANGLE / 2;
static int angle_270 = 3 * TRIG_MAX_ANGLE / 4;

static void graphics_draw_arc(GContext *ctx, GPoint center, int radius, int thickness, int start_angle, int end_angle, GColor c) {
  int32_t xmin = 65535000, xmax = -65535000, ymin = 65535000, ymax = -65535000;
  int32_t cosStart, sinStart, cosEnd, sinEnd;
  int32_t r, t;
  
  while (start_angle < 0) start_angle += TRIG_MAX_ANGLE;
  while (end_angle < 0) end_angle += TRIG_MAX_ANGLE;

  start_angle %= TRIG_MAX_ANGLE;
  end_angle %= TRIG_MAX_ANGLE;
  
  if (end_angle == 0) end_angle = TRIG_MAX_ANGLE;
  
  if (start_angle > end_angle) {
    graphics_draw_arc(ctx, center, radius, thickness, start_angle, TRIG_MAX_ANGLE, c);
    graphics_draw_arc(ctx, center, radius, thickness, 0, end_angle, c);
  } else {
    // Calculate bounding box for the arc to be drawn
    cosStart = cos_lookup(start_angle);
    sinStart = sin_lookup(start_angle);
    cosEnd = cos_lookup(end_angle);
    sinEnd = sin_lookup(end_angle);
    
    r = radius;
    // Point 1: radius & start_angle
    t = r * cosStart;
    if (t < xmin) xmin = t;
    if (t > xmax) xmax = t;
    t = r * sinStart;
    if (t < ymin) ymin = t;
    if (t > ymax) ymax = t;

    // Point 2: radius & end_angle
    t = r * cosEnd;
    if (t < xmin) xmin = t;
    if (t > xmax) xmax = t;
    t = r * sinEnd;
    if (t < ymin) ymin = t;
    if (t > ymax) ymax = t;
    
    r = radius - thickness;
    // Point 3: radius-thickness & start_angle
    t = r * cosStart;
    if (t < xmin) xmin = t;
    if (t > xmax) xmax = t;
    t = r * sinStart;
    if (t < ymin) ymin = t;
    if (t > ymax) ymax = t;

    // Point 4: radius-thickness & end_angle
    t = r * cosEnd;
    if (t < xmin) xmin = t;
    if (t > xmax) xmax = t;
    t = r * sinEnd;
    if (t < ymin) ymin = t;
    if (t > ymax) ymax = t;
    
    // Normalization
    xmin /= TRIG_MAX_RATIO;
    xmax /= TRIG_MAX_RATIO;
    ymin /= TRIG_MAX_RATIO;
    ymax /= TRIG_MAX_RATIO;
        
    // Corrections if arc crosses X or Y axis
    if ((start_angle < angle_90) && (end_angle > angle_90)) {
      ymax = radius;
    }
    
    if ((start_angle < angle_180) && (end_angle > angle_180)) {
      xmin = -radius;
    }
    
    if ((start_angle < angle_270) && (end_angle > angle_270)) {
      ymin = -radius;
    }
    
    // Slopes for the two sides of the arc
    float sslope = (float)cosStart/ (float)sinStart;
    float eslope = (float)cosEnd / (float)sinEnd;
   
    if (end_angle == TRIG_MAX_ANGLE) eslope = -1000000;
   
    int ir2 = (radius - thickness) * (radius - thickness);
    int or2 = radius * radius;
   
    graphics_context_set_stroke_color(ctx, c);

    for (int x = xmin; x <= xmax; x++) {
      for (int y = ymin; y <= ymax; y++)
      {
        int x2 = x * x;
        int y2 = y * y;
   
        if (
          (x2 + y2 < or2 && x2 + y2 >= ir2) && (
            (y > 0 && start_angle < angle_180 && x <= y * sslope) ||
            (y < 0 && start_angle > angle_180 && x >= y * sslope) ||
            (y < 0 && start_angle <= angle_180) ||
            (y == 0 && start_angle <= angle_180 && x < 0) ||
            (y == 0 && start_angle == 0 && x > 0)
          ) && (
            (y > 0 && end_angle < angle_180 && x >= y * eslope) ||
            (y < 0 && end_angle > angle_180 && x <= y * eslope) ||
            (y > 0 && end_angle >= angle_180) ||
            (y == 0 && end_angle >= angle_180 && x < 0) ||
            (y == 0 && start_angle == 0 && x > 0)
          )
        )
        graphics_draw_pixel(ctx, GPoint(center.x+x, center.y+y));
      }
    }
  }
}

static void update_circles(Layer *layer, GContext *ctx) {
    time_t now = time(NULL);
    struct tm *tick_time = localtime(&now);
    int32_t hour_angle   = TRIG_MAX_ANGLE * (tick_time->tm_hour%12)/12 + TRIG_MAX_ANGLE/12 * tick_time->tm_min/60;
    int32_t minute_angle = TRIG_MAX_ANGLE * tick_time->tm_min/60;
    //int32_t hour_angle   = TRIG_MAX_ANGLE-1;
    //int32_t minute_angle = TRIG_MAX_ANGLE-1;
    
    graphics_draw_arc(ctx, window_center, window_center.x, HOUR_CIRCLE_THICKNESS, angle_270, hour_angle+angle_270, GColorWhite);
    graphics_draw_arc(ctx, window_center, window_center.x-CIRCLE_SPACING_THICKNESS-HOUR_CIRCLE_THICKNESS, MINUTE_CIRCLE_THICKNESS, angle_270, minute_angle+angle_270, GColorWhite);
}

static void update_battery_bluetooth(Layer *layer, GContext *ctx) {
    uint8_t shift_battery = 6;

    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (bluetooth_connection_service_peek()) {
        graphics_draw_line(ctx, GPoint(24,0), GPoint(24,12));
        graphics_draw_line(ctx, GPoint(21,3), GPoint(27,9));
        graphics_draw_line(ctx, GPoint(21,9), GPoint(27,3));
        graphics_draw_line(ctx, GPoint(25,1), GPoint(26,2));
        graphics_draw_line(ctx, GPoint(25,11), GPoint(26,10));
        shift_battery = 0;
    }
    
    graphics_draw_rect(ctx, GRect(0+shift_battery, 3, 14, 8));
    graphics_draw_line(ctx, GPoint(14+shift_battery,5), GPoint(14+shift_battery,8));
    
    BatteryChargeState battery_state = battery_state_service_peek();
    uint8_t charge_percent = battery_state.charge_percent + 10;
    uint8_t x_bar = shift_battery + 2;
    

    // Fill battery bars
    while(charge_percent > 15) {
        graphics_draw_line(ctx, GPoint(x_bar,5), GPoint(x_bar, 8));
        x_bar++;
        charge_percent = charge_percent - 10;
    }
    
}

static void update_digital_clock_t(struct tm *tick_time) {
    static char s_time_text[] = "20:00";
    static char s_date_text[] = "Xxx, 00.00.";
    char *time_format;

    if (clock_is_24h_style()) time_format = "%R";
    else time_format = "%I:%M";

    strftime(s_time_text, sizeof(s_time_text), time_format, tick_time);
    strftime(s_date_text, sizeof(s_date_text), "%a, %e.%m.", tick_time);

    text_layer_set_text(text_layer_time, s_time_text);
    text_layer_set_text(text_layer_date, s_date_text);
}

static void update_digital_clock() {
    time_t now = time(NULL);
    struct tm *tick_time = localtime(&now);
    update_digital_clock_t(tick_time);
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
    update_digital_clock_t(tick_time);
    layer_mark_dirty(layer_arcs);
}

static void show_digital_clock(bool show_digital_clock) {
    digital_clock_is_visible = show_digital_clock || DIGITAL_ALWAYS_ON;
    layer_set_hidden(bitmap_layer_get_layer(bitmap_layer_logo), digital_clock_is_visible || HIDE_LOGO);
    layer_set_hidden(layer_battery_bluetooth, !digital_clock_is_visible);
    layer_set_hidden(text_layer_get_layer(text_layer_time), !digital_clock_is_visible);
    layer_set_hidden(text_layer_get_layer(text_layer_date), !digital_clock_is_visible);
}

static void tap_timer(void *data) {
    show_digital_clock(false);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
    if(!digital_clock_is_visible) app_timer_register(DIGITAL_CLOCK_DURATION, tap_timer, NULL);
    show_digital_clock(true);
}

static void window_load(Window *window) {
    // Set window background to black and get window layer and dimensions
    window_set_background_color(window, GColorBlack);
    Layer *window_layer = window_get_root_layer(window);
    window_bounds   = layer_get_bounds(window_layer);
    window_center.x = window_bounds.size.w/2;
    window_center.y = window_bounds.size.h/2;

    // Create and style logo layer
    bitmap_logo = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CS);
    bitmap_layer_logo = bitmap_layer_create((GRect) { .origin = {window_center.x-LOGO_WIDTH/2, window_center.y-LOGO_HEIGHT/2}, .size = {LOGO_WIDTH, LOGO_HEIGHT } });
    bitmap_layer_set_bitmap(bitmap_layer_logo, bitmap_logo);  

    // Create and style digital time layer
    text_layer_time = text_layer_create((GRect) { .origin = { 0, window_center.y-26}, .size = { window_bounds.size.w, 34 } });
    text_layer_set_font(text_layer_time, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
    text_layer_set_text_alignment(text_layer_time, GTextAlignmentCenter);
    text_layer_set_background_color(text_layer_time, GColorClear);
    text_layer_set_text_color(text_layer_time, GColorWhite);

    // Create and style digital date layer
    text_layer_date = text_layer_create((GRect) { .origin = { 0, window_center.y+9}, .size = { window_bounds.size.w, 34 } });
    text_layer_set_font(text_layer_date, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(text_layer_date, GTextAlignmentCenter);
    text_layer_set_background_color(text_layer_date, GColorClear);
    text_layer_set_text_color(text_layer_date, GColorWhite);

    // Create and style arc (analog) layer
    layer_arcs = layer_create(window_bounds);
    layer_set_update_proc(layer_arcs, update_circles);

    // Create and style battery layer
    layer_battery_bluetooth = layer_create(GRect(58, 44, 28, 13));
    //layer_set_update_proc(layer_battery_bluetooth, update_battery_bluetooth);
    
    // Update time and date
    update_digital_clock();
    show_digital_clock(false);

    // Add layers to root layer
    layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer_logo));
    layer_add_child(window_layer, text_layer_get_layer(text_layer_time));
    layer_add_child(window_layer, text_layer_get_layer(text_layer_date));
    layer_add_child(window_layer, layer_arcs);
    layer_add_child(window_layer, layer_battery_bluetooth);
}

static void window_unload(Window *window) {
  bitmap_layer_destroy(bitmap_layer_logo);
  gbitmap_destroy(bitmap_logo);
  text_layer_destroy(text_layer_time);
  text_layer_destroy(text_layer_date);
  layer_destroy(layer_arcs);
  layer_destroy(layer_battery_bluetooth);
}

static void init(void) {
  // Create main window element and assign to pointer
  window = window_create();

  // Set handlers to manage the elements inside the window
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  // Show the window on the watch, with animated=true
  window_stack_push(window, true);
  
  // Add timer for minutes
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

  // React to taps
  accel_tap_service_subscribe(tap_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
