#include <stdlib.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include <thread>
#include <memory>
#include <chrono>

#include <signal.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include <glib-2.0/glib.h>
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <gdkmm-2.4/gdkmm/pixbufloader.h>
#include <gdkmm-2.4/gdkmm/pixbuf.h>
#include <glibmm-2.4/glibmm.h>
#include <sigc++-2.0/sigc++/sigc++.h>
#include <festival/festival.h>

#include "MpdClient.h"
#include "Xmms2dClient.h"

using namespace std;

const char *ICON_PATH = "/usr/local/share/icons/hicolor/64x64/apps/xmms2.png";

typedef jack_default_audio_sample_t sample_t;

const int RB_size = 3276800;
const int RB_size_midi = 1024*3*8;
const double quiet_factor = 0.4;

class DigitalDJ;

static volatile __sig_atomic_t signal_flag;

void set_signal_flag(int signal) { signal_flag = signal; }

class SpeechEngine {
public:
    SpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr);
    virtual void speak(const string& to_say) = 0;
    void process_message(std::string message);
    void stop_speaking();

protected:
    shared_ptr<jack_ringbuffer_t> rb;
    jack_nframes_t sr;
};

class FestivalSpeechEngine : public SpeechEngine {
public:
    FestivalSpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr);
    void speak(const string& to_say) override;
};

SpeechEngine::SpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr) {
    this->rb = rb;
    this->sr = sr;
}

void SpeechEngine::stop_speaking() {
    jack_ringbuffer_reset(rb.get());
}

void SpeechEngine::process_message(std::string msg) {
    /*Strip out annoying [Explicit] from title.*/
    std::size_t found = msg.find("[Explicit]");
    if (found != msg.npos) {
        msg.erase(found, 10);
    }
    /*Replace '&' with 'and'.*/
    found = msg.find(" & ");
    while (found != msg.npos) {
        std::string replacement(" and ");
        msg.replace(found, 3, replacement, 0, replacement.size());
        found = msg.find(" & ");
    }
        
    /*Remove [, ], and /.*/
    auto toRemove = vector<string>{"[", "]", "/"};
    for (auto s : toRemove) {
        found = msg.find(s);
        if (found != msg.npos) {
            msg.erase(msg.begin() + found);
        }
    }
        
    /*Replace 'feat.' with 'featuring'.*/
    auto toReplace = "feat."s;
    found = msg.find(toReplace);
    if (found != msg.npos) {
        std::string replacement("featuring");
        msg.replace(found, toReplace.size(), replacement, 0, replacement.size());
    }
}

