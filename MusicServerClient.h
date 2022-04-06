#ifndef MUSICSERVERCLIENT_H
#define MUSICSERVERCLIENT_H

#include <string>
#include <gtkmm.h>

using namespace std;

typedef struct song_info {
    int id;
    string title;
    string artist;
    string album;
    Glib::RefPtr<Gdk::Pixbuf> albumart;
} song_info_t;

class MusicServerClient {
public:
    MusicServerClient(Glib::RefPtr<Glib::MainLoop> ml);
    sigc::signal<void(song_info_t)> song_changed() { return song_change_signal; }
    virtual int next() = 0;
    virtual int previous() = 0;
    virtual int pause() = 0;
    virtual int stop()= 0;
protected:
    Glib::RefPtr<Glib::MainLoop> ml;
    sigc::signal<void(song_info_t)> song_change_signal;
    Glib::RefPtr<Gdk::Pixbuf> scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf );
};

#endif // MUSICSERVERCLIENT_H
