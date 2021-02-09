/*************************************************************************/
/*  webview.h                                                            */
/*************************************************************************/

#ifndef WEB_VIEW_H
#define WEB_VIEW_H

#include "scene/gui/control.h"

/*************************************************************************/

class WebViewOverlay : public Control {
	GDCLASS(WebViewOverlay, Control);

	void *native_view = nullptr;
	void *native_view_controller = nullptr;

	String home_url;
	String user_agent;
	double zoom = 1.0f;
	bool no_background = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:

	void set_no_background(bool p_bg);
	bool get_no_background() const;

	void set_url(const String& p_url);
	String get_url() const;

	void set_user_agent(const String& p_user_agent);
	String get_user_agent() const;

	double get_zoom_level() const;
	void set_zoom_level(double p_zoom);

	String get_title() const;

	void load_string(const String &p_source, const String &p_url);
	void execute_java_script(const String &p_script);

	void get_snapshot(int p_width);

	bool can_go_back() const;
	bool can_go_forward() const;
	bool is_loading() const;
	bool is_secure_content() const;
	void go_back();
	void go_forward();
	void reload();
	void stop();

};

#endif // WEB_VIEW_H
