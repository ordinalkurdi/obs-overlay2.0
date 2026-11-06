// LiveBackend: the real OBS, through libobs and obs-frontend-api.
#include "backend.hpp"
#include "audio_math.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <QMetaObject>
#include <QObject>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <set>
#include <unordered_map>

namespace ovl {

namespace {

// Audio sources that are really live in a scene: visible items only, walking into nested scenes and groups.
struct LiveCtx {
	std::set<std::string> *names;
	int depth;
};

void collect_live_audio(obs_scene_t *scene, int depth, std::set<std::string> &names);

bool live_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<LiveCtx *>(param);
	if (!obs_sceneitem_visible(item)) return true;
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src) return true;
	if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) c->names->insert(obs_source_get_name(src));
	if (obs_scene_t *inner = obs_scene_from_source(src)) collect_live_audio(inner, c->depth + 1, *c->names); // nested scene or group
	return true;
}

void collect_live_audio(obs_scene_t *scene, int depth, std::set<std::string> &names)
{
	if (depth > 8) return; // guards against scenes that contain each other
	LiveCtx c{&names, depth};
	obs_scene_enum_items(scene, live_item, &c);
}

struct ItemCollector {
	std::vector<ItemInfo> items;
};

bool collect_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<ItemCollector *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	ItemInfo i;
	i.id = obs_sceneitem_get_id(item);
	i.name = src ? obs_source_get_name(src) : "";
	i.enabled = obs_sceneitem_visible(item);
	i.locked = obs_sceneitem_locked(item);
	i.group = obs_sceneitem_is_group(item);
	c->items.push_back(std::move(i));
	return true;
}

struct InputCollector {
	std::vector<InputInfo> inputs;
};

bool collect_input(void *param, obs_source_t *src)
{
	auto *c = static_cast<InputCollector *>(param);
	if (!(obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)) return true;
	InputInfo i;
	i.name = obs_source_get_name(src);
	const char *id = obs_source_get_id(src);
	i.kind = id ? id : "";
	i.muted = obs_source_muted(src);
	i.db = audio::mul_to_db(obs_source_get_volume(src));
	c->inputs.push_back(std::move(i));
	return true;
}

} // namespace

class LiveBackend final : public Backend {
public:
	LiveBackend()
	{
		poster_ = new QObject; // context for queued preview frames; deleting it drops frames still in flight
		cpu_ = os_cpu_usage_info_start();
		obs_frontend_add_event_callback(&LiveBackend::on_frontend_event, this);
	}

	~LiveBackend() override
	{
		obs_frontend_remove_event_callback(&LiveBackend::on_frontend_event, this);
		set_preview(false, 0);
		delete poster_;
		clear_meters();
		if (cpu_) os_cpu_usage_info_destroy(cpu_);
	}

	// ---- state ----------------------------------------------------------------------------------------
	Snapshot snapshot() override
	{
		Snapshot d;
		d.version = std::string("OBS ") + obs_get_version_string();
		const uint64_t now = os_gettime_ns();

		d.recording = obs_frontend_recording_active();
		d.paused = d.recording && obs_frontend_recording_paused();
		if (d.recording) {
			if (!rec_start_) rec_start_ = now; // recording was already running when the plugin started
			uint64_t paused = paused_total_ + (d.paused && pause_start_ ? now - pause_start_ : 0);
			d.rec_dur = (double)(now - rec_start_ - std::min(paused, now - rec_start_)) / 1e9;
			if (obs_output_t *o = obs_frontend_get_recording_output()) {
				d.rec_bytes = (uint64_t)obs_output_get_total_bytes(o);
				obs_output_release(o);
			}
		}

		d.streaming = obs_frontend_streaming_active();
		if (d.streaming) {
			if (!stream_start_) stream_start_ = now;
			d.stream_dur = (double)(now - stream_start_) / 1e9;
			if (obs_output_t *o = obs_frontend_get_streaming_output()) {
				int total = obs_output_get_total_frames(o);
				d.dropped_pct = total > 0 ? 100.0 * obs_output_get_frames_dropped(o) / total : 0.0;
				uint64_t bytes = (uint64_t)obs_output_get_total_bytes(o);
				if (last_bytes_valid_ && bytes >= last_bytes_ && now > last_t_)
					d.bitrate_kbps = (double)(bytes - last_bytes_) * 8.0 / 1000.0 / ((double)(now - last_t_) / 1e9);
				last_bytes_ = bytes; last_t_ = now; last_bytes_valid_ = true;
				obs_output_release(o);
			}
		} else {
			last_bytes_valid_ = false;
		}

		d.replay_active = obs_frontend_replay_buffer_active();
		d.replay_available = d.replay_active || replay_enabled_in_profile();
		d.vcam = obs_frontend_virtualcam_active();

		// scenes + sources of the program scene
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t k = 0; k < scenes.sources.num; ++k) d.scenes.push_back(obs_source_get_name(scenes.sources.array[k]));
		obs_frontend_source_list_free(&scenes);
		std::set<std::string> live_audio;
		if (obs_source_t *cur = obs_frontend_get_current_scene()) {
			d.scene = obs_source_get_name(cur);
			if (obs_scene_t *sc0 = obs_scene_from_source(cur)) collect_live_audio(sc0, 0, live_audio);
			if (obs_scene_t *sc = obs_scene_from_source(cur)) {
				ItemCollector ic;
				obs_scene_enum_items(sc, collect_item, &ic);
				std::reverse(ic.items.begin(), ic.items.end()); // top-most first
				d.items = std::move(ic.items);
			}
			obs_source_release(cur);
		}

