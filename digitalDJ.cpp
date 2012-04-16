#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <espeak/speak_lib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <xmmsclient/xmmsclient.h>
/* also include this to get glib integration */
#include <xmmsclient/xmmsclient-glib.h>
/* include the GLib header */
#include <glib-2.0/glib.h>
#include <glib-2.0/glib/gmain.h>

#include <QtCore/QString>

typedef jack_default_audio_sample_t sample_t;

jack_port_t  *my_output_ports[2], *his_input_ports[2];
jack_client_t *client;
jack_ringbuffer_t *jringbuf;
xmmsc_connection_t *connection, *async_connection;

const char *err_buf;
const char *val;
int intval;

xmmsv_t *dict_entry;
xmmsv_t *infos;


/*
 * We set this up as a callback for our current_id
 * method. Read the main program first before
 * returning here.
 */
int
my_current_id (xmmsv_t *value, void *userdata)
{

  char say[8000];
  xmmsc_result_t *result;
  xmmsv_t *return_value;

  /*
   * At this point the value struct contains the
   * answer. And we can now extract it as normal.
   */

  int32_t id;
  uint espeak_id;
  /*
   * we passed the mainloop as an argument
   * to set_notifier, which means it will be
   * passed as userdata to this function
   */
  GMainLoop *ml = (GMainLoop *) userdata;

  printf("in the result callback\n");

  if (!xmmsv_get_int (value, &id)) {
    fprintf (stderr, "Value didn't contain the expected type!\n");
    exit (EXIT_FAILURE);
  }

  result = xmmsc_medialib_get_info (connection, id);

  /* And waaait for it .. */
  xmmsc_result_wait (result);

  /* Let's reuse the previous return_value pointer, it
   * was invalidated as soon as we freed the result that
   * contained it anyway.
   */
  return_value = xmmsc_result_get_value (result);

  if (xmmsv_is_error (return_value) &&
      xmmsv_get_error (return_value, &err_buf)) {
    /*
     * This can return error if the id
     * is not in the medialib
     */
    fprintf (stderr, "medialib get info returns error, %s\n",
             err_buf);
    exit (EXIT_FAILURE);
  }

  /*
   * Because of the nature of the dict returned by
   * xmmsc_medialib_get_info, we need to convert it to
   * a simpler dict using xmmsv_propdict_to_dict.
   * Let's not worry about that for now and accept it
   * as a fact of life.
   *
   * See tut5 for a discussion about dicts and propdicts.
   *
   * Note that xmmsv_propdict_to_dict creates a new
   * xmmsv_t struct, which we will need to free manually
   * when we're done with it.
   */
  infos = xmmsv_propdict_to_dict (return_value, NULL);

  /*
   * We must first retrieve the xmmsv_t struct
   * corresponding to the "artist" key in the dict,
   * and then extract the string from that struct.
   */
  if (!xmmsv_dict_get (infos, "artist", &dict_entry) ||
      !xmmsv_get_string (dict_entry, &val)) {
    /*
     * if we end up here it means that the key "artist" wasn't
     * in the dict or that the value for "artist" wasn't a
     * string.
     *
     * You can check the type of the entry (if there is one) with
     * xmmsv_get_type (dict_entry). It will return an
     * xmmsv_type_t enum describing the type.
     *
     * Actually this is no disaster, it might just mean that
     * we don't have an artist tag on this entry. This might mean
     * it's a stream, in which case "channel" will be good for artist
     * otherwise, call it "No Artist"
     */
      if (!xmmsv_dict_get (infos, "channel", &dict_entry) ||
          !xmmsv_get_string(dict_entry, &val)) {
          val = "No Artist";
      }
  }

  /* print the value */
  sprintf (say, "%s. ", val);

  if (!xmmsv_dict_get (infos, "title", &dict_entry) ||
      !xmmsv_get_string (dict_entry, &val)) {
    val = "No Title";
  }
  sprintf (say, "%s%s. ", say, val);

  /*
   * Let's extract an integer as well
   */
//  if (!xmmsv_dict_get (infos, "bitrate", &dict_entry) ||
//      !xmmsv_get_int (dict_entry, &intval)) {
//    intval = 0;
//  }
  //  sprintf (say, "%sBitrate is  %i. ", say, intval);

  /*
   * We need to free infos manually here, else we will leak.
   */
  xmmsv_unref (infos);

  /*
   * !!Important!!
   *
   * When unreffing the result here we will free
   * the memory that we have extracted from the dict,
   * and that includes all the string pointers of the
   * dict entries! So if you want to keep strings
   * somewhere you need to copy that memory! Very
   * important otherwise you will get undefined behaviour.
   */
  xmmsc_result_unref (result);

  char *blah = (char *) malloc(4096);
  while ( jack_ringbuffer_read(jringbuf,blah, 2048) ) {
      printf("clearing ringbuffer\n");
  }

  espeak_Synth(say,
               8000,
               0,
               POS_CHARACTER,
               0,
               espeakCHARS_AUTO,
               &espeak_id,
               NULL);



  // g_main_loop_quit (ml);

  /* We will see in the next tutorial what the return value of a
   * callback is used for.  It only matters for signals and
   * broadcasts anyway, so for simple commands like here, we can
   * return either TRUE or FALSE.
   */
  return TRUE;

  /* One thing to notice here, at the end of callbacks,
   * is that as soon as the xmmsv_t struct goes out of
   * scope, it will be freed automatically.
   * If you want to keep it around in memory, you will
   * need to increment its refcount using xmmsv_ref.
   */
}


