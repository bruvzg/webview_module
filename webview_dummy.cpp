/*************************************************************************/
/*  webview_dummy.cpp                                                    */
/*************************************************************************/

#include "webview.h"

WebViewOverlay::WebViewOverlay() {
}

WebViewOverlay::~WebViewOverlay() {
}

void WebViewOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			_draw_error("Not supported!");
		} break;
		default: {
			//NOP
		} break;
	}
}

void WebViewOverlay::get_snapshot(int p_width) {}

void WebViewOverlay::set_no_background(bool p_bg) {
	no_background = p_bg;
}

bool WebViewOverlay::get_no_background() const {
	return no_background;
}

void WebViewOverlay::set_url(const String& p_url) {
	home_url = p_url;
}

String WebViewOverlay::get_url() const {
	return home_url;
}

void WebViewOverlay::set_user_agent(const String& p_user_agent) {
	user_agent = p_user_agent;
}

String WebViewOverlay::get_user_agent() const {
	return user_agent;
}

double WebViewOverlay::get_zoom_level() const {
	return zoom;
}

void WebViewOverlay::set_zoom_level(double p_zoom) {
	zoom = p_zoom;
}

String WebViewOverlay::get_title() const {
	return "";
}

void WebViewOverlay::execute_java_script(const String &p_script) {}

void WebViewOverlay::load_string(const String &p_source) {}

bool WebViewOverlay::can_go_back() const {
	return false;
}

bool WebViewOverlay::can_go_forward() const {
	return false;
}

bool WebViewOverlay::is_ready() const {
	return false;
}

bool WebViewOverlay::is_loading() const {
	return false;
}

bool WebViewOverlay::is_secure_content() const {
	return false;
}

void WebViewOverlay::go_back() {}

void WebViewOverlay::go_forward() {}

void WebViewOverlay::reload() {}

void WebViewOverlay::stop() {}

void WebViewOverlay::init() {}

void WebViewOverlay::finish() {}