/*************************************************************************/
/*  webview_wk.mm                                                        */
/*************************************************************************/

#include "webview.h"
#include "core/os/os.h"

#include <WebKit/WebKit.h>
#include <WebKit/WKNavigationDelegate.h>
#include <Foundation/NSURLError.h>

#if defined(IPHONE_ENABLED)
#include "platform/iphone/app_delegate.h"
#include "platform/iphone/godot_view.h"
#include "platform/iphone/view_controller.h"
#endif

/*************************************************************************/

@interface GDWKNavigationDelegate: NSObject <WKNavigationDelegate, WKUIDelegate> {
	WebViewOverlay *control;
}
- (void)setControl:(WebViewOverlay *)p_control;
@end

@implementation GDWKNavigationDelegate

- (WKWebView *)webView:(WKWebView *)webView createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration forNavigationAction:(WKNavigationAction *)navigationAction windowFeatures:(WKWindowFeatures *)windowFeatures {
	if (control != nullptr) {
		NSURL *requestURL = navigationAction.request.URL;
		NSString *ns = [requestURL absoluteString];
		String url = String::utf8([ns UTF8String]);
		control->emit_signal("new_window", url);
	}
	return nil;
}

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

class WebViewOverlayImplementation {
public:
	WKWebView* view = nullptr;
};

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
#if defined(OSX_ENABLED)
				NSView *main_view = [[[NSApplication sharedApplication] mainWindow] contentView];
#elif defined(IPHONE_ENABLED)
				UIView *main_view = AppDelegate.viewController.godotView;
#else
				#error Unsupported platform!
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
					float wh = OS::get_singleton()->get_window_size().y / sc;
					WKWebView* m_webView = [[WKWebView alloc] initWithFrame:CGRectMake(rect.position.x / sc, wh - rect.position.y / sc - rect.size.height / sc, rect.size.width / sc, rect.size.height / sc) configuration:webViewConfig];

					[m_webView setNavigationDelegate:nav_handle];
					[m_webView setUIDelegate:nav_handle];
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

					data->view = m_webView;
					ctrl_err_status = 0;

					set_process_internal(false);
				}
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (err_status != 0) {
				switch (err_status) {
					case -1: {
						_draw_error("WebView interface no initialized.");
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
				float wh = OS::get_singleton()->get_window_size().y / sc;
				[data->view setFrame:CGRectMake(rect.position.x / sc, wh - rect.position.y / sc - rect.size.height / sc, rect.size.width / sc, rect.size.height / sc)];
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (data->view != nullptr) {
				[data->view setHidden:!is_visible_in_tree()];
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			
			if (data->view != nullptr) {
				[data->view removeFromSuperview];
				data->view = nullptr;
			}
		} break;
		default: {
			//NOP
		} break;
	}
}

void WebViewOverlay::get_snapshot(int p_width) {
	
	WKSnapshotConfiguration *wkSnapshotConfig = [[WKSnapshotConfiguration alloc] init];
	wkSnapshotConfig.snapshotWidth = [NSNumber numberWithInt:p_width];
	wkSnapshotConfig.afterScreenUpdates = NO;


	[data->view takeSnapshotWithConfiguration:wkSnapshotConfig
#if defined(OSX_ENABLED)
		completionHandler:^(NSImage * _Nullable image, NSError * _Nullable error) {
#elif defined(IPHONE_ENABLED)
		completionHandler:^(UIImage * _Nullable image, NSError * _Nullable error) {
#else
		#error Unsupported platform!
#endif
		if (image != nullptr) {
			CGImageRef imageRef = [image CGImage];
			NSUInteger width = CGImageGetWidth(imageRef);
			NSUInteger height = CGImageGetHeight(imageRef);

			PoolVector<uint8_t> imgdata;
			imgdata.resize(width * height * 4);
			PoolVector<uint8_t>::Write wr = imgdata.write();

			NSUInteger bytesPerPixel = 4;
			NSUInteger bytesPerRow = bytesPerPixel * width;
			NSUInteger bitsPerComponent = 8;
			CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
			CGContextRef context = CGBitmapContextCreate(wr.ptr(), width, height, bitsPerComponent, bytesPerRow, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
			CGColorSpaceRelease(colorSpace);

			CGContextDrawImage(context, CGRectMake(0, 0, width, height), imageRef);
			CGContextRelease(context);

			emit_signal("snapshot_ready", memnew(Image(width, height, false, Image::FORMAT_RGBA8, imgdata)));
		}
	}];
}

void WebViewOverlay::set_no_background(bool p_bg) {
	no_background = p_bg;
	
	if (data->view != nullptr) {
		[data->view setValue:((no_background) ? @(NO) : @(YES)) forKey:@"drawsBackground"];
	}
}

bool WebViewOverlay::get_no_background() const {
	return no_background;
}

void WebViewOverlay::set_url(const String& p_url) {
	home_url = p_url;
	
	if (data->view != nullptr) {
		[data->view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:home_url.utf8().get_data()]]]];
	}
}

