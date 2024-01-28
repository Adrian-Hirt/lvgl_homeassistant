#ifdef SIMULATOR
  #include "ha_panel.h"
  #include <sys/time.h>
  #include <curl/curl.h>
#else
  #include "secrets.h"
  #include "lvgl_functions.h"
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif

// Some variables are in a (not-checked in) header file, you need to define the
// following constants in your codebase:
// const char* ssid = "YOUR-SSID";
// const char* password = "YOUR-PASSWORD";
// const char* auth_token_header = "Authorization: Bearer <YOUR-TOKEN-FROM-HOMEASSISTANT>";
// const char* auth_token = "Bearer "<YOUR-TOKEN-FROM-HOMEASSISTANT>";

// Some globals
lv_obj_t *wifi_connected_label;
lv_obj_t *wifi_ip_address_label;

// Settings for wifi
unsigned long previous_wifi_check_milis = 0;
unsigned long wifi_check_interval = 5000; // 5 seconds

// API of Homeassistant
const char* light_toggle_url = "http://homeassistant.local:8123/api/services/light/toggle";
const char* light_turn_on_url = "http://homeassistant.local:8123/api/services/light/turn_on";

// Services of home assistant
const char *services[] = {
  "light.esstischlampen",
  "light.wohnzimmerspots",
  "light.wohnzimmer_alle",
  "light.shapes_8bc1"
};

static uint32_t esstisch_light_service = 0;
static uint32_t wohnzimmerspots_service = 1;
static uint32_t wohnzimmer_all_service = 2;
static uint32_t nanoleaf_light_service = 3;

// Prints a message, for debugging
void output(const char *message) {
#ifdef SIMULATOR
  printf(message);
#else
  Serial.print(message);
#endif
}

// Returns `true`if the WiFi is connected, `false` otherwise
bool wifi_connected() {
#ifdef SIMULATOR
  return rand() & 1;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

// Perform the initial connect to WiFi
#ifndef SIMULATOR
void connect_wifi() {
  WiFi.setMinSecurity(WIFI_AUTH_WEP);
  WiFi.begin(ssid, password);
}
#endif

// Update the WiFi status text in the "info" tab
void update_wifi_status() {
  if (wifi_connected()) {
    lv_label_set_text(wifi_connected_label, "Connected");
    lv_obj_set_style_text_color(wifi_connected_label, lv_palette_main(LV_PALETTE_GREEN), NULL);
#ifdef SIMULATOR
    lv_label_set_text(wifi_ip_address_label, "192.186.1.1");
#else
    lv_label_set_text(wifi_ip_address_label, WiFi.localIP().toString().c_str());
#endif
  }
  else {
    lv_label_set_text(wifi_connected_label, "Not connected");
    lv_obj_set_style_text_color(wifi_connected_label, lv_palette_main(LV_PALETTE_RED), NULL);
    lv_label_set_text(wifi_ip_address_label, "-");
  }
}

#ifdef SIMULATOR
// Dummy function that does not write the data to stdout
size_t curl_dummy_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
  return size * nmemb;
}
#endif

void perform_post_request(const char* url, const char* request_data) {
#ifdef SIMULATOR
  // Create curl handle
  CURL *curl = curl_easy_init();
  // Set URL
  curl_easy_setopt(curl, CURLOPT_URL, url);

  // Pass in the request data
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data);

  // Setup the HTTP headers
  struct curl_slist *list = NULL;
  list = curl_slist_append(list, "Content-Type: application/json");
  list = curl_slist_append(list, auth_token_header);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

  // Set our dummy write function
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_dummy_write);

  // Perform the request and log the error if any error occured
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    printf("%s\n", curl_easy_strerror(res));
  }

  // Cleanup the curl handle
  curl_easy_cleanup(curl);
#else
  // Create the http client
  HTTPClient http;
  http.begin(url);

  // Disable HTTP basic auth
  http.setAuthorization("");

  // And setup the headers
  http.addHeader("Authorization", auth_token);
  http.addHeader("Content-Type", "application/json");

  // Send the request
  int httpResponseCode = http.POST(request_data);

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
#endif
}

void light_toggle_button_event_callback(lv_event_t *event) {
  // Get the service identifier from the event data
  uint32_t* service_id = (uint32_t*)lv_event_get_user_data(event);
  const char *service = services[*service_id];

  // Build the POST request data
  char data[100];
  sprintf(data, "{\"entity_id\": \"%s\"}", service);

  // And perform the POST request
  perform_post_request(light_toggle_url, data);
}

void brightness_slider_event_callback(lv_event_t *event) {
  // Get the service identifier from the event data
  uint32_t* service_id = (uint32_t*)lv_event_get_user_data(event);
  const char *service = services[*service_id];

  // Get the slider value
  lv_obj_t *slider = lv_event_get_target(event);
  int slider_value = (int)lv_slider_get_value(slider);

  // Build the POST request data
  char data[100];
  sprintf(data, "{\"entity_id\": \"%s\", \"brightness_pct\": %i}", service, slider_value);

  // And perform the POST request
  perform_post_request(light_turn_on_url, data);
}

