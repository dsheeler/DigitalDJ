#include <cstdlib>
#include <string>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#include <signal.h>
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
#include <glibmm-2.4/glibmm.h>
#include <festival/festival.h>

#define ICON_PATH "/usr/local/share/icons/hicolor/64x64/apps/xmms2.png"

typedef jack_default_audio_sample_t sample_t;

const int RB_size = 3276800;
const double quiet_factor = 0.4;

static volatile sig_atomic_t signal_flag;

void set_signal_flag(int signal) { signal_flag = signal; }

class SpeechEngine {
    public:
        virtual void speek(const string& to_say,
                           shared_ptr<jack_ringbuffer_t> rb,
                           jack_nframes_t sr) = 0;
};

class FestivalSpeechEngine : public SpeechEngine {
    public:
        FestivalSpeechEngine();
        void speek(const string& to_say,
                   shared_ptr<jack_ringbuffer_t> rb,
                   jack_nframes_t sr) override;
};

FestivalSpeechEngine::FestivalSpeechEngine() {
    int heap_size = 20'000'000;  // default scheme heap size
    int load_init_files = 1; // we want the festival init files loaded
    vector<string> voices = {"cmu_us_awb_cg",
                             "cmu_us_rms_cg", "cmu_us_slt_cg",
                             "cmu_us_awb_arctic_clunits",
                             "cmu_us_bdl_arctic_clunits", "cmu_us_clb_arctic_clunits",
                             "cmu_us_jmk_arctic_clunits", "cmu_us_rms_arctic_clunits",
                             "cmu_us_slt_arctic_clunits", "kal_diphone", "rab_diphone" };
    festival_initialize(load_init_files,heap_size);
    festival_eval_command("(voice_cmu_us_jmk_arctic_clunits)");
}

void FestivalSpeechEngine::speek(const string& to_say,
                                 shared_ptr<jack_ringbuffer_t> rb,
                                 jack_nframes_t sr) {
    EST_Wave wave;
    festival_text_to_wave(to_say.c_str(), wave);
    double scale = 1/32768.0;
    wave.resample(sr);
    
    int numsamples = wave.num_samples();
    sample_t jbuf[numsamples];
    
    for (int i = 0; i < numsamples; i++) {
        jbuf[i] =  wave(i) * scale;
    }
        
    size_t num_bytes_to_write;
        
    num_bytes_to_write = numsamples*sizeof(sample_t);
        
    do {
        size_t nwritten = jack_ringbuffer_write(rb.get(), (char*)jbuf, num_bytes_to_write);
        if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
            usleep(100000);
        }
        num_bytes_to_write -= nwritten;
        
    } while (num_bytes_to_write > 0);
}



class DigitalDJ {
    public:
        DigitalDJ();
        ~DigitalDJ();
        
        auto scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf );
        bool my_current_id(const int& id);
        bool error_handler(const std::string& error);
        bool handle_bindata(const Xmms::bin& data);
        jack_port_t *jack_output_port(int i);
        jack_port_t *jack_input_port(int i);
        auto main_loop() { return ml_; }
        shared_ptr<jack_ringbuffer_t> jack_ringbuffer() { return rb_; }
       
    private:
        unique_ptr<SpeechEngine> se_;
        Xmms::Client xmms2_client_;
        Xmms::Client xmms2_sync_client_;
        NotifyNotification *notification_;
        jack_port_t  *jack_output_ports_[2], *jack_input_ports_[2];
        jack_client_t *jack_client_;
        shared_ptr<jack_ringbuffer_t> rb_;
        jack_nframes_t sr_ = 48000;
        GMainLoop *ml_;
        void setup_jack();
        void teardown_jack();
        void setup_signal_handler();
        static int process(jack_nframes_t nframes, void *arg);
};

struct RB_deleter { 
    void operator()(jack_ringbuffer_t* r) const {
        std::cout << "free jack ringbuffer...\n";
        jack_ringbuffer_free(r);
    }
};

DigitalDJ::DigitalDJ() :
    xmms2_client_(std::string("DigitalDJ")),
    xmms2_sync_client_(std::string("SyncDigitalDJ")) {
    
    xmms2_client_.connect(std::getenv("XMMS_PATH"));
    xmms2_sync_client_.connect(std::getenv("XMMS_PATH"));
    notify_init("xmms2-jack-dj");
    notification_ = notify_notification_new("", nullptr, ICON_PATH);
    setup_jack();
    se_ = make_unique<FestivalSpeechEngine>();
    setup_signal_handler();
    xmms2_client_.playback.broadcastCurrentID()(
                Xmms::bind( &DigitalDJ::my_current_id, this ),
                Xmms::bind( &DigitalDJ::error_handler, this )
                );

    xmms2_client_.setMainloop( new Xmms::GMainloop( xmms2_client_.getConnection() ) );
    ml_ = g_main_loop_new( 0, 0 );
}

DigitalDJ::~DigitalDJ() { 
    teardown_jack();
}

