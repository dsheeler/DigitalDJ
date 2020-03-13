#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <xmms2/xmmsclient/xmmsclient++.h>
#include <xmms2/xmmsclient/xmmsclient++-glib.h>
#include <glib-2.0/glib.h>
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <gtkmm.h>
#include <gdkmm-2.4/gdkmm/pixbufloader.h>
#include <gdkmm-2.4/gdkmm/pixbuf.h>
#include <festival/festival.h>

#include <cstdlib>
#include <string>
#include <iostream>

#define SAY_BUF_SIZE 8000
#define ICON_PATH "/usr/local/share/icons/hicolor/64x64/apps/xmms2.png"

typedef jack_default_audio_sample_t sample_t;

Glib::RefPtr< Gdk::Pixbuf > scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {
    
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

static void signal_handler(int sig) {
    fprintf(stderr, "signal %d received, exiting ...\n", sig);
    exit(0);
}

class DigitalDJ {
    public:
        DigitalDJ();
        ~DigitalDJ();
        
        bool my_current_id(const int& id);
        bool error_handler(const std::string& error);
        bool handle_bindata(const Xmms::bin& data);
        jack_port_t *jack_output_port(int i);
        jack_ringbuffer_t *jack_ringbuffer() { return jack_ringbuf_; }
        
    private:
        Xmms::Client xmms2_client_;
        Xmms::Client xmms2_sync_client_;
        NotifyNotification *notification_;
        jack_port_t  *jack_output_ports_[2];
        jack_client_t *jack_client_;
        jack_ringbuffer_t *jack_ringbuf_;
        jack_nframes_t sample_rate_ = 48000;
        
        void setup_jack();
        void setup_festival();
        void setup_signal_handler();
        void festival_synth(const char *text);
        static int process(jack_nframes_t nframes, void *arg);
};



DigitalDJ::DigitalDJ()
    : xmms2_client_(std::string("DigitalDJ")),
      xmms2_sync_client_(std::string("SyncDigitalDJ")) {
    
    xmms2_client_.connect(std::getenv("XMMS_PATH"));
    xmms2_sync_client_.connect(std::getenv("XMMS_PATH"));
    notify_init("xmms2-jack-dj");
    notification_ = notify_notification_new("", NULL, ICON_PATH);
    setup_jack();
    setup_festival();
    setup_signal_handler();
    xmms2_client_.playback.broadcastCurrentID()(
                Xmms::bind( &DigitalDJ::my_current_id, this ),
                Xmms::bind( &DigitalDJ::error_handler, this )
                );
    
    xmms2_client_.setMainloop( new Xmms::GMainloop( xmms2_client_.getConnection() ) );
    
    /*
  * Initialize and run glib mainloop, check out glib documentation
  * for this.
  */
    GMainLoop* ml = g_main_loop_new( 0, 0 );
    g_main_loop_run( ml );
    
}

DigitalDJ::~DigitalDJ() { 
    jack_ringbuffer_free(jack_ringbuf_);
    jack_client_close (jack_client_);

}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */

int DigitalDJ::process (jack_nframes_t nframes, void *arg)
{
    sample_t *out[2];
    size_t num_bytes_to_read, num_bytes, num_samples;
    DigitalDJ *dj = (DigitalDJ *)arg;

    for (size_t i = 0; i < 2; i++) {
        out[i] = (sample_t *) jack_port_get_buffer (dj->jack_output_port(i), nframes);
    }

    num_bytes_to_read = sizeof(sample_t) * nframes;
    num_bytes = jack_ringbuffer_read(dj->jack_ringbuffer(), (char*)out[0], num_bytes_to_read);
    num_samples = num_bytes / sizeof(sample_t);
    for (size_t i = 0; i < num_samples; i++) {
        out[1][i] = out[0][i];
    }
    for (size_t i = num_samples; i < nframes; i++) {
        out[0][i] = out[1][i] = 0.0;
    }
    return 0;
}

void DigitalDJ::festival_synth(const char *text_to_say) {
    /*Strip out annoying [Explicit] from title.*/
    std::string edited_text(text_to_say);
    std::size_t found = edited_text.find("[Explicit]");
    if (found != edited_text.npos) {
        edited_text.erase(found, 10);
    }
    
    /*Replace '&' with 'and'.*/
    found = edited_text.find(" & ");
    while (found != edited_text.npos) {
        std::string replacement(" and ");
        edited_text.replace(found, 3, replacement, 0, replacement.size());
        found = edited_text.find(" & ");
    }
    
    /*Replace 'feat.' with 'featuring'.*/
    std::string toReplace = "feat.";
    found = edited_text.find(toReplace);
    if (found != edited_text.npos) {
        std::string replacement("featuring");
        edited_text.replace(found, toReplace.size(), replacement, 0, replacement.size());
    }
    
    EST_Wave wave;
    festival_text_to_wave(edited_text.c_str(), wave);
    double scale = 1/32768.0;
    wave.resample(sample_rate_);
    
    int numsamples = wave.num_samples();
    sample_t jbuf[numsamples];
    
    for (int i = 0; i < numsamples; i++) {
        jbuf[i] =  wave(i) * scale;
    }
    
    size_t num_bytes_to_write;
    
    num_bytes_to_write = numsamples*sizeof(sample_t);
    
    do {
        size_t nwritten = jack_ringbuffer_write(jack_ringbuf_, (char*)jbuf, num_bytes_to_write);
        if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
            usleep(100000);
        }
        num_bytes_to_write -= nwritten;
        
    } while (num_bytes_to_write > 0);
}

