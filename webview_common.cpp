/*************************************************************************/
/*  webview.cpp                                                          */
/*************************************************************************/

#include "webview.h"
#include "webview_icons.h"

int WebViewOverlay::err_status = -1;

void WebViewOverlay::_bind_methods() {
	ClassDB::bind_method(D_METHOD("can_go_back"), &WebViewOverlay::can_go_back);
	ClassDB::bind_method(D_METHOD("can_go_forward"), &WebViewOverlay::can_go_forward);

	ClassDB::bind_method(D_METHOD("is_loading"), &WebViewOverlay::is_loading);
	ClassDB::bind_method(D_METHOD("is_secure_content"), &WebViewOverlay::is_secure_content);

	ClassDB::bind_method(D_METHOD("go_back"), &WebViewOverlay::go_back);
	ClassDB::bind_method(D_METHOD("go_forward"), &WebViewOverlay::go_forward);
	ClassDB::bind_method(D_METHOD("reload"), &WebViewOverlay::reload);
	ClassDB::bind_method(D_METHOD("stop"), &WebViewOverlay::stop);

	ClassDB::bind_method(D_METHOD("set_no_background", "no_background"), &WebViewOverlay::set_no_background);
	ClassDB::bind_method(D_METHOD("get_no_background"), &WebViewOverlay::get_no_background);

	ClassDB::bind_method(D_METHOD("set_url", "url"), &WebViewOverlay::set_url);
	ClassDB::bind_method(D_METHOD("get_url"), &WebViewOverlay::get_url);

	ClassDB::bind_method(D_METHOD("set_user_agent", "user_agent"), &WebViewOverlay::set_user_agent);
	ClassDB::bind_method(D_METHOD("get_user_agent"), &WebViewOverlay::get_user_agent);

	ClassDB::bind_method(D_METHOD("get_zoom_level"), &WebViewOverlay::get_zoom_level);
	ClassDB::bind_method(D_METHOD("set_zoom_level", "zoom"), &WebViewOverlay::set_zoom_level);

	ClassDB::bind_method(D_METHOD("load_string", "source", "url"), &WebViewOverlay::load_string);
	ClassDB::bind_method(D_METHOD("execute_java_script", "script"), &WebViewOverlay::execute_java_script);

	ClassDB::bind_method(D_METHOD("get_snapshot", "width"), &WebViewOverlay::get_snapshot);

	ClassDB::bind_method(D_METHOD("get_title"), &WebViewOverlay::get_title);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "no_background"), "set_no_background", "get_no_background");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "user_agent"), "set_user_agent", "get_user_agent");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "zoom_level"), "set_zoom_level", "get_zoom_level");

	ADD_SIGNAL(MethodInfo("callback", PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo("new_window", PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo("start_navigation"));
	ADD_SIGNAL(MethodInfo("finish_navigation"));
	ADD_SIGNAL(MethodInfo("snapshot_ready", PropertyInfo(Variant::OBJECT, "image", PROPERTY_HINT_RESOURCE_TYPE, "Image")));
}

void WebViewOverlay::_draw_placeholder() {
	Ref<Font> font = get_font("font", "Label");
	Size2i size = get_size();

	// Main border
	draw_rect(Rect2(Point2(), size), Color(0, 0, 0), false);
	draw_rect(Rect2(Point2(1, 1), size - Size2(2, 2)), Color(1, 1, 1), false);
	draw_rect(Rect2(Point2(2, 2), size - Size2(4, 4)), Color(1, 1, 1), false);
	draw_rect(Rect2(Point2(3, 3), size - Size2(6, 6)), Color(0, 0, 0), false);

	if (icon_main.is_null()) {
		Ref<Image> image = memnew(Image(__icon_dummy, __icon_dummy_len));
		icon_main.instance();
		icon_main->create_from_image(image, Texture::FLAG_VIDEO_SURFACE);
	}

	icon_main->draw(get_canvas_item(), Vector2((size.x - 64) / 2, 20));

	Size2i url_size = font->get_string_size(home_url);
	font->draw(get_canvas_item(), Vector2((size.x - url_size.x) / 2, 100 +url_size.y) + Vector2(1, 1), home_url, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - url_size.x) / 2, 100 +url_size.y) + Vector2(-1, 1), home_url, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - url_size.x) / 2, 100 +url_size.y) + Vector2(1, -1), home_url, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - url_size.x) / 2, 100 +url_size.y) + Vector2(-1, -1), home_url, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - url_size.x) / 2, 100 +url_size.y), home_url, Color(1, 1, 1));
}

void WebViewOverlay::_draw_error(const String &p_error) {
	Ref<Font> font = get_font("font", "Label");
	Size2i size = get_size();

	// Main border
	draw_rect(Rect2(Point2(), size), Color(0, 0, 0), false);
	draw_rect(Rect2(Point2(1, 1), size - Size2(2, 2)), Color(1, 1, 1), false);
	draw_rect(Rect2(Point2(2, 2), size - Size2(4, 4)), Color(1, 1, 1), false);
	draw_rect(Rect2(Point2(3, 3), size - Size2(6, 6)), Color(0, 0, 0), false);

	if (icon_error.is_null()) {
		Ref<Image> image = memnew(Image(__icon_error, __icon_error_len));
		icon_error.instance();
		icon_error->create_from_image(image, Texture::FLAG_VIDEO_SURFACE);
	}

	icon_error->draw(get_canvas_item(), Vector2((size.x - 64) / 2, 20));

	// Error message
	String err_ti = "-- WEBVIEW ERROR --";
	Size2i er_size = font->get_string_size(p_error);
	Size2i ti_size = font->get_string_size(err_ti);

	font->draw(get_canvas_item(), Vector2((size.x - ti_size.x) / 2, 100 + ti_size.y) + Vector2(1, 1), err_ti, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - ti_size.x) / 2, 100 + ti_size.y) + Vector2(-1, 1), err_ti, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - ti_size.x) / 2, 100 + ti_size.y) + Vector2(1, -1), err_ti, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - ti_size.x) / 2, 100 + ti_size.y) + Vector2(-1, -1), err_ti, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - ti_size.x) / 2, 100 + ti_size.y), err_ti, Color(1, 0, 0));

	font->draw(get_canvas_item(), Vector2((size.x - er_size.x) / 2, 100 + er_size.y + 10 + ti_size.y) + Vector2(1, 1), p_error, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - er_size.x) / 2, 100 + er_size.y + 10 + ti_size.y) + Vector2(-1, 1), p_error, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - er_size.x) / 2, 100 + er_size.y + 10 + ti_size.y) + Vector2(1, -1), p_error, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - er_size.x) / 2, 100 + er_size.y + 10 + ti_size.y) + Vector2(-1, -1), p_error, Color(0, 0, 0, 0.5));
	font->draw(get_canvas_item(), Vector2((size.x - er_size.x) / 2, 100 + er_size.y + 10 + ti_size.y), p_error, Color(1, 0, 0));
}
