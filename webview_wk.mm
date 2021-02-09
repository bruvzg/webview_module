/*************************************************************************/
/*  webview_wk.mm                                                        */
/*************************************************************************/

#include "webview.h"
#include "core/os/os.h"

#include <WebKit/WebKit.h>
#include <WebKit/WKNavigationDelegate.h>
#include <Foundation/NSURLError.h>

/*************************************************************************/

@interface GDWKNavigationDelegate: NSObject <WKNavigationDelegate> {
	WebViewOverlay *control;
}
- (void)setControl:(WebViewOverlay *)p_control;
@end

@implementation GDWKNavigationDelegate

- (void)setControl:(WebViewOverlay *)p_control {
	control = p_control;
}

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation {
	if (control != nullptr) {
		control->emit_signal("start_navigation");
	}
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
	if (control != nullptr) {
		control->emit_signal("finish_navigation");
	}
}

@end

/*************************************************************************/

@interface GDWKURLSchemeHandler: NSObject <WKURLSchemeHandler> {
	WebViewOverlay *control;
}
- (void)setControl:(WebViewOverlay *)p_control;
@end

@implementation GDWKURLSchemeHandler

- (void)setControl:(WebViewOverlay *)p_control {
	control = p_control;
}

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
	NSURL *requestURL = urlSchemeTask.request.URL;
	NSString *ns = [requestURL absoluteString];
	String url = String::utf8([ns UTF8String]);

	if (url.begins_with("res") || url.begins_with("user")) {
		Error err;
		FileAccess *f = FileAccess::open(url, FileAccess::READ, &err);
		if (err != OK) {
			[urlSchemeTask didReceiveResponse:[[NSHTTPURLResponse alloc] initWithURL:requestURL statusCode:404 HTTPVersion:@"HTTP/1.1" headerFields:nil]];
			[urlSchemeTask didFinish];
		} else {
			PoolVector<uint8_t> data;
			data.resize(f->get_len());
			f->get_buffer(data.write().ptr(), f->get_len());

			[urlSchemeTask didReceiveResponse:[[NSURLResponse alloc] initWithURL:requestURL MIMEType:@"text/html" expectedContentLength:(NSInteger)data.size() textEncodingName:nil]];
			[urlSchemeTask didReceiveData:[[NSData alloc] initWithBytes:(const void *)data.read().ptr() length:(NSUInteger)data.size()]];
			[urlSchemeTask didFinish];
		}
	} else if (url.begins_with("gdscript")) {
		if (control != nullptr) {
			control->emit_signal("callback", url);
		}

		[urlSchemeTask didReceiveResponse:[[NSURLResponse alloc] initWithURL:requestURL MIMEType:@"text/html" expectedContentLength:(NSInteger)0 textEncodingName:nil]];
		[urlSchemeTask didFinish];
	} else {
		[urlSchemeTask didReceiveResponse:[[NSHTTPURLResponse alloc] initWithURL:requestURL statusCode:404 HTTPVersion:@"HTTP/1.1" headerFields:nil]];
		[urlSchemeTask didFinish];
	}
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
	[urlSchemeTask didFinish];
}

@end

/*************************************************************************/

void WebViewOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process_internal(true); // Wait for window to init, do not init in editor.
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!Engine::get_singleton()->is_editor_hint() && (native_view == nullptr)) {
#ifdef OSX_ENABLED
				NSView *main_view = [[[NSApplication sharedApplication] mainWindow] contentView];
#else
				UIWindow *main_view = [[[UIApplication sharedApplication] windows] firstObject];
#endif
				if (main_view != nullptr) {
					GDWKURLSchemeHandler *sch_handle = [[GDWKURLSchemeHandler alloc] init];
					[sch_handle setControl:this];

					GDWKNavigationDelegate *nav_handle = [[GDWKNavigationDelegate alloc] init];
					[nav_handle setControl:this];

					WKWebViewConfiguration* webViewConfig = [[WKWebViewConfiguration alloc] init];
					[webViewConfig setURLSchemeHandler:sch_handle forURLScheme:@"res"];
					[webViewConfig setURLSchemeHandler:sch_handle forURLScheme:@"user"];
					[webViewConfig setURLSchemeHandler:sch_handle forURLScheme:@"gdscript"];

					float sc = OS::get_singleton()->get_screen_max_scale();
					Rect2i rect = get_window_rect();
					float wh = OS::get_singleton()->get_window_size().y;
					WKWebView* m_webView = [[WKWebView alloc] initWithFrame:NSMakeRect(rect.position.x / sc, wh - rect.position.y / sc - rect.size.height / sc, rect.size.width / sc, rect.size.height / sc) configuration:webViewConfig];

					[m_webView setNavigationDelegate:nav_handle];
					if (user_agent.length() > 0) {
						[m_webView setCustomUserAgent:[NSString stringWithUTF8String:user_agent.utf8().get_data()]];
					} else {
						[m_webView setCustomUserAgent:nil];
					}
					[m_webView setValue:((no_background) ? @(NO) : @(YES)) forKey:@"drawsBackground"];
					[m_webView setPageZoom:zoom];

					[m_webView setHidden:!is_visible_in_tree()];
					[main_view addSubview:m_webView];
					[m_webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:home_url.utf8().get_data()]]]];

					native_view = m_webView;
					set_process_internal(false);
				}
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (Engine::get_singleton()->is_editor_hint()) {
				Size2i size = get_size();
				draw_rect(Rect2(Point2(), size), Color(1, 1, 1), false);
				draw_string(get_font("font", "Label"), Point2(10, 10), home_url, Color(1, 1, 1));
			}
		} FALLTHROUGH;
		case NOTIFICATION_MOVED_IN_PARENT:
		case NOTIFICATION_RESIZED: {
			WKWebView* m_webView = (WKWebView* )native_view;
			if (m_webView != nullptr) {
				float sc = OS::get_singleton()->get_screen_max_scale();
				Rect2i rect = get_window_rect();
				float wh = OS::get_singleton()->get_window_size().y;
				[m_webView setFrame:NSMakeRect(rect.position.x / sc, wh - rect.position.y / sc - rect.size.height / sc, rect.size.width / sc, rect.size.height / sc)];
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			WKWebView* m_webView = (WKWebView* )native_view;
			if (m_webView != nullptr) {
				[m_webView setHidden:!is_visible_in_tree()];
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			WKWebView* m_webView = (WKWebView* )native_view;
			if (m_webView != nullptr) {
				[m_webView removeFromSuperview];
				[m_webView release];
				native_view = nullptr;
			}
		} break;
		default: {
			//NOP
		} break;
	}
}

