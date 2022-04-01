#include "Xmms2dClient.h"

static auto scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {
    const int width = pixbuf->get_width();
    const int height = pixbuf->get_height();
    int dest_width = 128;
    int dest_height = 128;
    double ratio = width / static_cast< double >(height);
    
    if( width > height ) {
        dest_height = static_cast< int >(dest_height / ratio);
    }
    else if( height > width ) {
        dest_width = static_cast< int >(dest_width * ratio);
    }
    return pixbuf->scale_simple( dest_width, dest_height, Gdk::INTERP_BILINEAR );
}

Xmms2dClient::Xmms2dClient(Glib::RefPtr<Glib::MainLoop> gml) : MusicServerClient(gml) {
    client = new Xmms::Client(std::string("DigitalDJ"));
    sync_client = new Xmms::Client(std::string("SyncDigitalDJ"));
    client->connect(std::getenv("XMMS2_PATH"));
    sync_client->connect(std::getenv("XMMS_PATH"));

    client->playback.broadcastCurrentID()(
                Xmms::bind( &Xmms2dClient::my_current_id, this ),
                Xmms::bind( &Xmms2dClient::error_handler, this )
                );

    client->setMainloop( new Xmms::GMainloop( client->getConnection() ) );

}

Xmms2dClient::~Xmms2dClient() {
    delete client;
    delete sync_client;
}

int Xmms2dClient::next() {
    xmmsc_result_t *result;
    xmmsv_t *return_value;
    const char *err_buf;
    result = xmmsc_playlist_set_next_rel(client->getConnection(), 1);
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playlist_set_next_rel returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsc_result_unref (result);
    result = xmmsc_playback_tickle(client->getConnection());
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playback_tickle returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsc_result_unref (result);
    return 0;
}


int Xmms2dClient::previous()
{
    xmmsc_result_t *result;
    xmmsv_t *return_value;
    const char *err_buf;
    result = xmmsc_playlist_set_next_rel(client->getConnection(), -1);
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playlist_set_next_rel returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsc_result_unref (result);
    result = xmmsc_playback_tickle(client->getConnection());
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playback_tickle returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsc_result_unref (result);
    return 0;
}

int Xmms2dClient::pause() {
    xmmsc_result_t *result;
    xmmsv_t *return_value;
    int32_t status;
    const char *err_buf;
    result = xmmsc_playback_status(client->getConnection());
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playback status returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsv_get_int (return_value, &status);
    xmmsc_result_unref(result);
    if (status == XMMS_PLAYBACK_STATUS_PLAY)
        result = xmmsc_playback_pause(client->getConnection());
    else
        result = xmmsc_playback_start(client->getConnection());
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
            xmmsv_get_error (return_value, &err_buf)) {
        fprintf (stderr, "playback toggle returned error, %s",
                 err_buf);
        xmmsc_result_unref(result);
        return -1;
    }
    xmmsc_result_unref (result);
    return 0;
}

int Xmms2dClient::stop() {
    xmmsc_result_t *result;
    xmmsv_t *return_value;
    const char *err_buf;
    result = xmmsc_playback_stop(client->getConnection());
    xmmsc_result_wait(result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) &&
     xmmsv_get_error (return_value, &err_buf)) {
      fprintf (stderr, "playback status returned error, %s",
       err_buf);
      xmmsc_result_unref(result);
      return -1;
    }
    xmmsc_result_unref (result);
    return 0;
}

bool Xmms2dClient::my_current_id(const int &id) {
    std::string val, spoken_msg, notification_msg;
    notification_msg += "xmms2d: ";
    try {
        Xmms::Dict info =  sync_client->medialib.getInfo(id);
        std::cout << "artist = ";
        try {
            std::cout << info["artist"] << std::endl;
            val = boost::get< std::string >(info["artist"]) + std::string(". ");
        } catch( Xmms::no_such_key_error& err ) {
            std::cout << "No artist" << std::endl;
                val = std::string("Unknown Artist. ");
        }
        spoken_msg = val;
        notification_msg += val;
        std::cout << "title = ";
        try {
            std::cout << info["title"] << std::endl;
            val = boost::get< std::string >(info["title"]);
        }
        catch( Xmms::no_such_key_error& err ) {
            std::cout << "Title" << std::endl;
            val = std::string("Unknown Title.");
        }
        spoken_msg += val + std::string(".");
        notification_msg += std::string("'") + val + std::string("'");
        try {
            val = boost::get< std::string >(info["picture_front"]);
            client->bindata.retrieve(val)(
                        Xmms::bind( &Xmms2dClient::handle_bindata, this ),
                        Xmms::bind( &Xmms2dClient::error_handler, this )
                        );
        } catch(Xmms::no_such_key_error& err) {
        }
    } catch( Xmms::result_error& err ) {
        // This can happen if the id is not in the medialib
        std::cout << "medialib get info returns error, "
                  << err.what() << std::endl;
    }
    
    
    //notify_notification_update (notification_, msg.c_str(), NULL, NULL);

    //se->speek(spoken_msg);
    return true;
        
}
    
bool Xmms2dClient::error_handler( const std::string& error ) {
    
    /*
     * This is the error callback function which will get called
     * if there was an error in the process.
     */
    std::cout << "Error: " << error << std::endl;
    return false;
        
}

bool Xmms2dClient::handle_bindata(const Xmms::bin& data) {

    Glib::RefPtr< Gdk::Pixbuf > image;

    try {
        Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
        loader->write( data.c_str(), data.size() );
        loader->close();
        image = scale_pixbuf( loader->get_pixbuf() );
       // notify_notification_set_image_from_pixbuf (notification_, image->gobj());
    }
    catch ( Glib::Error& e ) {
        std::clog << "Could not load image: " << e.what() << std::endl;
    }

    //GError *gerr = 0;
    //notify_notification_show (notification_, &gerr);

    return true;
}
