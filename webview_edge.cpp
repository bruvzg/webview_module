/*************************************************************************/
/*  webview_edge.cpp                                                     */
/*************************************************************************/

#include "webview.h"
#include "core/os/os.h"

#include <shlwapi.h>
#include <Webview2.h>
#include <Objbase.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

typedef HRESULT (WINAPI *CreateCoreWebView2EnvironmentWithOptionsPtr)(PCWSTR p_browser_executable_folder, PCWSTR p_user_data_folder, ICoreWebView2EnvironmentOptions* p_environment_options, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* r_environment_created_handler);
CreateCoreWebView2EnvironmentWithOptionsPtr webview_CreateCoreWebView2EnvironmentWithOptions = nullptr;

class WebViewOverlayDelegate :
	public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
	public ICoreWebView2NavigationStartingEventHandler,
	public ICoreWebView2NavigationCompletedEventHandler,
	public ICoreWebView2NewWindowRequestedEventHandler,
	public ICoreWebView2WebResourceRequestedEventHandler,
	public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
	public ICoreWebView2CapturePreviewCompletedHandler {
public:
	WebViewOverlay *control = nullptr;
	HWND hwnd = nullptr;

	ComPtr<IStream> img_data_stream = nullptr;
	LONG _cRef = 1;

	bool is_loading = false;

	ICoreWebView2Environment *env = nullptr;
	ICoreWebView2 *webview = nullptr;
	ICoreWebView2Controller *controller = nullptr;

	EventRegistrationToken navigation_start_token = {};
	EventRegistrationToken navigation_completed_token = {};
	EventRegistrationToken new_window_token = {};
	EventRegistrationToken resource_requested_token = {};

	ULONG STDMETHODCALLTYPE AddRef() {
		return InterlockedIncrement(&_cRef);
	}

	ULONG STDMETHODCALLTYPE Release() {
		ULONG ulRef = InterlockedDecrement(&_cRef);
		if (0 == ulRef) {
			delete this;
		}
		return ulRef;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface) {
		AddRef();
		*ppvInterface = this;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* p_sender, ICoreWebView2NavigationStartingEventArgs* p_args) {
		if (control != nullptr) {
			control->emit_signal("start_navigation");
		}
		is_loading = true;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* p_sender, ICoreWebView2NavigationCompletedEventArgs* p_args) {
		if (control != nullptr) {
			control->emit_signal("finish_navigation");
		}
		is_loading = false;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* p_sender, ICoreWebView2NewWindowRequestedEventArgs* p_args) {
		LPWSTR uri;
		p_args->get_Uri(&uri);
		ERR_FAIL_COND_V(uri == nullptr, S_OK);
		String result = String(uri);

		control->emit_signal("new_window", result);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* p_sender, ICoreWebView2WebResourceRequestedEventArgs* p_args) {
		ComPtr<ICoreWebView2WebResourceRequest> req;
		p_args->get_Request(&req);

		LPWSTR uri;
		req->get_Uri(&uri);
		String url = String(uri);

		if (url.begins_with("res") || url.begins_with("user")) {
			Error err;
			FileAccess *f = FileAccess::open(url, FileAccess::READ, &err);
			if (err != OK) {
				ComPtr<ICoreWebView2WebResourceResponse> response;
				env->CreateWebResourceResponse(nullptr, 404, L"Not found", L"", &response);
				p_args->put_Response(response.Get());
			} else {
				PoolVector<uint8_t> data;
				data.resize(f->get_len());
				f->get_buffer(data.write().ptr(), f->get_len());

				ComPtr<IStream> data_stream = SHCreateMemStream((const BYTE *)data.read().ptr(), data.size());

				ComPtr<ICoreWebView2WebResourceResponse> response;
				env->CreateWebResourceResponse(data_stream.Get(), 200, L"OK", L"", &response);

				ComPtr<ICoreWebView2HttpResponseHeaders> headers;
				response->get_Headers(&headers);
				headers->AppendHeader(L"Content-Type", L"text/html");
				headers->AppendHeader(L"Content-Length", (LPCWSTR)itos(data.size()).c_str());

				p_args->put_Response(response.Get());
			}
		} else if (url.begins_with("gdscript")) {
			if (control != nullptr) {
				control->emit_signal("callback", url);
			}

			ComPtr<ICoreWebView2WebResourceResponse> response;
			env->CreateWebResourceResponse(nullptr, 200, L"OK", L"", &response);

			ComPtr<ICoreWebView2HttpResponseHeaders> headers;
			response->get_Headers(&headers);
			headers->AppendHeader(L"Content-Type", L"text/html");
			headers->AppendHeader(L"Content-Length", L"0");
			p_args->put_Response(response.Get());
		} else {
			ComPtr<ICoreWebView2WebResourceResponse> response;
			env->CreateWebResourceResponse(nullptr, 404, L"Not found", L"", &response);
			p_args->put_Response(response.Get());
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(HRESULT p_result, ICoreWebView2Environment* p_environment) {
		env = p_environment;
		env->CreateCoreWebView2Controller(hwnd, this);
		LPWSTR version_info;
		env->get_BrowserVersionString(&version_info);
		printf("edge_version: %ws\n", version_info);

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(HRESULT p_result, ICoreWebView2Controller* p_controller) {
		controller = p_controller;
		HRESULT hr = controller->get_CoreWebView2(&webview);
		ERR_FAIL_COND_V(FAILED(hr), S_OK);

		webview->add_NavigationStarting(this, &navigation_start_token);
		webview->add_NavigationCompleted(this, &navigation_completed_token);
		webview->add_NewWindowRequested(this, &new_window_token);

		//custom schemes
		webview->AddWebResourceRequestedFilter(L"res://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
		webview->AddWebResourceRequestedFilter(L"user://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
		webview->AddWebResourceRequestedFilter(L"gdscript://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
		webview->add_WebResourceRequested(this, &resource_requested_token);

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(HRESULT p_error_code) {
		if (img_data_stream != nullptr) {
			STATSTG stats;
			img_data_stream->Stat(&stats, STATFLAG_NONAME);
			ULONG size = stats.cbSize.QuadPart;
			ULONG bytes_read;

			PoolVector<uint8_t> imgdata;
			imgdata.resize(size);
			PoolVector<uint8_t>::Write wr = imgdata.write();
			img_data_stream->Read(wr.ptr(), size, &bytes_read);

			Ref<Image> image;
			image.instance();
			image->load_png_from_buffer(imgdata);

			control->emit_signal("snapshot_ready", image);

			img_data_stream = nullptr;
		}
		return S_OK;
	}

	WebViewOverlayDelegate(WebViewOverlay* p_control, HWND p_hwnd) {
		control = p_control;
		hwnd = p_hwnd;

		String exe_path = OS::get_singleton()->get_executable_path();
		String cache_path = OS::get_singleton()->get_cache_path();

		HRESULT hr = webview_CreateCoreWebView2EnvironmentWithOptions((LPCWSTR)exe_path.c_str(), (LPCWSTR)cache_path.c_str(), nullptr, this);
		ERR_FAIL_COND(FAILED(hr));
	}

	~WebViewOverlayDelegate() {
		if (webview) {
			webview->remove_NavigationCompleted(navigation_completed_token);
			webview->remove_NavigationStarting(navigation_start_token);
			webview->remove_NewWindowRequested(new_window_token);
			webview->remove_WebResourceRequested(resource_requested_token);
		}
	}
};

/*************************************************************************/

class WebViewOverlayImplementation {
public:
	WebViewOverlayDelegate *view = nullptr;
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
				HWND hwnd = (HWND)OS::get_singleton()->get_native_handle(OS::WINDOW_HANDLE);
				if (hwnd != nullptr) {
					float sc = OS::get_singleton()->get_screen_max_scale();
					Rect2i rect = get_window_rect();
					HWND ctrl_wnd = CreateWindowW(L"GodotWebViewControl", nullptr, WS_CHILD | WS_VISIBLE, rect.position.x / sc, rect.position.y / sc, rect.size.width / sc, rect.size.height / sc, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
					WebViewOverlayDelegate *webview = new WebViewOverlayDelegate(this, ctrl_wnd);
					if (user_agent.length() > 0) {
						//TODO - available in unreleased ICoreWebView2ExperimentalSettings only
					}
					//set (no_background) TODO, add WM_PAINT handler with SetBkMode(hdc, TRANSPARENT);

					RECT rc;
					rc.left = rect.position.x / sc;
					rc.top = rect.position.y / sc;
					rc.right = rc.left + rect.size.width / sc;
					rc.bottom = rc.top + rect.size.height / sc;
					webview->controller->put_Bounds(rc);

					data->view->controller->put_ZoomFactor(zoom);
					if (is_visible_in_tree()) {
						ShowWindow(ctrl_wnd, SW_SHOW);
						webview->controller->put_IsVisible(TRUE);
					} else {
						webview->controller->put_IsVisible(FALSE);
						ShowWindow(ctrl_wnd, SW_HIDE);
					}
					data->view->webview->Navigate((LPCWSTR)home_url.c_str());
					data->view = webview;
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
					case 1: {
						_draw_error("Failed to load 'WebView2Loader.dll' library.");
					} break;
					case 2: {
						_draw_error("'CreateCoreWebView2EnvironmentWithOptions' function not found.");
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
		} break;
		case NOTIFICATION_MOVED_IN_PARENT:
		case NOTIFICATION_RESIZED: {
			if (data->view == nullptr) {
				float sc = OS::get_singleton()->get_screen_max_scale();
				Rect2i rect = get_window_rect();
				SetWindowPos(data->view->hwnd, 0, rect.position.x / sc, rect.position.y / sc , rect.size.width / sc, rect.size.height / sc, SWP_NOACTIVATE | SWP_NOZORDER);

				RECT rc;
				rc.left = rect.position.x / sc;
				rc.top = rect.position.y / sc;
				rc.right = rc.left + rect.size.width / sc;
				rc.bottom = rc.top + rect.size.height / sc;
				data->view->controller->put_Bounds(rc);
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (data->view == nullptr) {
				if (is_visible_in_tree()) {
					ShowWindow(data->view->hwnd, SW_SHOW);
					data->view->controller->put_IsVisible(TRUE);
				} else {
					data->view->controller->put_IsVisible(FALSE);
					ShowWindow(data->view->hwnd, SW_HIDE);
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (data->view == nullptr) {
				HWND ctrl_wnd = data->view->hwnd;
				delete data->view;
				DestroyWindow(ctrl_wnd);
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

	if (data->view->img_data_stream == nullptr) {
		data->view->img_data_stream = SHCreateMemStream(nullptr, 0);
		data->view->webview->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, data->view->img_data_stream.Get(), data->view);
	}
}

void WebViewOverlay::set_no_background(bool p_bg) {
	no_background = p_bg;
	//TODO
}

bool WebViewOverlay::get_no_background() const {
	return no_background;
}

void WebViewOverlay::set_url(const String& p_url) {
	home_url = p_url;
	if (data->view == nullptr) {
		data->view->webview->Navigate((LPCWSTR)p_url.c_str());
	}
}

String WebViewOverlay::get_url() const {
	if (data->view == nullptr) {
		LPWSTR uri;
		data->view->webview->get_Source(&uri);
		ERR_FAIL_COND_V(uri == nullptr, "");

		String result = String(uri);
		return result;
	}

	return home_url;
}

void WebViewOverlay::set_user_agent(const String& p_user_agent) {
	user_agent = p_user_agent;
	//TODO
}

String WebViewOverlay::get_user_agent() const {
	//TODO
	return user_agent;
}

double WebViewOverlay::get_zoom_level() const {
	if (data->view == nullptr) {
		double _zoom;
		data->view->controller->get_ZoomFactor(&_zoom);
		return _zoom;
	}
	return zoom;
}

void WebViewOverlay::set_zoom_level(double p_zoom) {
	zoom = p_zoom;
	if (data->view == nullptr) {
		data->view->controller->put_ZoomFactor(zoom);
	}
}

String WebViewOverlay::get_title() const {
	ERR_FAIL_COND_V(data->view == nullptr, "");
	
	LPWSTR title;
	data->view->webview->get_DocumentTitle(&title);
	ERR_FAIL_COND_V(title == nullptr, "");
	String result = String(title);

	return result;
}

void WebViewOverlay::execute_java_script(const String &p_script) {
	ERR_FAIL_COND(data->view == nullptr);

	data->view->webview->ExecuteScript((LPCWSTR)p_script.c_str(), nullptr);
}

void WebViewOverlay::load_string(const String &p_source, const String &p_url) {
	ERR_FAIL_COND(data->view == nullptr);
	data->view->webview->NavigateToString((LPCWSTR)p_source.c_str());
	//TODO any way to set URL ???
}

bool WebViewOverlay::can_go_back() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	BOOL result = false;
	data->view->webview->get_CanGoBack(&result);
	return result;
}

bool WebViewOverlay::can_go_forward() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	BOOL result = false;
	data->view->webview->get_CanGoForward(&result);
	return result;
}

bool WebViewOverlay::is_loading() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	return data->view->is_loading;
}

bool WebViewOverlay::is_secure_content() const {
	ERR_FAIL_COND_V(data->view == nullptr, false);
	//TODO not supported ???
	return false;
}

void WebViewOverlay::go_back() {
	ERR_FAIL_COND(data->view == nullptr);
	data->view->webview->GoBack();
}

void WebViewOverlay::go_forward() {
	ERR_FAIL_COND(data->view == nullptr);
	data->view->webview->GoForward();
}

void WebViewOverlay::reload() {
	ERR_FAIL_COND(data->view == nullptr);
	data->view->webview->Reload();
}

void WebViewOverlay::stop() {
	ERR_FAIL_COND(data->view == nullptr);
	data->view->webview->Stop();
}

void WebViewOverlay::init() {
	HMODULE wv2_lib = LoadLibraryW(L"WebView2Loader.dll");
	if (wv2_lib) {
		webview_CreateCoreWebView2EnvironmentWithOptions = (CreateCoreWebView2EnvironmentWithOptionsPtr)GetProcAddress(wv2_lib, "CreateCoreWebView2EnvironmentWithOptions");
		if (webview_CreateCoreWebView2EnvironmentWithOptions != nullptr) {
			err_status = 0;
		} else {
			err_status = 2;
		}
	} else {
		err_status = 1;
	}
}

void WebViewOverlay::finish() {}