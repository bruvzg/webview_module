/*************************************************************************/
/*  webview_gtk.cpp                                                      */
/*************************************************************************/

#include "webview.h"
#include "core/os/os.h"

#include <dlfcn.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <JavaScriptCore/JavaScript.h>

/*************************************************************************/

typedef GdkDisplay *(*gdk_x11_lookup_xdisplay_ptr)(void *xdisplay);
typedef GdkWindow *(*gdk_x11_window_foreign_new_for_display_ptr)(GdkDisplay *display, void *xwindow);

gdk_x11_lookup_xdisplay_ptr webview_gdk_x11_lookup_xdisplay = nullptr;
gdk_x11_window_foreign_new_for_display_ptr webview_gdk_x11_window_foreign_new_for_display = nullptr;

/*************************************************************************/

class WebViewOverlayImplementation {
public:
	static GtkWidget *container;

	GtkWidget *view = nullptr;
	WebViewOverlay *control = nullptr;
	bool secure = false;

	static gboolean msg_callback(WebKitWebView *p_web_view, WebKitUserMessage *p_message, gpointer p_user_data) {
		WebViewOverlayImplementation *impl = (WebViewOverlayImplementation *)p_user_data;
		if (impl->control != nullptr) {
			impl->control->emit_signal("callback", String::utf8(webkit_user_message_get_name(p_message)));
		}
		return TRUE;
	}

	static void load_callback(WebKitWebView *p_web_view, WebKitLoadEvent p_load_event, gpointer p_user_data) {
		WebViewOverlayImplementation *impl = (WebViewOverlayImplementation *)p_user_data;
		if (impl->control != nullptr) {
			switch (p_load_event) {
				case WEBKIT_LOAD_STARTED: {
					impl->secure = true;
					if (impl->control != nullptr) {
						impl->control->emit_signal("start_navigation");
					}
				} break;
				case WEBKIT_LOAD_REDIRECTED: {
					//TODO
				} break;
				case WEBKIT_LOAD_COMMITTED: {
					//TODO
				} break;
				case WEBKIT_LOAD_FINISHED: {
					if (impl->control != nullptr) {
						impl->control->emit_signal("finish_navigation");
					}
					break;
				}
			}
		}
	}

	static void insec_callback(WebKitWebView *p_web_view, WebKitInsecureContentEvent p_event, gpointer p_user_data) {
		WebViewOverlayImplementation *impl = (WebViewOverlayImplementation *)p_user_data;
		impl->secure = false;
	}

	static void snapshot_callback(GObject *p_source_object, GAsyncResult *p_res, gpointer p_user_data) {
		WebViewOverlayImplementation *impl = (WebViewOverlayImplementation *)p_user_data;

		GError *error;
		cairo_surface_t *surf = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(impl->view), p_res, &error);
		const unsigned char *data = cairo_image_surface_get_data(surf);
		int w = cairo_image_surface_get_width(surf);
		int h = cairo_image_surface_get_height(surf);
		cairo_format_t format = cairo_image_surface_get_format(surf);
		int color_size = 0;
		switch (format) {
			case CAIRO_FORMAT_RGB24:
			case CAIRO_FORMAT_ARGB32: {
				color_size = 8;
			} break;
			default: {
				cairo_surface_destroy(surf);
				ERR_FAIL_MSG("unsupported pixel format: " + itos(format) + ".");
			} break;
		}

		PoolVector<uint8_t> imgdata;
		imgdata.resize(w * h * color_size);
		PoolVector<uint8_t>::Write wr = imgdata.write();

		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int ofs = (y * w + x) * color_size;
				switch (format) {
					case CAIRO_FORMAT_RGB24: {
						wr[ofs + 0] = data[ofs + 1];
						wr[ofs + 1] = data[ofs + 2];
						wr[ofs + 2] = data[ofs + 3];
						wr[ofs + 3] = 255;
					} break;
					case CAIRO_FORMAT_ARGB32: {
						wr[ofs + 0] = data[ofs + 1];
						wr[ofs + 1] = data[ofs + 2];
						wr[ofs + 2] = data[ofs + 3];
						wr[ofs + 3] = data[ofs + 0];
					} break;
					default: {
						cairo_surface_destroy(surf);
						ERR_FAIL_MSG("unsupported pixel format: " + itos(format) + ".");
					} break;
				}
			}
		}

		impl->control->emit_signal("snapshot_ready", memnew(Image(w, h, false, Image::FORMAT_RGBA8, imgdata)));

		cairo_surface_destroy(surf);
	}
};

