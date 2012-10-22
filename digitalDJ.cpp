#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <espeak/speak_lib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <xmmsclient/xmmsclient.h>
/* also include this to get glib integration */
#include <xmmsclient/xmmsclient-glib.h>
#include <xmmsclient/xmmsclient++.h>
#include <xmms2/xmmsclient/xmmsclient++-glib.h>

/* include the GLib header */
#include <glib-2.0/glib.h>
#include <glib-2.0/glib/gmain.h>
#include <gtkmm.h>
#include <notification.h>
#include <notify.h>
#include <gdk/gdk.h>
#include <gdkmm/gdkmm/pixbufloader.h>
#include <gdkmm/gdkmm/pixbuf.h>
#include <gtkmm/main.h>

#define SAY_BUF_SIZE 8000
#define ICON_PATH "/usr/local/share/icons/hicolor/64x64/apps/xmms2.png"

typedef jack_default_audio_sample_t sample_t;

NotifyNotification *notification;



jack_port_t  *my_output_ports[2], *his_input_ports[2];
jack_client_t *client;
jack_ringbuffer_t *jringbuf;
xmmsc_connection_t *connection, *async_connection;

const char *err_buf;
const char *val;
int intval;

xmmsv_t *dict_entry;
xmmsv_t *infos;

void log (const char *pattern, ...) 
{
	va_list arguments;
	FILE *logfile;
	
	logfile = fopen ("/tmp/DDJ.log", "a");
	fprintf (logfile, pattern);
	fclose(logfile);
}


/* Dummy callback that resets the action status as finished. */
void
done (xmmsc_result_t *res)
{
	const gchar *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_error (val, &err)) {
		log ("Server error: %s\n", err);
	}

	xmmsc_result_unref (res);
}

Glib::RefPtr< Gdk::Pixbuf > 
scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) 
{
	const int width = pixbuf->get_width();
	const int height = pixbuf->get_height();
	int dest_width = 64;
	int dest_height = 64;
	double ratio = width / static_cast< double >(height);

	if( width > height ) {
		dest_height = static_cast< int >(100 / ratio);
	}
	else if( height > width ) {
		dest_width = static_cast< int >(100 * ratio);
	}
	
    return pixbuf->scale_simple( dest_width, dest_height, Gdk::INTERP_BILINEAR );
}

int sync_callback (jack_transport_state_t state, jack_position_t *position, void* arg) 
{
	char say[SAY_BUF_SIZE];
	uint espeak_id;
	xmmsc_result_t *res;
	
	if (state == JackTransportStopped) {
		log ("transport stopped\n");
		res = xmmsc_playback_stop (connection);
		xmmsc_result_wait (res);
		done (res);
		
		//  sprintf(say, "Transport stop.");
		// espeak_Synth(say, SAY_BUF_SIZE, 0, POS_CHARACTER, 0, espeakCHARS_AUTO, 
		//            &espeak_id, NULL);
	} else if (state == JackTransportStarting) {
		log ("starting xmms output\n");
		res = xmmsc_playback_start (connection);
		xmmsc_result_wait (res);
		done (res);
		
		sprintf(say, "Transport start.");
		espeak_Synth(say, SAY_BUF_SIZE, 0, POS_CHARACTER, 0, espeakCHARS_AUTO, 
		             &espeak_id, NULL);
	} else if (state == JackTransportRolling) {
		log ("transport rolling\n");
	}    
	return 1;
}

bool handle_bindata (const xmmsv_t *bindata)
{
	unsigned int size;
	const unsigned char *data;
	Glib::RefPtr< Gdk::Pixbuf > image;
//	Gdk::Pixbuf p;
	xmmsv_get_bin(bindata, &data, &size);
	try {

		Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
		
		loader->write( data, size );
		loader->close();

		image = scale_pixbuf( loader->get_pixbuf() );
		
		
		notify_notification_set_image_from_pixbuf (notification, image->gobj());
	}
	catch ( Glib::Error& e ) {
		std::clog << "Could not load image: " << e.what() << std::endl;
		
	}

	
	
	
	return true;
}