void temp_slider_event_callback(lv_event_t *event) {
  // Get the service identifier from the event data
  uint32_t* service_id = (uint32_t*)lv_event_get_user_data(event);
  const char *service = services[*service_id];

  // Get the slider value. We need to get the value from [0, 100]
  // to [2000, 6500]. For this, we first take the value to [0, 4500]
  // by multiplying with 45, and then to [2000, 6500] by adding 2000.
  // Due to some bug, nanoleafs only work up to 6490, so we cap the value
  // at that.
  lv_obj_t *slider = lv_event_get_target(event);
  int slider_value = (int)lv_slider_get_value(slider);
  slider_value *= 45;
  slider_value += 2000;
  if (slider_value > 6490){
    slider_value = 6490;
  }

  // Build the POST request data
  char data[100];
  sprintf(data, "{\"entity_id\": \"%s\", \"kelvin\": %i}", service, slider_value);

  // And perform the POST request
  perform_post_request(light_turn_on_url, data);
}

void rgb_slider_event_callback(lv_event_t *event) {
  // Get the service identifier from the event data
  uint32_t* service_id = (uint32_t*)lv_event_get_user_data(event);
  const char *service = services[*service_id];

  // Get the slider value. We use HS mode from home assistant, where
  // the hue must be [0, 360] and S in [0, 100] (we leave S fixed at 100).
  // We can therefore simply multiply the value by 3.6 to get
  // the hue.
  lv_obj_t *slider = lv_event_get_target(event);
  int slider_value = (int)lv_slider_get_value(slider);
  slider_value *= 3.6;

  // Build the POST request data
  char data[100];
  sprintf(data, "{\"entity_id\": \"%s\", \"hs_color\": [%i, 100]}", service, slider_value);

  // And perform the POST request
  perform_post_request(light_turn_on_url, data);
}

void addLightWidget(lv_obj_t *parent, const char *name, lv_coord_t x_coord, lv_coord_t y_coord, uint32_t *service_id) {
  // Create the container
  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_set_size(container, 160, 440);
  lv_obj_set_pos(container, x_coord, y_coord);
  lv_obj_set_style_pad_all(container, 10, NULL);
  lv_obj_set_style_border_width(container, 0, NULL);
  lv_obj_set_style_radius(container, 0, LV_PART_MAIN);

  // Add label
  lv_obj_t *widget_label = lv_label_create(container);
  lv_label_set_text(widget_label, name);
  static lv_style_t style;
  lv_style_init(&style);
  lv_style_set_text_font(&style, &lv_font_montserrat_20);
  lv_obj_add_style(widget_label, &style, NULL);
  lv_obj_align(widget_label, LV_ALIGN_TOP_MID, 0, 0);

  // Add button
  lv_obj_t * btn = lv_btn_create(container);
  lv_obj_set_pos(btn, 0, y_coord + 24);
  lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
  lv_obj_set_size(btn, 140, 100);

  // Add button callback
  lv_obj_add_event_cb(btn, light_toggle_button_event_callback, LV_EVENT_CLICKED, service_id);
  lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

  // Add button label
  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, LV_SYMBOL_POWER);
  lv_obj_center(label);

  // Create slider for Brightness
  lv_obj_t *brightness_slider = lv_slider_create(container);
  lv_obj_set_pos(brightness_slider, 10, y_coord + 144);
  lv_obj_set_size(brightness_slider, 15, 260);
  lv_obj_add_event_cb(brightness_slider, brightness_slider_event_callback, LV_EVENT_RELEASED, service_id);

  // Create slider for light temp
  lv_obj_t *temp_slider = lv_slider_create(container);
  lv_obj_set_pos(temp_slider, 62, y_coord + 144);
  lv_obj_set_size(temp_slider, 15, 260);
  lv_obj_add_event_cb(temp_slider, temp_slider_event_callback, LV_EVENT_RELEASED, service_id);

  // Create slider for rgb value
  lv_obj_t *rgb_slider = lv_slider_create(container);
  lv_obj_set_pos(rgb_slider, 115, y_coord + 144);
  lv_obj_set_size(rgb_slider, 15, 260);
  lv_obj_add_event_cb(rgb_slider, rgb_slider_event_callback, LV_EVENT_RELEASED, service_id);
}

void addDataContents(lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, "Coming soon :)");
}