GtkWidget *WebViewOverlayImplementation::container = nullptr;

/*************************************************************************/

WebViewOverlay::WebViewOverlay() {
	data = memnew(WebViewOverlayImplementation());
}

WebViewOverlay::~WebViewOverlay() {
	memdelete(data);
}

void WebViewOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (!Engine::get_singleton()->is_editor_hint() && (err_status == 0)) {
				set_process_internal(true); // Wait for window to init, do not init in editor.
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!Engine::get_singleton()->is_editor_hint() && (data->view == nullptr) && (err_status == 0)) {
					if (data->container == nullptr) {
						void *xwindow = OS::get_singleton()->get_native_handle(OS::WINDOW_HANDLE);
						void *xdisplay = OS::get_singleton()->get_native_handle(OS::DISPLAY_HANDLE);

						if (xwindow == nullptr || xdisplay == nullptr) {
							return;
						}

						int argc = 0;
						gtk_init(&argc, nullptr);

						GdkWindow *window = webview_gdk_x11_window_foreign_new_for_display(webview_gdk_x11_lookup_xdisplay(xdisplay), xwindow);
						data->container = gtk_fixed_new();
						gtk_widget_set_window(GTK_WIDGET(data->container), window);
						gtk_widget_set_visible(GTK_WIDGET(data->container), TRUE);
					}

					GtkWidget *webview = webkit_web_view_new();
					WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(webview));
					WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview));

					//TODO custom schemes

					webkit_user_content_manager_add_script(manager, webkit_user_script_new("function webviewMessage(s){window.webkit.messageHandlers.callback.postMessage(s);}", WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr));
					webkit_user_content_manager_register_script_message_handler(manager, "callback");
					g_signal_connect_data(G_OBJECT(webview), "user-message-received", G_CALLBACK(data->msg_callback), data, nullptr, (GConnectFlags)0);
					g_signal_connect_data(G_OBJECT(webview), "load-changed", G_CALLBACK(data->load_callback), data, nullptr, (GConnectFlags)0);
					g_signal_connect_data(G_OBJECT(webview), "insecure-content-detected", G_CALLBACK(data->insec_callback), data, nullptr, (GConnectFlags)0);

					float sc = OS::get_singleton()->get_screen_max_scale();
					Rect2i rect = get_window_rect();
					gtk_widget_set_size_request(GTK_WIDGET(webview), rect.size.width / sc, rect.size.height / sc);
					gtk_fixed_put(GTK_FIXED(data->container), GTK_WIDGET(webview), rect.position.x / sc, rect.position.y / sc);

					//TODO useragent

					GdkRGBA rgba;
					if (no_background) {
						gdk_rgba_parse(&rgba, "rgba(0,0,0,0)");
					} else {
						gdk_rgba_parse(&rgba, "rgba(1,1,1,1)");
					}
					webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(webview), &rgba);

					if (is_visible_in_tree()) {
						gtk_widget_set_visible(GTK_WIDGET(webview), TRUE);
					} else {
						gtk_widget_set_visible(GTK_WIDGET(webview), FALSE);
					}

					webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(webview), zoom);
					webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview), home_url.utf8().get_data());

					data->control = this;
					data->view = webview;
					ctrl_err_status = 0;

					set_process_internal(false);
			}
			if (!Engine::get_singleton()->is_editor_hint() && (data->view != nullptr) && (err_status == 0)) {
				gtk_main_iteration_do(false);
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (err_status != 0) {
				switch (err_status) {
					case -1: {
						_draw_error("WebView interface no initialized.");
					} break;
					case 1: {
						_draw_error("Failed to load 'libgdk-3.so' library.");
					} break;
					case 2: {
						_draw_error("'libgdk-3.so' functions not found.");
					} break;
					case 3: {
						_draw_error("Failed to load 'libgtk-3.so' library.");
					} break;
					case 4: {
						_draw_error("'libgtk-3.so' functions not found.");
					} break;
					case 5: {
						_draw_error("Failed to load 'libcairo.so.2' library.");
					} break;
					case 6: {
						_draw_error("'libcairo.so.2' functions not found.");
					} break;
					case 7: {
						_draw_error("Failed to load 'libgobject-2.0.so' library.");
					} break;
					case 8: {
						_draw_error("'libgobject-2.0.so' functions not found.");
					} break;
					case 9: {
						_draw_error("Failed to load 'libwebkit2gtk-4.0.so' library.");
					} break;
					case 10: {
						_draw_error("'libwebkit2gtk-4.0.so' functions not found.");
					} break;
					default: {
						_draw_error("Unknown error.");
					} break;
				};
			} else if (ctrl_err_status > 0) {
				_draw_error("Unknown control error.");
			} else if (Engine::get_singleton()->is_editor_hint()) {
				_draw_placeholder();
			}
		} FALLTHROUGH;
		case NOTIFICATION_MOVED_IN_PARENT:
		case NOTIFICATION_RESIZED: {
			if (data->view != nullptr) {
				float sc = OS::get_singleton()->get_screen_max_scale();
				Rect2i rect = get_window_rect();
				gtk_widget_set_size_request(GTK_WIDGET(data->view), rect.size.width / sc, rect.size.height / sc);
				gtk_fixed_move(GTK_FIXED(data->container), GTK_WIDGET(data->view), rect.position.x / sc, rect.position.y / sc);
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (data->view != nullptr) {
				if (is_visible_in_tree()) {
					gtk_widget_set_visible(GTK_WIDGET(data->view), TRUE);
				} else {
					gtk_widget_set_visible(GTK_WIDGET(data->view), FALSE);
				}
		}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (data->view != nullptr) {
				gtk_widget_destroy(data->view);
				data->view = nullptr;
			}
		} break;
		default: {
			//NOP
		} break;
	}
}

