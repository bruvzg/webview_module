/*************************************************************************/
/*  webview.cpp                                                          */
/*************************************************************************/

#include "webview.h"

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

	ClassDB::bind_method(D_METHOD("get_title"), &WebViewOverlay::get_title);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "no_background"), "set_no_background", "get_no_background");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "url"), "set_url", "get_url");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "user_agent"), "set_user_agent", "get_user_agent");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "zoom_level"), "set_zoom_level", "get_zoom_level");

	ADD_SIGNAL(MethodInfo("callback", PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo("start_navigation"));
	ADD_SIGNAL(MethodInfo("finish_navigation"));
}

