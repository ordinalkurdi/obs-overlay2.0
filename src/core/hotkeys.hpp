// Hotkeys through OBS's own hotkey system: they show up in OBS Settings > Hotkeys (rebindable there, conflicts are
// flagged by OBS), work while a game has focus, and are saved with the scene collection like every OBS hotkey.
#pragma once
#include <QObject>
#include <vector>

namespace ovl {

class Hotkeys : public QObject {
	Q_OBJECT
public:
	explicit Hotkeys(QObject *parent = nullptr);
	~Hotkeys() override;
	void register_all();   // call once OBS has finished loading
	void unregister_all();
	void load(void *obs_data);  // obs_data_t*  (scene collection load)
	void save(void *obs_data);  // obs_data_t*  (scene collection save)

	struct Entry;

signals:
	void triggered(const QString &action);

private:
	std::vector<Entry *> entries_;
};

} // namespace ovl