int current_id_callback (xmmsv_t *value, void *userdata) 
{

	char say[8000];
	xmmsc_result_t *result;
	xmmsv_t *return_value;

	int32_t id;
	uint espeak_id;
	

	if (!xmmsv_get_int (value, &id)) {
		fprintf (stderr, "Value didn't contain the expected type!\n");
		exit (EXIT_FAILURE);
	}

    result = xmmsc_medialib_get_info (async_connection, id);
	xmmsc_result_wait (result);
  
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



    infos = xmmsv_propdict_to_dict (return_value, NULL);
	
	if (!xmmsv_dict_get (infos, "artist", &dict_entry) ||
	        !xmmsv_get_string (dict_entry, &val)) {
		
		if (!xmmsv_dict_get (infos, "channel", &dict_entry) ||
		        !xmmsv_get_string(dict_entry, &val)) {
			val = "No Artist";
		}
	}

    char msg[8000];
    sprintf(msg, "xmms2d: ");

    sprintf (say, "%s. ", val);
	sprintf (msg, "%s%s, ", msg, val);
	
	if (!xmmsv_dict_get (infos, "title", &dict_entry) ||
	        !xmmsv_get_string (dict_entry, &val)) {
		val = "No Title";
	}
	sprintf (say, "%s%s. ", say, val);
	sprintf (msg, "%s'%s'", msg, val);
	
	if(!xmmsv_dict_get (infos, "picture_front", &dict_entry) ||
	        !xmmsv_get_string (dict_entry, &val)) {    
		val = NULL;
	}
	
	if (val != NULL) {
		result = xmmsc_bindata_retrieve(async_connection, val);
		xmmsc_result_wait (result);
	  
		return_value = xmmsc_result_get_value (result);
	  
		if (xmmsv_is_error (return_value) &&
		        xmmsv_get_error (return_value, &err_buf)) {
		
			fprintf (stderr, "bindata retrieve returns error, %s\n",
			         err_buf);
			
		} else {
			handle_bindata(return_value);
		}
		
		
	}
	
	GError *gerr = 0;
	notify_notification_update (notification, msg, NULL, 
	                            NULL);
	notify_notification_show (notification, &gerr);
	xmmsv_unref (infos);

	xmmsc_result_unref (result);

	//char blah[4096];
	jack_ringbuffer_reset(jringbuf);
	//while ( jack_ringbuffer_read(jringbuf,blah, 2048) );
     
	espeak_Synth(say,
	             8000,
	             0,
	             POS_CHARACTER,
	             0,
	             espeakCHARS_AUTO,
	             &espeak_id,
	             NULL);
	
	return TRUE;
	
}