void WebViewOverlay::get_snapshot(int p_width) {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_get_snapshot(WEBKIT_WEB_VIEW(data->view), WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_TRANSPARENT_BACKGROUND, nullptr, data->snapshot_callback, data);
}

void WebViewOverlay::set_no_background(bool p_bg) {
	no_background = p_bg;
	if (data->view != nullptr) {
		GdkRGBA rgba;
		if (no_background) {
			gdk_rgba_parse(&rgba, "rgba(0,0,0,0)");
		} else {
			gdk_rgba_parse(&rgba, "rgba(1,1,1,1)");
		}
		webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(data->view), &rgba);
	}
}

bool WebViewOverlay::get_no_background() const {
	return no_background;
}

void WebViewOverlay::set_url(const String& p_url) {
	home_url = p_url;
	if (data->view != nullptr) {
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(data->view), home_url.utf8().get_data());
	}
}

String WebViewOverlay::get_url() const {
	if (data->view != nullptr) {
		return String::utf8(webkit_web_view_get_uri(WEBKIT_WEB_VIEW(data->view)));
	}
	return home_url;
}

void WebViewOverlay::set_user_agent(const String& p_user_agent) {
	user_agent = p_user_agent;
	//TODO, is it supported ?
}

String WebViewOverlay::get_user_agent() const {
	//TODO
	return user_agent;
}

double WebViewOverlay::get_zoom_level() const {
	if (data->view != nullptr) {
		webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(data->view));
	}
	return zoom;
}

void WebViewOverlay::set_zoom_level(double p_zoom) {
	zoom = p_zoom;
	if (data->view != nullptr) {
		webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(data->view), zoom);
	}
}

String WebViewOverlay::get_title() const {
	ERR_FAIL_COND_V(data->view == nullptr, "");
	return String::utf8(webkit_web_view_get_title(WEBKIT_WEB_VIEW(data->view)));
}

void WebViewOverlay::execute_java_script(const String &p_script) {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(data->view), p_script.utf8().get_data(), nullptr, nullptr, nullptr);
}

void WebViewOverlay::load_string(const String &p_source) {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_load_html(WEBKIT_WEB_VIEW(data->view), p_source.utf8().get_data(), "");
}