//void speak(QString utterance) {
//
//    char say[8000];
//    static uint id = 1;
//    espeak_Cancel();
//    strcpy(say,utterance.toAscii().data());
//    espeak_Synth(say,
//	       8000,
//	       0,
//	       POS_CHARACTER,
//	       0,
//	       espeakCHARS_AUTO,
//	       &id,
//	       NULL);
//
//    id++;
//
//
//}

static void usage () {

}

static void signal_handler(int sig) {

  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

int synth_callback(short *wav, int numsamples, espeak_EVENT *events) {

  int i;
  size_t num_bytes_to_write;
  sample_t buf[2*numsamples];
  static int times = 0;

  if (!wav) return 0;
  if (numsamples == 0) return 0;

  /* i am assuming espeak output at 22050 hz and jack at 44100 hz.
     this is true for me and i cannot find a way to
     modify espeak sample rate, so here i am
     not using interpolation, just copying */

  double scale = 1/32768.0;


  for (int i = 0; i < numsamples; i++) {
      buf[2*i] = wav[i] * scale;
  }

  for (int i = 1; i < 2 * numsamples - 2; i += 2) {
      buf[i] = 0.5 * (buf[i-1] + buf[i+1]);
  }

  buf[2*numsamples - 1] = 2 * buf[2*numsamples-2] - buf[2*numsamples-3];

  num_bytes_to_write = 2*numsamples*sizeof(sample_t);
  int done = 0;

  do {
    int nwritten = jack_ringbuffer_write(jringbuf, (char*)buf, num_bytes_to_write);
    num_bytes_to_write -= nwritten;
    if (nwritten == 0 && num_bytes_to_write > 0) {
      usleep(40000);
    }
  } while (num_bytes_to_write > 0);

  return 0;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */

int process (jack_nframes_t nframes, void *arg) {
  sample_t *out[2];
  int jack_sample_size;
  size_t num_bytes_to_write, num_bytes_written, num_samples_written;
  int i;

  num_bytes_to_write = sizeof(sample_t) * nframes;
  
  for (i = 0; i < 2; i++) {
    out[i] = (sample_t *) jack_port_get_buffer (my_output_ports[i], nframes);
  }

  num_bytes_written = jack_ringbuffer_read(jringbuf, (char*)out[0], num_bytes_to_write);
  num_samples_written = num_bytes_written / sizeof(sample_t);
  for (i = 0; i < num_samples_written; i++) {
    out[1][i] = out[0][i];
  } 
  for (i = num_samples_written; i < nframes; i++) {
    out[0][i] = out[1][i] = 0.0;
  }
  return 0;      
}


int main (int argc, char *argv[]) {
  
  float fm, fc, m;

  int buflength = 20;
  uint id;
  jack_ringbuffer_t *ringbuf;
  const char **ports;
  char *client_name;
  const char *server_name = NULL;
  jack_options_t jack_options = JackNullOption;
  jack_status_t status;
  int option_index;
  int opt;
  int i;
  jack_nframes_t SR;
  int SRespeak;
  xmmsc_result_t *result;

  char his_input_port_name[80], my_output_port_name[80];

  GMainLoop *ml;

		

  his_input_port_name[0] = '\0';
  my_output_port_name[0] = '\0';

  const char *options = "o:";
  struct option long_options[] =
    {
      {"outputPort", 1, 0, 'o'},
      {0, 0, 0, 0}
    };
  
  while ((opt = getopt_long (argc, argv, options, long_options, &option_index)) != EOF) {
    switch (opt) {
    case 'o':
      strncpy (his_input_port_name, optarg, 79);
      his_input_port_name[79] = '\0';
      break;
    default:
      fprintf (stderr, "unknown option %c\n", opt); 
    case 'h':
      usage ();
      return -1;
    }
  }
	  
  jringbuf = jack_ringbuffer_create(3276800);
  
  /* open a client connection to the JACK server */
    
  client_name = (char *) malloc(80 * sizeof(char));
  char *tmp = "syntheticDJ";
  strcpy(client_name, tmp);
  client = jack_client_open (client_name, jack_options, &status);
  if (client == NULL) {
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
  if (status & JackNameNotUnique) {
    client_name = jack_get_client_name(client);
    fprintf (stderr, "unique name `%s' assigned\n", client_name);
  }

  SR = jack_get_sample_rate (client);

  /* tell the JACK server to call `process()' whenever
     there is work to be done.
  */
  
  jack_set_process_callback (client, process, NULL);

  /* tell the JACK server to call `jack_shutdown()' if
     it ever shuts down, either entirely, or if it
     just decides to stop calling us.
  */

  //  jack_on_shutdown (client, jack_shutdown, 0);

  /* create stereo out ports */

  my_output_ports[0] = jack_port_register (client, "output_l",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsOutput, 0);
  my_output_ports[1] = jack_port_register (client, "output_r",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsOutput, 0);

  if ((my_output_ports[0] == NULL) ||
      (my_output_ports[1] == NULL)) {
    fprintf(stderr, "no more JACK ports available\n");
    exit (1);
  }
   
  /* Tell the JACK server that we are ready to roll.  Our
   * process() callback will start running now. */

  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    exit (1);
  }

  /* Connect the ports.  You can't do this before the client is
   * activated, because we can't make connections to clients
   * that aren't running.  Note the confusing (but necessary)
   * orientation of the driver backend ports: playback ports are
   * "input" to the backend, and capture ports are "output" from
   * it.
   */
 	
   if (jack_connect (client, jack_port_name(my_output_ports[0]), "JackMix:return_l")) {
    fprintf (stderr, "cannot connect to port left output\n");
  }
  
  if (jack_connect (client, jack_port_name(my_output_ports[1]), "JackMix:return_r")) {
    fprintf (stderr, "cannot connect to port right output\n");
  }
  
  /*
  if (jack_connect (client, "ardour:master/out 1", jack_port_name(my_input_port))) {
    fprintf (stderr, "cannot connect to port '%s'\n", "ardour:master/out 1");
  }
  */
    
  /* install a signal handler to properly quits jack client */
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);

  /* now start espeak and give it a try */

  SRespeak = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 
			 buflength, NULL, 0);

  espeak_SetSynthCallback(synth_callback);
  espeak_SetParameter(espeakVOLUME, 200, 0);
  //  espeak_SetParameter(espeakWORDGAP, 10, 0);
  espeak_SetParameter(espeakRATE, 76, 0);
  espeak_SetParameter(espeakPITCH, 30, 0);
  espeak_SetParameter(espeakRANGE, 30, 0);

  espeak_VOICE voice_spec;
  voice_spec.name = NULL;
  voice_spec.languages = "english-us";
  voice_spec.gender=1;
  voice_spec.age=20;
  voice_spec.variant = 1;
  espeak_SetVoiceByProperties(&voice_spec);




  connection =  xmmsc_init ("SyntheticDJ");
  if (xmmsc_connect(connection, "tcp://") == 0) {
      exit(1);
  }

  async_connection =  xmmsc_init ("async_SyntheticDJ");
  if (xmmsc_connect(async_connection, "tcp://") == 0) {
      exit(1);
  }

  ml = g_main_loop_new (NULL, FALSE);

  result = xmmsc_broadcast_playback_current_id (async_connection);
  xmmsc_result_notifier_set(result, my_current_id, ml);
  xmmsc_result_unref(result);

  xmmsc_mainloop_gmain_init(async_connection);
  /*
   * We are now all set to go. Just run the m
doneain loop and watch the magic.
   */
  printf("main loop\n");
  g_main_loop_run (ml);
  
  jack_client_close (client);
  exit (0);
}
