# Digital DJ

Digital DJ uses the festival text to speech engine and the jack audio toolkit
to create an MPD client that, upon song change, announces the upcoming song
title and artist and shows a notification. It also takes midi input that can
control MPD with pause/play, stop, previous track, and next track operations.

## MIDI Control of MPD
Digital DJ has a midi input port that allows contrtol over MPD.

An MPD operation is triggered by sending a value of 127 to the operation's
mapped MIDI CC number. The following table shows the CC number to operation
mapping.

| MIDI CC number | MPD Operation  |
|----------------|----------------|
| 41             | Play/Pause     |
| 42             | Stop           |
| 43             | Previous Track |
| 44             | Next Track     |

## JACK Connections

To hear the announcements, route the output of Digital DJ to your
desired pipewire or jack input ports. Any audio routed to the Digital
DJ input ports will be re-played on the output ports modified by lowering
volume of the incoming audio while the DJ is speaking. 

Route your MPD output ports to the DJ input ports to hear the lowered volume while
the DJ makes its announcements. 