auto DigitalDJ::scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {
    
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

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */

int DigitalDJ::process (jack_nframes_t nframes, void *arg)
{
    sample_t *out[2], *in[2];
    size_t num_bytes_to_read, num_bytes, num_samples;
    DigitalDJ *dj = (DigitalDJ *)arg;

    for (size_t i = 0; i < 2; i++) {
        out[i] = (sample_t *) jack_port_get_buffer (dj->jack_output_port(i), nframes);
        in[i] = (sample_t *) jack_port_get_buffer (dj->jack_input_port(i), nframes);
    }

    num_bytes_to_read = sizeof(sample_t) * nframes;
    num_bytes = jack_ringbuffer_read(dj->jack_ringbuffer().get(), (char*)out[0], num_bytes_to_read);
    num_samples = num_bytes / sizeof(sample_t);
    for (size_t i = 0; i < num_samples; i++) {
        out[0][i] *= 1.5;
        out[1][i] = out[0][i] ;
        out[0][i] += in[0][i] * quiet_factor;
        out[1][i] += in[1][i] * quiet_factor;
        
    }
    for (size_t i = num_samples; i < nframes; i++) {
        out[0][i] = in[0][i];
        out[1][i] = in[1][i];
    }
    return 0;
}



bool DigitalDJ::handle_bindata(const Xmms::bin& data) {
    
    Glib::RefPtr< Gdk::Pixbuf > image;
    
    try {
        Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
        loader->write( data.c_str(), data.size() );
        loader->close();
        image = this->scale_pixbuf( loader->get_pixbuf() );
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

jack_port_t *DigitalDJ::jack_input_port(int i)
{
    if (i < 0 || i > 1) {
        return nullptr;
    } else {
        return jack_input_ports_[i];
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

    notify_notification_update (notification_, msg.c_str(), NULL, NULL);
    jack_ringbuffer_reset(rb_.get());

    /*Strip out annoying [Explicit] from title.*/
    std::string edited_text(say);
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
    
    /*Remove [, ], and /.*/
    auto toRemove = vector<string>{"[", "]", "/"};
    for (auto s : toRemove) {
        found = edited_text.find(s);
        if (found != edited_text.npos) {
            edited_text.erase(edited_text.begin() + found);
        }
    }
    
    /*Replace 'feat.' with 'featuring'.*/
    auto toReplace = "feat."s;
    found = edited_text.find(toReplace);
    if (found != edited_text.npos) {
        std::string replacement("featuring");
        edited_text.replace(found, toReplace.size(), replacement, 0, replacement.size());
    }
    se_->speek(edited_text, rb_, sr_);
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

    rb_ = shared_ptr<jack_ringbuffer_t>(jack_ringbuffer_create(RB_size), RB_deleter());

    string name = "DigitalDJ";

    strcpy(client_name, name.c_str());
    jack_client_ = jack_client_open(client_name, jack_options, &status);
    if (jack_client_ == nullptr) {
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
    
    sr_ = jack_get_sample_rate(jack_client_);
    jack_set_process_callback (jack_client_, process, this);

    jack_output_ports_[0] = jack_port_register (jack_client_, "output_l",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
    jack_output_ports_[1] = jack_port_register (jack_client_, "output_r",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
    
    if ((jack_output_ports_[0] == NULL) ||
            (jack_output_ports_[1] == NULL)) {
        cout << "no more JACK ports available" << endl;
        exit (1);
    }
    
    jack_input_ports_[0] = jack_port_register (jack_client_, "input_l",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsInput, 0);
    jack_input_ports_[1] = jack_port_register (jack_client_, "input_r",
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsInput, 0);
    
    if ((jack_input_ports_[0] == NULL) ||
            (jack_input_ports_[1] == NULL)) {
        cout << "no more JACK ports available" << endl;
        exit (1);
    }
    if (jack_activate (jack_client_)) {
        fprintf (stderr, "cannot activate client");
        exit (1);
    }
}

void DigitalDJ::teardown_jack() {
    for (int i = 0; i < 2; ++i) {
        jack_port_unregister(jack_client_, jack_output_ports_[i]);
    }
    if (jack_client_) {
        jack_client_close(jack_client_);
    }
    
}

void DigitalDJ::setup_signal_handler() {
    signal(SIGQUIT, set_signal_flag);
    signal(SIGTERM, set_signal_flag);
    signal(SIGHUP, set_signal_flag);
    signal(SIGINT, set_signal_flag);
}

void process_signal(DigitalDJ &dj) {
    while (1) {
        if (signal_flag == SIGQUIT || signal_flag == SIGTERM ||
                signal_flag == SIGHUP || signal_flag == SIGINT) {
            cout << "got signal; terminating" << endl;
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    g_main_loop_quit(dj.main_loop());
}

int main (int argc, char *argv[]) {
    Gtk::Main kit( argc, argv );
    
    try {
        DigitalDJ myclient;
        thread signal_handler(process_signal, ref(myclient));
        g_main_loop_run(myclient.main_loop());
        signal_handler.join();
    }
    catch( Xmms::connection_error& err ) {
        std::cout << "Connection failed: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
    
}

