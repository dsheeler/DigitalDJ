#ifndef FESTIVALSPEECHENGINE_H
#define FESTIVALSPEECHENGINE_H

#include "SpeechEngine.h"
#include <vector>
class FestivalSpeechEngine : public SpeechEngine {
public:
    FestivalSpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr);
    void speak(const string& to_say) override;
private:
    vector<string> voices;
};

#endif // FESTIVALSPEECHENGINE_H
