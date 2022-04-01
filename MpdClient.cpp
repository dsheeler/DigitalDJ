#include "MpdClient.h"

#include <cassert>
#include <iostream>
#include <mpd/client.h>

using namespace std;

MpdClient::MpdClient(Glib::RefPtr<Glib::MainLoop> ml) : MusicServerClient(ml) {
    conn = mpd_connection_new(NULL, 0, 0);
    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        handle_error("Connection Failure");
        exit(EXIT_FAILURE);
    }
    attach_io_event_handler();
    struct mpd_status *status;
    status = mpd_run_status(conn);
    if (status == NULL)
        handle_error("MpdClient Error Getting Status");
    else {
        last_state = mpd_status_get_state(status);
        mpd_status_free(status);
        song_info info;
        get_song_info(info);
        last_songid = info.id;
        string state_name;
        state_to_name(last_state, state_name);
        cout << state_name << endl;
        cout << "'" << info.title << "'" << " by " << info.artist << endl;
    }
    start_listening();
}

MpdClient::~MpdClient() {
    mpd_connection_free(conn);
}

int MpdClient::handle_error(string prefix) {
    if (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS) {
        fprintf(stdout, "%s: hmmmmm. no error, actually.\n", prefix.c_str());
    } else {
        fprintf(stdout, "%s: %s\n", prefix.c_str(), mpd_connection_get_error_message(conn));
    }
    return 0;
}

void MpdClient::get_song_info(song_info_t &info) {
    struct mpd_song *song = mpd_run_current_song(conn);
    if (song != NULL) {
        info.id = mpd_song_get_id(song);
        info.artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
        info.album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
        info.title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
        mpd_song_free(song);
    } else {
        handle_error("get_song_info mpd_recv_song");
    }
}

bool MpdClient::io_event_handler(Glib::IOCondition ioc) {
    assert(Glib::IOCondition::IO_IN & ioc);
    stop_listening();
    song_info_t info;
    get_song_info(info);
    if (last_songid != info.id) {
        last_songid = info.id;
        song_change_signal.emit(info);
    }
    struct mpd_status *status;
    status = mpd_run_status(conn);
    if (status == NULL) {
        handle_error("IO Event Handler Error Getting Status");
    } else {
        mpd_state new_state = mpd_status_get_state(status);
        if ((new_state != last_state && last_state == MPD_STATE_STOP
             && new_state == MPD_STATE_PLAY))  {
            song_change_signal.emit(info);
        }
        last_state = new_state;
        mpd_status_free(status);
    }
    start_listening();
    return true;
}

void MpdClient::attach_io_event_handler() {
    Glib::RefPtr<Glib::MainContext> mc = ml->get_context();
    Glib::RefPtr<Glib::IOSource> iosource = Glib::IOSource::create(
                                                mpd_connection_get_fd(conn),
                                                Glib::IOCondition::IO_IN);
    sigc::slot<bool(Glib::IOCondition)> slot = sigc::mem_fun(this,& MpdClient::io_event_handler);
    iosource->connect(slot);
    iosource->attach(mc);
}

void MpdClient::stop_listening() {
    mpd_run_noidle(conn);
}

void MpdClient::start_listening() {
    if (!mpd_send_idle(conn)) {
        handle_error("start_listening");
    }
}

void MpdClient::state_to_name(mpd_state state, string &name) {
   switch(state) {
   case MPD_STATE_PAUSE:
       name = "PAUSED";
       break;
   case MPD_STATE_PLAY:
       name = "PLAYING";
       break;
   case MPD_STATE_STOP:
       name = "STOPPED";
       break;
   default:
       name = "UNKNOWN";
   }
}

int MpdClient::next() {
    struct mpd_status * status;
    stop_listening();
    status = mpd_run_status(conn);
    if (status == NULL)
        handle_error("Next Error Getting Status");
    else {
        if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
                mpd_status_get_state(status) == MPD_STATE_PAUSE) {
            if (!mpd_run_next(conn)) {
                handle_error("Next Error");
            }
        }
        mpd_status_free(status);
    }
    start_listening();
    return 0;
}

int MpdClient::previous() {
    struct mpd_status * status;
    stop_listening();
    status = mpd_run_status(conn);
    if (status == NULL)
        handle_error("Previous Error Getting Status");
    else {
        if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
                mpd_status_get_state(status) == MPD_STATE_PAUSE) {
            if (!mpd_run_previous(conn)) {
                handle_error("Previous Error");
            }
        }
        mpd_status_free(status);
    }
    start_listening();
    return 0;
}

int MpdClient::pause() {
    struct mpd_status * status;
    stop_listening();
    status = mpd_run_status(conn);
    if (status == NULL) {
        handle_error("Toggle Pause Error Getting Status");
    } else {
        if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
                mpd_status_get_state(status) == MPD_STATE_PAUSE) {
            if (!mpd_run_toggle_pause(conn)) {
                handle_error("Play/Pause Error");
            }
        } else {
            if (!mpd_run_play(conn)) {
                handle_error("Play Error");
            }
        }
        mpd_status_free(status);
    }
    start_listening();
    return 0;
}

int MpdClient::stop() {
    stop_listening();
    if (!mpd_run_stop(conn)) {
        handle_error("Stop Error");
    }
    start_listening();
    return 0;
}