void addInfoContents(lv_obj_t *parent) {
  // -------------------------------------------------------------------------
  // Network info
  // -------------------------------------------------------------------------
  unsigned int value_x_offset = 120;

  // Create the container
  lv_obj_t *network_container = lv_obj_create(parent);
  lv_obj_set_size(network_container, 330, 440);
  lv_obj_set_pos(network_container, 0, 0);
  lv_obj_set_style_pad_all(network_container, 10, NULL);
  lv_obj_set_style_border_width(network_container, 0, NULL);
  lv_obj_set_style_radius(network_container, 0, LV_PART_MAIN);

  // Add label
  lv_obj_t *widget_label = lv_label_create(network_container);
  lv_label_set_text(widget_label, "Network");
  static lv_style_t style;
  lv_style_init(&style);
  lv_style_set_text_font(&style, &lv_font_montserrat_20);
  lv_obj_add_style(widget_label, &style, NULL);

  // Add "wifi-connected" label
  lv_obj_t *wifi_label = lv_label_create(network_container);
  lv_label_set_text(wifi_label, "Wifi status:");
  lv_obj_set_pos(wifi_label, 0, 30);
  wifi_connected_label = lv_label_create(network_container);
  lv_obj_set_pos(wifi_connected_label, value_x_offset, 30);
  lv_label_set_text(wifi_connected_label, "Not connected");
  lv_obj_set_style_text_color(wifi_connected_label, lv_palette_main(LV_PALETTE_RED), NULL);

  // Add wifi ip address label
  lv_obj_t *wifi_addr_label = lv_label_create(network_container);
  lv_label_set_text(wifi_addr_label, "IP address:");
  lv_obj_set_pos(wifi_addr_label, 0, 50);
  wifi_ip_address_label = lv_label_create(network_container);
  lv_obj_set_pos(wifi_ip_address_label, value_x_offset, 50);
  lv_label_set_text(wifi_ip_address_label, "-");

  // Add SSID label
  lv_obj_t *ssid_label = lv_label_create(network_container);
  lv_label_set_text(ssid_label, "SSID:");
  lv_obj_set_pos(ssid_label, 0, 70);
  ssid_label = lv_label_create(network_container);
  lv_obj_set_pos(ssid_label, value_x_offset, 70);
  lv_label_set_text(ssid_label, ssid);

  // -------------------------------------------------------------------------
  // Other info
  // -------------------------------------------------------------------------
  // Create the container
  lv_obj_t *other_info_container = lv_obj_create(parent);
  lv_obj_set_size(other_info_container, 330, 440);
  lv_obj_set_pos(other_info_container, 350, 0);
  lv_obj_set_style_pad_all(other_info_container, 10, NULL);
  lv_obj_set_style_border_width(other_info_container, 0, NULL);
  lv_obj_set_style_radius(other_info_container, 0, LV_PART_MAIN);

  // Add label
  widget_label = lv_label_create(other_info_container);
  lv_label_set_text(widget_label, "Other");
  lv_obj_add_style(widget_label, &style, NULL);

  // Display LVGL version
  lv_obj_t *lvgl_version_label = lv_label_create(other_info_container);
  lv_label_set_text(lvgl_version_label, "LVGL Version:");
  lv_obj_set_pos(lvgl_version_label, 0, 30);
  lv_obj_t *lvgl_version_value = lv_label_create(other_info_container);
  lv_obj_set_pos(lvgl_version_value, value_x_offset, 30);
  char lvgl_version_str[6];
  sprintf(lvgl_version_str,"%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_label_set_text(lvgl_version_value, lvgl_version_str);
}

void layout() {
  /*Create a Tab view object*/
  lv_obj_t * tabview;
  // TODO: maybe add a little "taskbar" at the bottom?
  tabview = lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, 80);

  lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_set_style_text_color(tab_btns, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
  lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_RIGHT, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_20, NULL);


  /*Add 3 tabs (the tabs are page (lv_page) and can be scrolled*/
  lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Lights");
  lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Data");
  lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Info");

  /*Add content to the tabs*/
  // Tab 1
  addLightWidget(tab1, "Spots", 0, 0, &wohnzimmerspots_service);
  addLightWidget(tab1, "Table", 175, 0, &esstisch_light_service);
  addLightWidget(tab1, "All", 350, 0, &wohnzimmer_all_service);
  addLightWidget(tab1, "Nanoleaf", 525, 0, &nanoleaf_light_service);

  // Tab 2
  addDataContents(tab2);

  // Tab 3
  addInfoContents(tab3);

  lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
}

#ifdef SIMULATOR
void ha_panel() {
  layout();
}
#endif

#if !defined(SIMULATOR)
void setup() {
  Serial.begin(115200); /* prepare for possible serial debug */

  // Connect to wifi
  // Maybe this should happen in background / after starting the GUI
  connect_wifi();

  // Run the prepare method
  prepare_lvgl();

  // Run our code
  layout();

  // Run the finish method
  finish_lvgl();
}
#endif

void loop() {
#ifdef SIMULATOR
  struct timeval time;
  gettimeofday(&time, NULL);
  unsigned long current_millis = time.tv_sec * 1000;
#else
  unsigned long current_millis = millis();
#endif


  if(current_millis - previous_wifi_check_milis >= wifi_check_interval) {
    update_wifi_status();
    previous_wifi_check_milis = current_millis;
  }
}
