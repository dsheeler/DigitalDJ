#include <festival/festival.h>
#include <vector>
#include <jack/types.h>

#include "FestivalSpeechEngine.h"

FestivalSpeechEngine::FestivalSpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr) :
    SpeechEngine::SpeechEngine(rb, sr) {
    int heap_size = 20'000'000;  // default scheme heap size
    int load_init_files = 1; // we want the festival init files loaded
    this->voices = {"cmu_us_awb_cg",
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
    string tmp = to_say;
    string voice = voices[rand() % voices.size()];
    string voice_command = "(voice_" + voice + ")";
    festival_eval_command(voice_command.c_str());
    process_message(tmp);
    festival_text_to_wave(tmp.c_str(), wave);
    double scale = 1/32768.0;
    wave.resample(sr);

    int numsamples = wave.num_samples();
    jack_default_audio_sample_t jbuf[numsamples];

    for (int i = 0; i < numsamples; i++) {
        jbuf[i] =  wave(i) * scale;
    }

    size_t num_bytes_to_write;

    num_bytes_to_write = numsamples*sizeof(jack_default_audio_sample_t);

    do {
        size_t nwritten = jack_ringbuffer_write(rb.get(), (char*)jbuf, num_bytes_to_write);
        if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
            usleep(100000);
        }
        num_bytes_to_write -= nwritten;
    } while (num_bytes_to_write > 0);
}
