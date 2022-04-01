#ifndef FESTIVALSPEECHENGINE_H
#define FESTIVALSPEECHENGINE_H

#include "SpeechEngine.h"

class FestivalSpeechEngine : public SpeechEngine {
public:
    FestivalSpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr);
    void speak(const string& to_say) override;
};

#endif // FESTIVALSPEECHENGINE_H