bool WebViewOverlay::can_go_back() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	return webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(data->view));
}

bool WebViewOverlay::can_go_forward() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	return webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(data->view));
}

bool WebViewOverlay::is_ready() const {
	return (data->view != nullptr);
}

bool WebViewOverlay::is_loading() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	return webkit_web_view_is_loading(WEBKIT_WEB_VIEW(data->view));
}

bool WebViewOverlay::is_secure_content() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	return data->secure;
}

void WebViewOverlay::go_back() {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_go_back(WEBKIT_WEB_VIEW(data->view));
}

void WebViewOverlay::go_forward() {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_go_forward(WEBKIT_WEB_VIEW(data->view));
}

void WebViewOverlay::reload() {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_reload(WEBKIT_WEB_VIEW(data->view));
}

void WebViewOverlay::stop() {
	ERR_FAIL_COND(data->view == nullptr);
	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(data->view));
}

void WebViewOverlay::init() {
	void *gdk3_lib = dlopen("libgdk-3.so", RTLD_LAZY);
	if (gdk3_lib == nullptr) {
		err_status = 1;
		return;
	}
	webview_gdk_x11_lookup_xdisplay = (gdk_x11_lookup_xdisplay_ptr)dlsym(gdk3_lib, "gdk_x11_lookup_xdisplay");
	webview_gdk_x11_window_foreign_new_for_display = (gdk_x11_window_foreign_new_for_display_ptr)dlsym(gdk3_lib, "gdk_x11_window_foreign_new_for_display");
	//webview_gdk_rgba_parse = (gdk_rgba_parse_ptr)dlsym(gdk3_lib, "gdk_rgba_parse");
	if (webview_gdk_x11_lookup_xdisplay == nullptr || webview_gdk_x11_window_foreign_new_for_display == nullptr /* || webview_gdk_rgba_parse == nullptr*/) {
		err_status = 2;
		return;
	}

	void *gtk3_lib = dlopen("libgtk-3.so", RTLD_LAZY);
	if (gtk3_lib == nullptr) {
		err_status = 3;
		return;
	}
	/*
	gtk_init
	gtk_widget_set_window
	gtk_fixed_new
	gtk_container_add
	gtk_widget_set_size_request
	gtk_fixed_put
	gtk_fixed_move
	gtk_widget_set_visible
	gtk_main_iteration_do
	*/

	void *cairo_lib = dlopen("libcairo.so.2", RTLD_LAZY);
	if (cairo_lib == nullptr) {
		err_status = 5;
		return;
	}

	/*
	cairo_image_surface_get_data
	cairo_image_surface_get_width
	cairo_image_surface_get_height
	cairo_image_surface_get_format
	cairo_surface_destroy
	*/

	void *gobj_lib = dlopen("libgobject-2.0.so", RTLD_LAZY);
	if (gobj_lib == nullptr) {
		err_status = 7;
		return;
	}

	/*
	g_signal_connect_data
	*/

	void *wk_lib = dlopen("libwebkit2gtk-4.0.so", RTLD_LAZY);
	if (wk_lib == nullptr) {
		err_status = 9;
		return;
	}

	/*
	webkit_web_view_new
	webkit_web_view_get_user_content_manager
	webkit_user_content_manager_register_script_message_handler
	webkit_user_content_manager_add_script
	webkit_user_script_new
	webkit_web_view_get_settings
	webkit_web_view_load_html
	webkit_web_view_load_uri
	webkit_web_view_can_go_back
	webkit_web_view_go_back
	webkit_web_view_can_go_forward
	webkit_web_view_go_forward
	webkit_web_view_get_title
	webkit_web_view_reload
	webkit_web_view_stop_loading
	webkit_web_view_get_uri
	webkit_web_view_get_zoom_level
	webkit_web_view_set_zoom_level
	webkit_web_view_run_javascript
	webkit_web_view_get_snapshot
	webkit_web_view_get_snapshot_finish
	webkit_web_view_set_background_color
	webkit_web_view_is_loading
	webkit_user_message_get_name
	*/

	err_status = 0;
}

void WebViewOverlay::finish() {}