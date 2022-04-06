#ifndef MPDCLIENT_H
#define MPDCLIENT_H

#include <string>
#include <mpd/client.h>

#include "MusicServerClient.h"

using namespace std;

class MpdClient : public MusicServerClient {
public:
    MpdClient(Glib::RefPtr<Glib::MainLoop> ml);
    virtual ~MpdClient();
    int next();
    int previous();
    int pause();
    int stop();
private:
    struct mpd_connection *conn;
    song_info_t last_song_info;
    mpd_state last_state;
    int handle_error(string prefix);
    bool io_event_handler(Glib::IOCondition ioc);
    void attach_io_event_handler();
    void stop_idling();
    void start_idling();
    void get_song_info(song_info_t &info, bool get_albumart = false);
    static void state_to_name(mpd_state state, string &name);
};
#endif // MPDCLIENT_H