		obs_video_info ovi = {};
		if (obs_get_video_info(&ovi)) {
			d.base_res = std::make_pair((int)ovi.base_width, (int)ovi.base_height);
			d.out_res = std::make_pair((int)ovi.output_width, (int)ovi.output_height);
			if (ovi.fps_den) d.fps = std::round((double)ovi.fps_num / ovi.fps_den * 100.0) / 100.0;
		}
		if (cpu_) d.obs_cpu = std::round(os_cpu_usage_info_query(cpu_) * 10.0) / 10.0;

		InputCollector ic;
		obs_enum_sources(collect_input, &ic);
		d.inputs = std::move(ic.inputs);
		const std::set<std::string> globals = global_audio_names();
		for (auto &i : d.inputs) i.active = globals.count(i.name) || live_audio.count(i.name); // follows the scene
		sync_meters(d.inputs);
		detect_roles(d);
		return d;
	}

	// ---- commands ---------------------------------------------------------------------------------------
	void start_record() override { obs_frontend_recording_start(); }
	void stop_record() override { obs_frontend_recording_stop(); }
	void toggle_pause() override { obs_frontend_recording_pause(!obs_frontend_recording_paused()); }
	void start_stream() override { obs_frontend_streaming_start(); }
	void stop_stream() override { obs_frontend_streaming_stop(); }
	void start_replay() override { obs_frontend_replay_buffer_start(); }
	void stop_replay() override { obs_frontend_replay_buffer_stop(); }
	void save_replay() override { obs_frontend_replay_buffer_save(); }
	void start_vcam() override { obs_frontend_start_virtualcam(); }
	void stop_vcam() override { obs_frontend_stop_virtualcam(); }

	void set_scene(const std::string &name) override
	{
		if (obs_source_t *s = obs_get_source_by_name(name.c_str())) {
			obs_frontend_set_current_scene(s);
			obs_source_release(s);
		}
	}

	void set_item_enabled(int64_t id, bool on) override
	{
		with_item(id, [&](obs_sceneitem_t *it) { obs_sceneitem_set_visible(it, on); });
	}
	void set_item_locked(int64_t id, bool on) override
	{
		with_item(id, [&](obs_sceneitem_t *it) { obs_sceneitem_set_locked(it, on); });
	}

	void set_mute(const std::string &name, bool muted) override
	{
		if (obs_source_t *s = obs_get_source_by_name(name.c_str())) {
			obs_source_set_muted(s, muted);
			obs_source_release(s);
		}
	}
	void set_volume_db(const std::string &name, double db) override
	{
		if (obs_source_t *s = obs_get_source_by_name(name.c_str())) {
			obs_source_set_volume(s, db <= audio::FLOOR_DB ? 0.0f : (float)std::pow(10.0, db / 20.0));
			obs_source_release(s);
		}
	}

	// Writes the profile parameters used by the Simple and Advanced output modes. Takes effect when the buffer starts.
	void set_replay_seconds(int sec) override
	{
		if (config_t *c = obs_frontend_get_profile_config()) {
			config_set_int(c, "SimpleOutput", "RecRBTime", sec);
			config_set_int(c, "AdvOut", "RecRBTime", sec);
			config_save_safe(c, "tmp", nullptr);
		}
	}

	void request_screenshot() override { obs_frontend_take_screenshot(); }

	// Real-time preview: a render callback copies every rendered frame (max ~30/s) of the program output into a small
	// texture, reads it back one frame later (no GPU stall) and posts it to the UI thread.
	void set_preview(bool on, int width) override
	{
		pv_width_ = std::max(160, std::min(width, 960));
		if (on == pv_on_) return;
		pv_on_ = on;
		if (on) {
			obs_add_main_rendered_callback(&LiveBackend::on_rendered, this);
		} else {
			obs_remove_main_rendered_callback(&LiveBackend::on_rendered, this); // waits for a running callback
			obs_enter_graphics();
			free_preview_gfx();
			obs_leave_graphics();
		}
	}

	// ---- meters (real levels from libobs volume meters) ----------------------------------------------------
	std::map<std::string, double> meters() override
	{
		std::lock_guard<std::mutex> g(mtx_);
		return peaks_;
	}

	void set_meters_enabled(bool on) override
	{
		meters_on_ = on;
		if (!on) clear_meters();
	}

	std::string diagnostics() override
	{
		std::lock_guard<std::mutex> g(mtx_);
		return "volume meters: " + std::string(meters_on_ ? "on" : "off") + "\nattached meters: " + std::to_string(meters_.size()) +
		       "\nsources with level data: " + std::to_string(peaks_.size()) + "\nmeter callbacks received: " + std::to_string(meter_events_);
	}