static void signal_handler(int sig) 
{

	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

int synth_callback(short *wav, int numsamples, espeak_EVENT *events) 
{

	size_t num_bytes_to_write;
	sample_t buf[2*numsamples];
 
	if (!wav) return 0;
	if (numsamples == 0) return 0;

	/* i am assuming espeak output at 22050 hz and jack at 44100 hz.
	 this is true for me and i cannot find a way to
	 modify espeak sample rate */
	
	double scale = 1/32768.0;
	
	for (int i = 0; i < numsamples; i++) {
		buf[2*i] = wav[i] * scale;
	}
	
	for (int i = 1; i < 2 * numsamples - 2; i += 2) {
		buf[i] = 0.5 * (buf[i-1] + buf[i+1]);
	}
	
	buf[2*numsamples - 1] = 2 * buf[2*numsamples-2] - buf[2*numsamples-3];
	
	num_bytes_to_write = 2*numsamples*sizeof(sample_t);
	
	do {
		int nwritten = jack_ringbuffer_write(jringbuf, (char*)buf, num_bytes_to_write);
		
		if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
			usleep(1000000);
		}
		num_bytes_to_write -= nwritten;
	
	} while (num_bytes_to_write > 0);
	
	return 0;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */

int process (jack_nframes_t nframes, void *arg) 
{
	sample_t *out[2];
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


int main (int argc, char *argv[]) 
{
	
	int buflength = 20;
	char *client_name;
	jack_options_t jack_options = JackNullOption;
	jack_status_t status;
	int option_index;
	int opt;
	xmmsc_result_t *result;
	
	char his_input_port_names[2][80];
	
	GMainLoop *ml;	
	
	Gtk::Main kit( argc, argv );
	
	strcpy (his_input_port_names[0], "JackMix:return_l"); 
	strcpy (his_input_port_names[1], "JackMix:return_r");
	
	const char *options = "l:r:";
	struct option long_options[] = 
	{ {"leftOut", 1, 0, 'l'},
	{"rightOut", 1, 0, 'r'}, 
	{0, 0, 0, 0} };
	
	while ((opt = getopt_long (argc, argv, options, long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'l':
			strncpy (his_input_port_names[0], optarg, 79);
			his_input_port_names[0][79] = '\0';
			break;
		case 'r':
			strncpy (his_input_port_names[1], optarg, 79);
			his_input_port_names[1][79] = '\0';
			break;
		default:
			fprintf (stderr, "unknown option %c\n", opt); 
		}
	}

	notify_init("xmms2-jack-dj");
	notification = notify_notification_new("", NULL, ICON_PATH);
	
	jringbuf = jack_ringbuffer_create(3276800);
	
	/* open a client connection to the JACK server */
	
	client_name = (char *) malloc(80 * sizeof(char));
	char *tmp = "DigitalDJ";
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
	
	jack_set_process_callback (client, process, NULL);
	//jack_set_sync_callback (client, sync_callback, NULL);
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
	
	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		exit (1);
	}
	
//	if (jack_connect (client, jack_port_name(my_output_ports[0]), his_input_port_names[0])) {
//		fprintf (stderr, "cannot connect to port left output '%s'\n", his_input_port_names[0]);
//	}
	
//	if (jack_connect (client, jack_port_name(my_output_ports[1]), his_input_port_names[1])) {
//		fprintf (stderr, "cannot connect to port right output '%s'\n", his_input_port_names[1]);
//	}
	
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	
	espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 
	                  buflength, NULL, 0);
	
	espeak_SetSynthCallback(synth_callback);
	espeak_SetParameter(espeakVOLUME, 190, 0);
	espeak_SetParameter(espeakWORDGAP, 5, 0);
	espeak_SetParameter(espeakRATE, 76, 0);
	espeak_SetParameter(espeakPITCH, 200, 0);
	espeak_SetParameter(espeakRANGE, 200, 0);
	
	espeak_VOICE voice_spec;
	voice_spec.name = NULL;
	voice_spec.languages = "english-us";
	voice_spec.gender=1;
	voice_spec.age=20;
	voice_spec.variant = 3;
	espeak_SetVoiceByProperties(&voice_spec);
	
    //connection =  xmmsc_init ("DigitalDJ");
    //if (xmmsc_connect(connection, "tcp://192.168.1.88") == 0) {
    //	exit(1);
    //}
	
	async_connection =  xmmsc_init ("async_DigitalDJ");
	if (xmmsc_connect(async_connection, "tcp://192.168.1.88") == 0) {
		exit(1);
	}
	
	
	
	result = xmmsc_broadcast_playback_current_id (async_connection);
	xmmsc_result_notifier_set(result, current_id_callback, NULL);
	xmmsc_result_unref(result);
	
	xmmsc_mainloop_gmain_init(async_connection);
	Gtk::Main::run();
	g_main_loop_run (ml);
	
	jack_client_close (client);
	exit (0);
}
