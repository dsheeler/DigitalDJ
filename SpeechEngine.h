#ifndef SPEECHENGINGE_H
#define SPEECHENGINGE_H

#include <memory>

#include <jack/types.h>
#include <jack/ringbuffer.h>

using namespace std;

class SpeechEngine {
public:
    SpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr);
    virtual void speak(const string& to_say) = 0;
    void process_message(string& message);
    void stop_speaking();

protected:
    shared_ptr<jack_ringbuffer_t> rb;
    jack_nframes_t sr;
};

#endif // SPEECHENGINGE_H
