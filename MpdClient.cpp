#include "MpdClient.h"

#include <cassert>
#include <iostream>
#include <mpd/client.h>

using namespace std;

static void state_to_name(mpd_state state, string &name) {
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

MpdClient::MpdClient(Glib::RefPtr<Glib::MainLoop> ml) : MusicServerClient(ml) {
    conn = mpd_connection_new(NULL, 0, 0);
    if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        handle_error("Connection Failure");
        exit(EXIT_FAILURE);
    }
    attach_io_event_handler();
    /*struct mpd_status *status;
    mpd_send_status(conn);
    status = mpd_recv_status(conn);
    if (status == NULL)
        handle_error("MpdClient Error Getting Status");
    else {
        last_state = mpd_status_get_state(status);
        mpd_send_current_song(conn);
        struct mpd_song *song = mpd_recv_song(conn);
        last_songid = mpd_song_get_id(song);
        string state_name;
        state_to_name(last_state, state_name);
        cout << "state: " << state_name << endl;
        const char *song_title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
        if (song_title == NULL) song_title = "No TITLE";
        cout << "song name: '" << song_title << "'" << endl;
        mpd_status_free(status);
    }*/
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
        //assert(mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS);
    }
    return 0;
}

void MpdClient::get_song_info(song_info_t &info) {
    //return;
    //stop_listening(false);
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
    
    //start_listening();
}

bool MpdClient::io_event_handler(Glib::IOCondition ioc) {
    assert(Glib::IOCondition::IO_IN & ioc);
    stop_listening(false);
    enum mpd_idle event;
    event = mpd_recv_idle(conn, false);
    cout << event << endl;
    song_info_t info;
    get_song_info(info);
    //start_listening();
    cout << info.title << endl;
    if (last_songid != info.id) {
        last_songid = info.id;
        song_change_signal.emit(info);
    } else {
        //stop_listening(true);
        struct mpd_status *status;
        if (!mpd_send_status(conn)) {
            handle_error("HANDLER send_status");
        } else {
            status = mpd_recv_status(conn);
            if (status == NULL) {
                handle_error("IO Event Handler Error Getting Status");
            } else {
                mpd_state new_state = mpd_status_get_state(status);
                cout << "song id " << mpd_status_get_song_id(status) << endl;
                cout << "new " << new_state << " old " << last_state << endl;
                if ((new_state != last_state
                     && last_state == MPD_STATE_STOP
                     && new_state == MPD_STATE_PLAY))  {
                    
                    cout << "from stop to start" << endl;
                    song_change_signal.emit(info);
                }
                last_state = new_state;
                mpd_status_free(status);
            }
        }
    }
    /*while (event) {
        event = mpd_recv_idle(conn, false);
        cout << event << endl;
    }*/
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

void MpdClient::stop_listening(bool clear_events) {
    if (clear_events) {
        if (!mpd_run_noidle(conn)) {
            handle_error("stop_listening run_noidle");
        } 
    } else { 
        if (!mpd_send_noidle(conn)) {
            handle_error("stop_listening send_noidle");
        }
    }
}

void MpdClient::start_listening() {
    if (!mpd_send_idle(conn)) {
        handle_error("start_listening");
    }
}

int MpdClient::next() {
    struct mpd_status * status;
    stop_listening(true);
    if (!mpd_send_status(conn)) {
        handle_error("Next send_status");
    } else {
        status = mpd_recv_status(conn);
        if (status == NULL)
            handle_error("Next Error Getting Status");
        else {
            if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
                    mpd_status_get_state(status) == MPD_STATE_PAUSE) {
                if (!mpd_run_next(conn)) {
                    handle_error("Next Error");
                }
            }
        }
    }
    start_listening();
    return 0;
}

int MpdClient::previous() {
    struct mpd_status * status;
    printf("in previous before stop_listening\n");
    stop_listening(true);
    printf("in previous\n");
    if (!mpd_send_status(conn)) {
        handle_error("Previous send_status");
    } else {
        status = mpd_recv_status(conn);
        if (status == NULL)
            handle_error("Previous Error Getting Status");
        else {
            if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
                    mpd_status_get_state(status) == MPD_STATE_PAUSE) {
                if (!mpd_run_previous(conn)) {
                    handle_error("Previous Error");
                }        
            }
        }
    }
    start_listening();
    return 0;
}

int MpdClient::pause() {
    struct mpd_status * status;
    stop_listening(true);
    if (!mpd_send_status(conn)) {
        handle_error("puase send_status");
    } else {
        status = mpd_recv_status(conn);
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
        }
    }
    start_listening();
    return 0;
}

int MpdClient::stop() {
    stop_listening(true);
    if (!mpd_run_stop(conn)) {
        handle_error("Stop Error");
    }
    start_listening();
    return 0;
}