String WebViewOverlay::get_url() const {
	if (data->view != nullptr) {
		NSString *ns = [[data->view URL] absoluteString];
		return String::utf8([ns UTF8String]);
	}
	return home_url;
}

void WebViewOverlay::set_user_agent(const String& p_user_agent) {
	user_agent = p_user_agent;
	
	if (data->view != nullptr) {
		if (user_agent.length() > 0) {
			[data->view setCustomUserAgent:[NSString stringWithUTF8String:user_agent.utf8().get_data()]];
		} else {
			[data->view setCustomUserAgent:nil];
		}
	}
}

String WebViewOverlay::get_user_agent() const {
	if (data->view != nullptr) {
		NSString *ns = [data->view customUserAgent];
		return String::utf8([ns UTF8String]);
	}
	return user_agent;
}

double WebViewOverlay::get_zoom_level() const {
	if (data->view != nullptr) {
		return [data->view pageZoom];
	}
	return zoom;
}

void WebViewOverlay::set_zoom_level(double p_zoom) {
	zoom = p_zoom;

	if (data->view != nullptr) {
		[data->view setPageZoom:zoom];
	}
}

String WebViewOverlay::get_title() const {
	ERR_FAIL_COND_V(data->view == nullptr, "");

	NSString *ns = [data->view title];
	return String::utf8([ns UTF8String]);
}

void WebViewOverlay::execute_java_script(const String &p_script) {
	ERR_FAIL_COND(data->view == nullptr);
	[data->view evaluateJavaScript:[NSString stringWithUTF8String:p_script.utf8().get_data()] completionHandler:nil];
}

void WebViewOverlay::load_string(const String &p_source, const String &p_url) {
	ERR_FAIL_COND(data->view == nullptr);
	[data->view loadHTMLString:[NSString stringWithUTF8String:p_source.utf8().get_data()] baseURL:[NSURL URLWithString:[NSString stringWithUTF8String:p_url.utf8().get_data()]]];
}

bool WebViewOverlay::can_go_back() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);

	return [data->view canGoBack];
}

bool WebViewOverlay::can_go_forward() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);

	return [data->view canGoForward];
}

bool WebViewOverlay::is_loading() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);

	return [data->view isLoading];
}

bool WebViewOverlay::is_secure_content() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);

	return [data->view hasOnlySecureContent];
}

void WebViewOverlay::go_back() {
	ERR_FAIL_COND(data->view == nullptr);

	[data->view goBack];
}

void WebViewOverlay::go_forward() {
	ERR_FAIL_COND(data->view == nullptr);

	[data->view goForward];
}

void WebViewOverlay::reload() {
	ERR_FAIL_COND(data->view == nullptr);

	[data->view reload];
}

void WebViewOverlay::stop() {
	ERR_FAIL_COND(data->view == nullptr);

	[data->view stopLoading];
}

void WebViewOverlay::init() {
	err_status = 0;
}

void WebViewOverlay::finish() {
	//NOP
}