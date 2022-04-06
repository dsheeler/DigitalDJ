#include "MpdClient.h"

#include <cassert>
#include <iostream>
#include <mpd/client.h>

using namespace std;

#define CHUNK_SIZE 8192

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
        get_song_info(last_song_info, true);
    }
    start_idling();
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

void MpdClient::get_song_info(song_info_t &info, bool get_albumart) {
    struct mpd_song *song = mpd_run_current_song(conn);
    if (song != NULL) {
        info.id = mpd_song_get_id(song);
        info.artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
        info.album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
        info.title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
        if (get_albumart) {
            string uri(mpd_song_get_uri(song));
            int ret = 0;
            int start_at = 0;
            uint8_t buf[CHUNK_SIZE];
            Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
            int done = 0;
            while(!done) {
                ret = mpd_run_readpicture(conn, uri.c_str(), start_at, (void*)buf, CHUNK_SIZE);
                if (ret > 0) {
                    start_at += ret;
                    loader->write( buf, ret);
                } else {
                    done = 1;
                }
            }
            try {
                loader->close();
                info.albumart = loader->get_pixbuf();
            } catch ( Glib::Error& e ) {
                info.albumart = Gdk::Pixbuf::create(Gdk::Colorspace::COLORSPACE_RGB, 1, 8, 1,1);
                info.albumart->fill(0);
            }
        }
        mpd_song_free(song);
    } else {
        handle_error("get_song_info mpd_recv_song");
    }
}

bool MpdClient::io_event_handler(Glib::IOCondition ioc) {
    assert(Glib::IOCondition::IO_IN & ioc);
    stop_idling();
    song_info_t info;
    get_song_info(info, false);
    if (last_song_info.id != info.id) {
        get_song_info(last_song_info, true);
        song_change_signal.emit(last_song_info);
    }
    struct mpd_status *status;
    status = mpd_run_status(conn);
    if (status == NULL) {
        handle_error("IO Event Handler Error Getting Status");
    } else {
        mpd_state new_state = mpd_status_get_state(status);
        if ((new_state != last_state && last_state == MPD_STATE_STOP
             && new_state == MPD_STATE_PLAY))  {
            song_change_signal.emit(last_song_info);
        }
        last_state = new_state;
        mpd_status_free(status);
    }
    start_idling();
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

void MpdClient::stop_idling() {
    mpd_run_noidle(conn);
}

void MpdClient::start_idling() {
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
    stop_idling();
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
    start_idling();
    return 0;
}

int MpdClient::previous() {
    struct mpd_status * status;
    stop_idling();
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
    start_idling();
    return 0;
}

int MpdClient::pause() {
    struct mpd_status * status;
    stop_idling();
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
    start_idling();
    return 0;
}

int MpdClient::stop() {
    stop_idling();
    if (!mpd_run_stop(conn)) {
        handle_error("Stop Error");
    }
    start_idling();
    return 0;
}
