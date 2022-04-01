#include "SpeechEngine.h"
#include <vector>
#include <string>

SpeechEngine::SpeechEngine(shared_ptr<jack_ringbuffer_t> rb, jack_nframes_t sr) {
    this->rb = rb;
    this->sr = sr;
}

void SpeechEngine::stop_speaking() {
    jack_ringbuffer_reset(rb.get());
}

void SpeechEngine::process_message(string& msg) {
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
