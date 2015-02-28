#include "pebble.h"

#define MINUTE_CIRCLE_THICKNESS  4
#define HOUR_CIRCLE_THICKNESS    8
#define CIRCLE_SPACING_THICKNESS 6

#define LOGO_WIDTH   78
#define LOGO_HEIGHT  48

static Window *window;
static GBitmap *bitmap_cs;
static BitmapLayer *bitmap_layer_cs;
static Layer *layer_arcs;

static TextLayer *text_layer_time;
static TextLayer *text_layer_date;

static GRect window_bounds;
static GPoint window_center;

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

static void updateCircles(Layer *layer, GContext *ctx) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int32_t hour_angle   = TRIG_MAX_ANGLE * (t->tm_hour%12)/12 + TRIG_MAX_ANGLE/12 * t->tm_min/60;
        int32_t minute_angle = TRIG_MAX_ANGLE * t->tm_min/60;
        //int32_t hour_angle   = TRIG_MAX_ANGLE-1;
        //int32_t minute_angle = TRIG_MAX_ANGLE-1;

        graphics_draw_arc(ctx, window_center, window_center.x, HOUR_CIRCLE_THICKNESS, angle_270, hour_angle+angle_270, GColorWhite);
        graphics_draw_arc(ctx, window_center, window_center.x-CIRCLE_SPACING_THICKNESS-HOUR_CIRCLE_THICKNESS, MINUTE_CIRCLE_THICKNESS, angle_270, minute_angle+angle_270, GColorWhite);
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(window_get_root_layer(window));
}

static void tapHandler(AccelAxisType axis, int32_t direction) {
	/*if (step) return;	
	timeHandler(NULL);*/
}

static void window_load(Window *window) {
  // Set window background to black and get window layer and dimensions
  window_set_background_color(window, GColorBlack);
  Layer *window_layer = window_get_root_layer(window);
  window_bounds   = layer_get_bounds(window_layer);
  window_center.x = window_bounds.size.w/2;
  window_center.y = window_bounds.size.h/2;

  bitmap_cs = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CS);
  bitmap_layer_cs = bitmap_layer_create((GRect) { .origin = {window_center.x-LOGO_WIDTH/2, window_center.y-LOGO_HEIGHT/2}, .size = {LOGO_WIDTH, LOGO_HEIGHT } });
  bitmap_layer_set_bitmap(bitmap_layer_cs, bitmap_cs);

  text_layer_time = text_layer_create((GRect) { .origin = { 0, window_center.y-26}, .size = { window_bounds.size.w, 34 } });
  text_layer_set_text(text_layer_time, "23:59");
  text_layer_set_font(text_layer_time, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_text_alignment(text_layer_time, GTextAlignmentCenter);
  text_layer_set_background_color(text_layer_time, GColorClear);
  text_layer_set_text_color(text_layer_time, GColorWhite);

  text_layer_date = text_layer_create((GRect) { .origin = { 0, window_center.y+11}, .size = { window_bounds.size.w, 34 } });
  text_layer_set_text(text_layer_date, "Mon, 22.12.");
  text_layer_set_font(text_layer_date, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_layer_date, GTextAlignmentCenter);
  text_layer_set_background_color(text_layer_date, GColorClear);
  text_layer_set_text_color(text_layer_date, GColorWhite);

  layer_arcs = layer_create(window_bounds);
  layer_set_update_proc(layer_arcs, updateCircles);

  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer_cs));
  //layer_add_child(window_layer, text_layer_get_layer(text_layer_time));
  //layer_add_child(window_layer, text_layer_get_layer(text_layer_date));
  layer_add_child(window_layer, layer_arcs);
}

static void window_unload(Window *window) {
  bitmap_layer_destroy(bitmap_layer_cs);
  gbitmap_destroy(bitmap_cs);
  text_layer_destroy(text_layer_time);
  text_layer_destroy(text_layer_date);
  layer_destroy(layer_arcs);
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
  tick_timer_service_subscribe(SECOND_UNIT, handle_minute_tick);

  // React to taps
  accel_tap_service_subscribe(tapHandler);
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
