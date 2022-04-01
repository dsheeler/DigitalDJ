#ifndef XMMS2D_H
#define XMMS2D_H

#include <xmms2/xmmsclient/xmmsclient++.h>
#include <xmms2/xmmsclient/xmmsclient++-glib.h>

#include "MusicServerClient.h"

class Xmms2dClient : public MusicServerClient {
public:
    Xmms2dClient(Glib::RefPtr<Glib::MainLoop> ml);
    virtual ~Xmms2dClient();
    int next();
    int previous();
    int pause();
    int stop();
    // int on_song_change();
    
private:
    Xmms::Client *client;
    Xmms::Client *sync_client;
    bool my_current_id(const int& id);
    bool error_handler(const std::string& error);
    bool handle_bindata(const Xmms::bin& data);
};

#endif // XMMS2D_H