FestivalSpeechEngine::FestivalSpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr) :
    SpeechEngine::SpeechEngine(rb, sr) {
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

void FestivalSpeechEngine::speak(const string& to_say) {
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

struct RB_deleter { 
    void operator()(jack_ringbuffer_t* r) const {
        std::cout << "free jack ringbuffer...\n";
        jack_ringbuffer_free(r);
    }
};

class DigitalDJ {
public:
    DigitalDJ();
    ~DigitalDJ();

    auto main_loop() { return ml; }
    
private:

    bool midi_events_check();

    jack_port_t *jack_output_port(int i);
    jack_port_t *jack_input_port(int i);
    jack_port_t *jack_input_port_midi();

    shared_ptr<jack_ringbuffer_t> jack_ringbuffer() { return rb_; }
    shared_ptr<jack_ringbuffer_t> jack_ringbuffer_midi() { return rb_midi_; }
    Glib::RefPtr<Glib::MainLoop> ml;
    unique_ptr<SpeechEngine> se;
    unique_ptr<MusicServerClient> ms;
    NotifyNotification *notification_;
    jack_port_t  *jack_output_ports_[2], *jack_input_ports_[2];
    jack_port_t *jack_input_port_midi_;
    jack_client_t *jack_client_;
    shared_ptr<jack_ringbuffer_t> rb_, rb_midi_;
    jack_nframes_t sr_ = 48000;
    void setup_jack();
    void teardown_jack();
    void setup_signal_handler();
    void on_song_changed(song_info_t info);
    static int process(jack_nframes_t nframes, void *arg);
};

DigitalDJ::DigitalDJ() {
    notify_init("xmms2-jack-dj");
    notification_ = notify_notification_new("", nullptr, ICON_PATH);
    setup_jack();
    ml = Glib::MainLoop::create(true);
    se = make_unique<FestivalSpeechEngine>(rb_, sr_);
    ms = make_unique<MpdClient>(ml);
    ms->song_changed().connect(sigc::mem_fun(*this, &DigitalDJ::on_song_changed));
    setup_signal_handler();
    sigc::connection conn = Glib::signal_timeout().connect(sigc::mem_fun(*this,
                                                                   &DigitalDJ::midi_events_check), 50);
}

DigitalDJ::~DigitalDJ() { 
    teardown_jack();
}

bool DigitalDJ::midi_events_check() {
    jack_midi_event_t event;
    while (jack_ringbuffer_read_space(rb_midi_.get()) >= sizeof(event)) {
        jack_ringbuffer_read(rb_midi_.get(), (char *)&event, sizeof(event));
        printf("Midi Event\n");
        switch(event.buffer[1]) {
            case 41: 
                if (event.buffer[2] == 127) ms->pause();
                break;
            case 42:
                if (event.buffer[2] == 127) ms->stop();
                break;
            case 43:
                if (event.buffer[2] == 127) ms->previous();
                break;
            case 44:
                if (event.buffer[2] == 127) ms->next();
                break;
        }
    }
    return true;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int DigitalDJ::process (jack_nframes_t nframes, void *arg) {
    sample_t *out[2], *in[2];
    jack_nframes_t event_count;
    jack_midi_event_t event_in;
    void *in_midi;
    size_t num_bytes_to_read, num_bytes, num_samples;
    DigitalDJ *dj = (DigitalDJ *)arg;

    for (size_t i = 0; i < 2; i++) {
        out[i] = (sample_t *) jack_port_get_buffer (dj->jack_output_port(i), nframes);
        in[i] = (sample_t *) jack_port_get_buffer (dj->jack_input_port(i), nframes);
    }
    in_midi = jack_port_get_buffer(dj->jack_input_port_midi(), nframes);
    event_count = jack_midi_get_event_count(in_midi);

    for (size_t i = 0 ; i < event_count; i++) {
      jack_midi_event_get(&event_in, in_midi, i);
      if (event_in.size != 3 ||
          (event_in.buffer[0] & 0xF0) != 0xB0 ||
          event_in.buffer[1] > 127 ||
          event_in.buffer[2] > 127)
      {
        continue;
      }
      assert(event_in.time < nframes);
      jack_ringbuffer_write(dj->jack_ringbuffer_midi().get(), (char*)&event_in, sizeof(event_in));
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

jack_port_t *DigitalDJ::jack_output_port(int i) {
    if (i < 0 || i > 1) {
        return nullptr;
    } else {
        return jack_output_ports_[i];
    }
}

jack_port_t *DigitalDJ::jack_input_port(int i) {
    if (i < 0 || i > 1) {
        return nullptr;
    } else {
        return jack_input_ports_[i];
    }
}

jack_port_t *DigitalDJ::jack_input_port_midi() {
    return jack_input_port_midi_;
}

void DigitalDJ::setup_jack() {
    char client_name[80];
    jack_options_t jack_options = JackNullOption;
    jack_status_t status;

    rb_ = shared_ptr<jack_ringbuffer_t>(jack_ringbuffer_create(RB_size), RB_deleter());
    rb_midi_ = shared_ptr<jack_ringbuffer_t>(jack_ringbuffer_create(RB_size_midi), RB_deleter());

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
    jack_input_port_midi_ = jack_port_register(jack_client_, "input_midi",
                                               JACK_DEFAULT_MIDI_TYPE,
                                               JackPortIsInput, 0);

    if ((jack_input_ports_[0] == NULL) ||
            (jack_input_ports_[1] == NULL) ||
            (jack_input_port_midi_ == NULL)) {
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
        jack_port_unregister(jack_client_, jack_input_ports_[i]);
        
    }
    jack_port_unregister(jack_client_, jack_input_port_midi_);
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

void DigitalDJ::on_song_changed(song_info_t info) {
    se->stop_speaking();
    se->speak("Playing: " + info.title + " by " + info.artist);
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
    dj.main_loop()->quit();
}

int main (int argc, char *argv[]) {
    Gtk::Main kit( argc, argv );
    
    try {
        DigitalDJ dj;
        thread signal_handler(process_signal, ref(dj));
        dj.main_loop()->run();
        signal_handler.join();
    }
    catch( Xmms::connection_error& err ) {
        std::cout << "Connection failed: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
    
}