bool DigitalDJ::handle_bindata(const Xmms::bin& data) {
    
    Glib::RefPtr< Gdk::Pixbuf > image;
    
    try {
        Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
        std::cout << data.size() << " is size of bindata." << std::endl;
        
        loader->write( data.c_str(), data.size() );
        loader->close();
        image = scale_pixbuf( loader->get_pixbuf() );
        notify_notification_set_image_from_pixbuf (notification_, image->gobj());
    }
    catch ( Glib::Error& e ) {
        std::clog << "Could not load image: " << e.what() << std::endl;
    }
    
    GError *gerr = 0;
    notify_notification_show (notification_, &gerr);
    
    
    return true;
}

jack_port_t *DigitalDJ::jack_output_port(int i)
{
    if (i < 0 || i > 1) {
        return nullptr;
    } else {
        return jack_output_ports_[i];
    }
}

bool DigitalDJ::my_current_id(const int& id) {
    
    std::string val, say, msg;
    
    msg += "xmms2d: ";
    
    try {
        Xmms::Dict info =  xmms2_sync_client_.medialib.getInfo(id);
        
        std::cout << "artist = ";
        
        try {
            std::cout << info["artist"] << std::endl;
            val = boost::get< std::string >(info["artist"]) + std::string(". ");
            
        } catch( Xmms::no_such_key_error& err ) {
            
            std::cout << "No artist" << std::endl;
            val = std::string("Unknown Artist. ");
        }
        
        say = val;
        msg += val;
        
        std::cout << "title = ";
        
        try {
            std::cout << info["title"] << std::endl;
            val = boost::get< std::string >(info["title"]);
        }
        catch( Xmms::no_such_key_error& err ) {
            std::cout << "Title" << std::endl;
            val = std::string("Unknown Title.");
        }
        
        say += val + std::string(".");
        msg += std::string("'") + val + std::string("'");
        
        try {
            val = boost::get< std::string >(info["picture_front"]);
            xmms2_client_.bindata.retrieve(val)(
                        Xmms::bind( &DigitalDJ::handle_bindata, this ),
                        Xmms::bind( &DigitalDJ::error_handler, this )
                        );
        }
        catch(Xmms::no_such_key_error& err) {
        }
        
    }
    catch( Xmms::result_error& err ) {
        // This can happen if the id is not in the medialib
        std::cout << "medialib get info returns error, "
                  << err.what() << std::endl;
    }
    /*
  if(!xmmsv_dict_get (infos, "picture_front", &dict_entry) || !xmmsv_get_string (dict_entry, &val)) {
    val = NULL;
  }
  
  if (val != NULL) {
    result = xmmsc_bindata_retrieve(async_connection, val);
    xmmsc_result_wait (result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) && xmmsv_get_error (return_value, &err_buf)) {
      fprintf (stderr, "bindata retrieve returns error, %s\n", err_buf);
    } else {
      handle_bindata(return_value);
    }
  }
  
*/
    notify_notification_update (notification_, msg.c_str(), NULL, NULL);
    jack_ringbuffer_reset(jack_ringbuf_);
    
    festival_synth(say.c_str());
    return true;
    
}

bool DigitalDJ::error_handler( const std::string& error )
{
    
    /*
     * This is the error callback function which will get called
     * if there was an error in the process.
     */
    std::cout << "Error: " << error << std::endl;
    return false;
    
}

void DigitalDJ::setup_jack() {
    char client_name[80];
    jack_options_t jack_options = JackNullOption;
    jack_status_t status;

    jack_ringbuf_ = jack_ringbuffer_create(3276800);

    string name = "DigitalDJ";

    strcpy(client_name, name.c_str());
    jack_client_ = jack_client_open (client_name, jack_options, &status);
    if (jack_client_ == NULL) {
        fprintf (stderr, "jack_client_open() failed, "
                         "status = 0x%2.0x\n", status);
        if (status & JackServerFailed) {
            fprintf (stderr, "Unable to connect to JACK server\n");
        }
        exit (1);
    }
    if (status & JackServerStarted) {
        fprintf (stderr, "JACK server started\n");
    }
    
    sample_rate_ = jack_get_sample_rate(jack_client_);
    jack_set_process_callback (jack_client_, process, this);

    jack_output_ports_[0] = jack_port_register (jack_client_, "output_l",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
    jack_output_ports_[1] = jack_port_register (jack_client_, "output_r",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
    
    if ((jack_output_ports_[0] == NULL) ||
            (jack_output_ports_[1] == NULL)) {
        fprintf(stderr, "no more JACK ports available\n");
        exit (1);
    }
    
    if (jack_activate (jack_client_)) {
        fprintf (stderr, "cannot activate client");
        exit (1);
    }
}

void DigitalDJ::setup_festival() {
    
    int heap_size = 2000000;  // default scheme heap size
    int load_init_files = 1; // we want the festival init files loaded
    
    festival_initialize(load_init_files,heap_size);
    
    festival_eval_command("(voice_cmu_us_slt_arctic_clunits)");
    festival_synth("Hi.  I am your synthetic xmms2, jack DJ.  I hope you like my voice!");
    
}

void DigitalDJ::setup_signal_handler() {
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
}

int main (int argc, char *argv[]) {
    Gtk::Main kit( argc, argv );
    
    try {
        DigitalDJ myclient;
    }
    catch( Xmms::connection_error& err ) {
        std::cout << "Connection failed: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
    
}