private:
	struct Meter {
		LiveBackend *owner = nullptr;
		std::string name;
		obs_volmeter_t *vm = nullptr;
	};

	// Names of OBS's global audio devices (desktop audio 1-2, mic/aux 1-3): always live, whatever the scene.
	static std::set<std::string> global_audio_names()
	{
		std::set<std::string> out;
		for (uint32_t ch = 1; ch <= 5; ++ch) {
			if (obs_source_t *s = obs_get_output_source(ch)) {
				out.insert(obs_source_get_name(s));
				obs_source_release(s);
			}
		}
		return out;
	}

	static void on_rendered(void *p) { static_cast<LiveBackend *>(p)->render_preview_frame(); }

	void free_preview_gfx()
	{
		if (ss_[0]) gs_stagesurface_destroy(ss_[0]);
		if (ss_[1]) gs_stagesurface_destroy(ss_[1]);
		if (tr_) gs_texrender_destroy(tr_);
		ss_[0] = ss_[1] = nullptr;
		tr_ = nullptr;
		have_prev_ = false;
	}

	void render_preview_frame() // graphics thread
	{
		const uint64_t now = os_gettime_ns();
		if (now - last_pv_ < 33000000ULL) return;
		last_pv_ = now;
		obs_video_info ovi = {};
		if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height) return;
		const uint32_t bw = ovi.base_width, bh = ovi.base_height, w = (uint32_t)pv_width_.load(), h = (uint32_t)((uint64_t)w * bh / bw);
		if (!tr_ || w != tw_ || h != th_) {
			free_preview_gfx();
			tr_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
			ss_[0] = gs_stagesurface_create(w, h, GS_BGRA);
			ss_[1] = gs_stagesurface_create(w, h, GS_BGRA);
			tw_ = w; th_ = h; cur_ = 0;
			if (!tr_ || !ss_[0] || !ss_[1]) { free_preview_gfx(); return; }
		}
		gs_texrender_reset(tr_);
		if (!gs_texrender_begin(tr_, w, h)) return;
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)bw, 0.0f, (float)bh, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_render_main_texture();
		gs_blend_state_pop();
		gs_texrender_end(tr_);
		gs_stage_texture(ss_[cur_], gs_texrender_get_texture(tr_));
		if (have_prev_) { // the other surface holds the previous frame: reading it does not wait for the GPU
			uint8_t *data = nullptr;
			uint32_t linesize = 0;
			if (gs_stagesurface_map(ss_[1 - cur_], &data, &linesize)) {
				QImage img = QImage(data, (int)w, (int)h, (qsizetype)linesize, QImage::Format_RGB32).copy();
				gs_stagesurface_unmap(ss_[1 - cur_]);
				// OBS leaves alpha = 0 wherever no source covers the canvas. Force every pixel opaque so those areas
				// show as black, never as see-through.
				for (int y = 0; y < img.height(); ++y) {
					auto *line = reinterpret_cast<uint32_t *>(img.scanLine(y));
					for (int x = 0; x < img.width(); ++x) line[x] |= 0xFF000000u;
				}
				post_frame(std::move(img));
			}
		}
		have_prev_ = true;
		cur_ = 1 - cur_;
	}

	void post_frame(const QImage &img)
	{
		{
			std::lock_guard<std::mutex> g(pv_mtx_);
			latest_ = img; // only the newest frame is kept: a slow UI never builds up a queue
		}
		if (posted_.exchange(true)) return;
		QMetaObject::invokeMethod(poster_, [this] {
			QImage i;
			{
				std::lock_guard<std::mutex> g(pv_mtx_);
				i = std::move(latest_);
			}
			posted_ = false;
			if (on_event && !i.isNull()) {
				BackendEvent e;
				e.type = BackendEvent::PreviewFrame;
				e.image = i;
				on_event(e);
			}
		}, Qt::QueuedConnection);
	}

	static void on_frontend_event(enum obs_frontend_event e, void *p) { static_cast<LiveBackend *>(p)->handle(e); }

	static void on_volmeter(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
	{
		auto *m = static_cast<Meter *>(param);
		float best = -INFINITY;
		int n = obs_volmeter_get_nr_channels(m->vm);
		for (int i = 0; i < n && i < MAX_AUDIO_CHANNELS; ++i) best = std::max(best, peak[i]);
		std::lock_guard<std::mutex> g(m->owner->mtx_); // audio thread
		m->owner->peaks_[m->name] = std::isinf(best) ? audio::FLOOR_DB : (double)best;
		++m->owner->meter_events_;
	}

	void handle(enum obs_frontend_event e)
	{
		if (!on_event) return;
		const uint64_t now = os_gettime_ns();
		BackendEvent ev;
		switch (e) {
		case OBS_FRONTEND_EVENT_RECORDING_STARTED:
			rec_start_ = now; paused_total_ = 0; pause_start_ = 0;
			ev.type = BackendEvent::RecordStarted;
			break;
		case OBS_FRONTEND_EVENT_RECORDING_STOPPED: {
			rec_start_ = 0; paused_total_ = 0; pause_start_ = 0;
			ev.type = BackendEvent::RecordStopped;
			const char *p = obs_frontend_get_last_recording();
			ev.text = p ? QString::fromUtf8(p) : QString();
			bfree((void *)p);
			break;
		}
		case OBS_FRONTEND_EVENT_RECORDING_PAUSED: pause_start_ = now; break;
		case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
			if (pause_start_) paused_total_ += now - pause_start_;
			pause_start_ = 0;
			break;
		case OBS_FRONTEND_EVENT_STREAMING_STARTED: stream_start_ = now; last_bytes_valid_ = false; ev.type = BackendEvent::StreamStarted; break;
		case OBS_FRONTEND_EVENT_STREAMING_STOPPED: stream_start_ = 0; last_bytes_valid_ = false; ev.type = BackendEvent::StreamStopped; break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED: ev.type = BackendEvent::ReplayState; ev.flag = true; break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED: ev.type = BackendEvent::ReplayState; ev.flag = false; break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED: {
			ev.type = BackendEvent::ReplaySaved;
			const char *p = obs_frontend_get_last_replay();
			ev.text = p ? QString::fromUtf8(p) : QString();
			bfree((void *)p);
			break;
		}
		case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED: ev.type = BackendEvent::VcamState; ev.flag = true; break;
		case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED: ev.type = BackendEvent::VcamState; ev.flag = false; break;
		case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
			ev.type = BackendEvent::SceneChanged;
			if (obs_source_t *s = obs_frontend_get_current_scene()) {
				ev.text = QString::fromUtf8(obs_source_get_name(s));
				obs_source_release(s);
			}
			break;
		}
		case OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN: {
			ev.type = BackendEvent::ScreenshotTaken;
			const char *p = obs_frontend_get_last_screenshot();
			ev.text = p ? QString::fromUtf8(p) : QString();
			bfree((void *)p);
			break;
		}
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP: clear_meters(); return; // the sources are about to disappear
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		case OBS_FRONTEND_EVENT_FINISHED_LOADING: ev.type = BackendEvent::Reload; break;
		case OBS_FRONTEND_EVENT_EXIT: ev.type = BackendEvent::Exit; break;
		case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		case OBS_FRONTEND_EVENT_RECORDING_STARTING:
		case OBS_FRONTEND_EVENT_RECORDING_STOPPING: ev.type = BackendEvent::Changed; break;
		default: return;
		}
		on_event(ev);
	}

	// Which output mode is configured and is the replay buffer enabled in it?
	bool replay_enabled_in_profile() const
	{
		config_t *c = obs_frontend_get_profile_config();
		if (!c) return true;
		const char *mode = config_get_string(c, "Output", "Mode");
		const bool adv = mode && std::string(mode) == "Advanced";
		return config_get_bool(c, adv ? "AdvOut" : "SimpleOutput", "RecRB");
	}

	template<class F> void with_item(int64_t id, F fn)
	{
		if (obs_source_t *cur = obs_frontend_get_current_scene()) {
			if (obs_scene_t *sc = obs_scene_from_source(cur))
				if (obs_sceneitem_t *it = obs_scene_find_sceneitem_by_id(sc, id)) fn(it);
			obs_source_release(cur);
		}
	}

	// Microphone / desktop audio as OBS defines them (global audio devices); falls back to the source kind.
	void detect_roles(Snapshot &d)
	{
		auto has = [&](const std::string &n) {
			return std::any_of(d.inputs.begin(), d.inputs.end(), [&](const InputInfo &i) { return i.name == n; });
		};
		auto channel = [&](std::initializer_list<uint32_t> chans) -> std::string {
			for (uint32_t ch : chans) {
				if (obs_source_t *s = obs_get_output_source(ch)) {
					std::string n = obs_source_get_name(s);
					obs_source_release(s);
					if (has(n)) return n;
				}
			}
			return {};
		};
		d.desktop = channel({1, 2});
		d.mic = channel({3, 4, 5});
		auto by_kind = [&](const char *needle) {
			for (auto &i : d.inputs) if (i.kind.find(needle) != std::string::npos) return i.name;
			return std::string();
		};
		if (d.mic.empty()) d.mic = by_kind("input");
		if (d.desktop.empty()) d.desktop = by_kind("output");
	}

	// One volume meter per audio source, created on demand and removed when the source disappears.
	void sync_meters(const std::vector<InputInfo> &inputs)
	{
		if (!meters_on_) return;
		std::set<std::string> want;
		for (auto &i : inputs) want.insert(i.name);
		for (auto it = meters_.begin(); it != meters_.end();) {
			if (!want.count(it->first)) { destroy_meter(it->second); it = meters_.erase(it); }
			else ++it;
		}
		for (auto &n : want) {
			if (meters_.count(n)) continue;
			obs_source_t *s = obs_get_source_by_name(n.c_str());
			if (!s) continue;
			auto *m = new Meter;
			m->owner = this; m->name = n;
			m->vm = obs_volmeter_create(OBS_FADER_LOG);
			obs_volmeter_add_callback(m->vm, &LiveBackend::on_volmeter, m);
			if (!obs_volmeter_attach_source(m->vm, s)) { destroy_meter(m); obs_source_release(s); continue; }
			obs_source_release(s);
			meters_[n] = m;
		}
		std::lock_guard<std::mutex> g(mtx_);
		for (auto it = peaks_.begin(); it != peaks_.end();) it = want.count(it->first) ? std::next(it) : peaks_.erase(it);
	}

	void destroy_meter(Meter *m)
	{
		if (!m) return;
		if (m->vm) {
			obs_volmeter_remove_callback(m->vm, &LiveBackend::on_volmeter, m);
			obs_volmeter_destroy(m->vm); // detaches from the source and waits for running callbacks
		}
		delete m;
	}

	void clear_meters()
	{
		for (auto &kv : meters_) destroy_meter(kv.second);
		meters_.clear();
		std::lock_guard<std::mutex> g(mtx_);
		peaks_.clear();
	}

	os_cpu_usage_info_t *cpu_ = nullptr;
	uint64_t rec_start_ = 0, paused_total_ = 0, pause_start_ = 0, stream_start_ = 0;
	uint64_t last_bytes_ = 0, last_t_ = 0;
	bool last_bytes_valid_ = false;
	bool meters_on_ = true;
	std::unordered_map<std::string, Meter *> meters_;
	std::mutex mtx_;
	std::map<std::string, double> peaks_;
	uint64_t meter_events_ = 0;

	// real-time preview
	QObject *poster_ = nullptr;
	std::atomic<bool> pv_on_{false}, posted_{false};
	std::atomic<int> pv_width_{320};
	std::mutex pv_mtx_;
	QImage latest_;
	gs_texrender_t *tr_ = nullptr;
	gs_stagesurf_t *ss_[2] = {nullptr, nullptr};
	uint32_t tw_ = 0, th_ = 0;
	int cur_ = 0;
	bool have_prev_ = false;
	uint64_t last_pv_ = 0;
};

std::unique_ptr<Backend> make_live_backend() { return std::make_unique<LiveBackend>(); }

} // namespace ovl