void WebViewOverlay::set_no_background(bool p_bg) {
	no_background = p_bg;
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		[m_webView setValue:((no_background) ? @(NO) : @(YES)) forKey:@"drawsBackground"];
	}
}

bool WebViewOverlay::get_no_background() const {
	return no_background;
}

void WebViewOverlay::set_url(const String& p_url) {
	home_url = p_url;
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		[m_webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:home_url.utf8().get_data()]]]];
	}
}

String WebViewOverlay::get_url() const {
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		NSString *ns = [[m_webView URL] absoluteString];
		return String::utf8([ns UTF8String]);
	}
	return home_url;
}

void WebViewOverlay::set_user_agent(const String& p_user_agent) {
	user_agent = p_user_agent;
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		if (user_agent.length() > 0) {
			[m_webView setCustomUserAgent:[NSString stringWithUTF8String:user_agent.utf8().get_data()]];
		} else {
			[m_webView setCustomUserAgent:nil];
		}
	}
}

String WebViewOverlay::get_user_agent() const {
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		NSString *ns = [m_webView customUserAgent];
		return String::utf8([ns UTF8String]);
	}
	return user_agent;
}

double WebViewOverlay::get_zoom_level() const {
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		return [m_webView pageZoom];
	}
	return zoom;
}

void WebViewOverlay::set_zoom_level(double p_zoom) {
	zoom = p_zoom;
	WKWebView* m_webView = (WKWebView* )native_view;
	if (m_webView != nullptr) {
		[m_webView setPageZoom:zoom];
	}
}

String WebViewOverlay::get_title() const {
	ERR_FAIL_COND_V(native_view == nullptr, "");
	WKWebView* m_webView = (WKWebView* )native_view;
	NSString *ns = [m_webView title];
	return String::utf8([ns UTF8String]);
}

void WebViewOverlay::execute_java_script(const String &p_script) {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView evaluateJavaScript:[NSString stringWithUTF8String:p_script.utf8().get_data()] completionHandler:nil];
}

void WebViewOverlay::load_string(const String &p_source, const String &p_url) {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView loadHTMLString:[NSString stringWithUTF8String:p_source.utf8().get_data()] baseURL:[NSURL URLWithString:[NSString stringWithUTF8String:p_url.utf8().get_data()]]];
}

bool WebViewOverlay::can_go_back() const {
	ERR_FAIL_COND_V(native_view == nullptr, false);
	WKWebView* m_webView = (WKWebView* )native_view;
	return [m_webView canGoBack];
}

bool WebViewOverlay::can_go_forward() const {
	ERR_FAIL_COND_V(native_view == nullptr, false);
	WKWebView* m_webView = (WKWebView* )native_view;
	return [m_webView canGoForward];
}

bool WebViewOverlay::is_loading() const {
	ERR_FAIL_COND_V(native_view == nullptr, false);
	WKWebView* m_webView = (WKWebView* )native_view;
	return [m_webView loading];
}

bool  WebViewOverlay::is_secure_content() const {
	ERR_FAIL_COND_V(native_view == nullptr, false);
	WKWebView* m_webView = (WKWebView* )native_view;
	return [m_webView hasOnlySecureContent];
}

void WebViewOverlay::go_back() {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView goBack];
}

void WebViewOverlay::go_forward() {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView goForward];
}

void WebViewOverlay::reload() {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView reload];
}

void WebViewOverlay::stop() {
	ERR_FAIL_COND(native_view == nullptr);
	WKWebView* m_webView = (WKWebView* )native_view;
	[m_webView stopLoading];
}
