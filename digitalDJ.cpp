#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <xmmsclient/xmmsclient++.h>
#include <xmms2/xmmsclient/xmmsclient++-glib.h>
#include <glib-2.0/glib.h>
#include <gtkmm.h>
#include <notification.h>
#include <notify.h>
#include <gdk/gdk.h>
#include <gdkmm-2.4/gdkmm/pixbufloader.h>
#include <gdkmm-2.4/gdkmm/pixbuf.h>
#include <festival.h>

#include <cogl/cogl.h>
#include <cogl-pango/cogl-pango.h>
#include <clutter/clutter.h>

#include <cstdlib>
#include <string>
#include <iostream>

#define SAY_BUF_SIZE 8000
#define ICON_PATH "/usr/local/share/icons/hicolor/64x64/apps/xmms2.png"

typedef jack_default_audio_sample_t sample_t;

#define SHADOW_X_OFFSET         3
#define SHADOW_Y_OFFSET         3

static void
_text_paint_cb (ClutterActor *actor)
{
  PangoLayout *layout;
  guint8 real_opacity;
  CoglColor color;
  ClutterText *text = CLUTTER_TEXT (actor);
  ClutterColor text_color = { 0, };

  /* Get the PangoLayout that the Text actor is going to paint */
  layout = clutter_text_get_layout (text);

  /* Get the color of the text, to extract the alpha component */
  clutter_text_get_color (text, &text_color);

  /* Composite the opacity so that the shadow is correctly blended */
  real_opacity = clutter_actor_get_paint_opacity (actor)
               * text_color.alpha
               / 255;

  /* Create a #ccc color and premultiply it */
  cogl_color_init_from_4ub (&color, 0x20, 0x90, 0x180, real_opacity);
  cogl_color_premultiply (&color);

  /* Finally, render the Text layout at a given offset using the color */
  cogl_pango_render_layout (layout, SHADOW_X_OFFSET, SHADOW_Y_OFFSET, &color, 0);
}


NotifyNotification *notification;

jack_port_t  *my_output_ports[2], *his_input_ports[2];
jack_client_t *client;
jack_ringbuffer_t *jringbuf;

const char *err_buf;
int intval;

void log (const char *pattern, ...)
{
	va_list arguments;
	FILE *logfile;

	logfile = fopen ("/tmp/DDJ.log", "a");
	fprintf (logfile, pattern);
	fclose(logfile);
}

Glib::RefPtr< Gdk::Pixbuf > scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {

  const int width = pixbuf->get_width();
  const int height = pixbuf->get_height();
  int dest_width = 64;
  int dest_height = 64;
  double ratio = width / static_cast< double >(height);

  if( width > height ) {
    dest_height = static_cast< int >(dest_height / ratio);
  }
  else if( height > width ) {
    dest_width = static_cast< int >(dest_width * ratio);
  }
  return pixbuf->scale_simple( dest_width, dest_height, Gdk::INTERP_BILINEAR );
}

static void signal_handler(int sig) {
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void festival_Synth(const char *text_to_say) {
  /*Strip out annoying [Explicit] from title.*/
  std::string edited_text(text_to_say);
  std::size_t found = edited_text.find("[Explicit]");
  if (found != edited_text.npos) {
    edited_text.erase(found, 10);
  }

  EST_Wave wave;
  festival_text_to_wave(edited_text.c_str(), wave);
  double scale = 1/32768.0;
  wave.resample(48000);

  int numsamples = wave.num_samples();
  sample_t jbuf[numsamples];

  for (int i = 0; i < numsamples; i++) {
    jbuf[i] =  wave(i) * scale;
  }

  size_t num_bytes_to_write;

  num_bytes_to_write = numsamples*sizeof(sample_t);

  do {
    int nwritten = jack_ringbuffer_write(jringbuf, (char*)jbuf, num_bytes_to_write);
    if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
      usleep(100000);
    }
    num_bytes_to_write -= nwritten;

  } while (num_bytes_to_write > 0);
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



class DDJXmms2Client {
  public:
    DDJXmms2Client();
    ~DDJXmms2Client();

    bool my_current_id(const int& id);
    bool error_handler(const std::string& error);
    bool handle_bindata(const Xmms::bin& data);

  private:
    Xmms::Client client_;
    Xmms::Client sync_client_;
};

DDJXmms2Client::DDJXmms2Client()
 : client_(std::string("DigitalDJ")),
   sync_client_(std::string("SyncDigitalDJ")) {

  client_.connect(std::getenv("XMMS_PATH"));
  sync_client_.connect(std::getenv("XMMS_PATH"));

  client_.playback.broadcastCurrentID()(
   Xmms::bind( &DDJXmms2Client::my_current_id, this ),
   Xmms::bind( &DDJXmms2Client::error_handler, this )
  );

  client_.setMainloop( new Xmms::GMainloop( client_.getConnection() ) );

  /*
  * Initialize and run glib mainloop, check out glib documentation
  * for this.
  */
  GMainLoop* ml = g_main_loop_new( 0, 0 );
  g_main_loop_run( ml );

}

DDJXmms2Client::~DDJXmms2Client() { }

bool DDJXmms2Client::handle_bindata(const Xmms::bin& data) {

  Glib::RefPtr< Gdk::Pixbuf > image;

  try {
    Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
    std::cout << data.size() << " is size of bindata." << std::endl;

    loader->write( data.c_str(), data.size() );
    loader->close();
    image = scale_pixbuf( loader->get_pixbuf() );
    notify_notification_set_image_from_pixbuf (notification, image->gobj());
	}
  catch ( Glib::Error& e ) {
    std::clog << "Could not load image: " << e.what() << std::endl;
  }

  GError *gerr = 0;
  notify_notification_show (notification, &gerr);


  return true;
}

bool DDJXmms2Client::my_current_id(const int& id) {

  std::string val, say, msg;

  msg += "xmms2d: ";

  try {
    Xmms::Dict info = sync_client_.medialib.getInfo(id);

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
      client_.bindata.retrieve(val)(
       Xmms::bind( &DDJXmms2Client::handle_bindata, this ),
       Xmms::bind( &DDJXmms2Client::error_handler, this )
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
/*
  if(!xmmsv_dict_get (infos, "picture_front", &dict_entry) || !xmmsv_get_string (dict_entry, &val)) {
    val = NULL;
  }

  if (val != NULL) {
    result = xmmsc_bindata_retrieve(async_connection, val);
    xmmsc_result_wait (result);
    return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value) && xmmsv_get_error (return_value, &err_buf)) {
      fprintf (stderr, "bindata retrieve returns error, %s\n", err_buf);
    } else {
      handle_bindata(return_value);
    }
  }

*/
  notify_notification_update (notification, msg.c_str(), NULL, NULL);
  jack_ringbuffer_reset(jringbuf);

  festival_Synth(say.c_str());
  return true;

}

bool DDJXmms2Client::error_handler( const std::string& error )
{

	/*
	 * This is the error callback function which will get called
	 * if there was an error in the process.
	 */
	std::cout << "Error: " << error << std::endl;
	return false;

}

void setup_jack() {
  char *client_name;
  jack_options_t jack_options = JackNullOption;
  jack_status_t status;

  jringbuf = jack_ringbuffer_create(3276800);

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
}

void setup_festival() {

  int heap_size = 2000000;  // default scheme heap size
  int load_init_files = 1; // we want the festival init files loaded

  festival_initialize(load_init_files,heap_size);

  festival_eval_command("(voice_cmu_us_slt_arctic_clunits)");
  festival_Synth("Hi.  I am your synthetic xmms2, jack DJ.  I hope you like my voice!");

}

void setup_signal_handler() {
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
}

int main (int argc, char *argv[]) {
  /*ClutterActor *stage;
  ClutterActor *text;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  stage = clutter_stage_new ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Text shadow");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_main_quit), NULL);

  text = clutter_text_new ();
  clutter_text_set_text (CLUTTER_TEXT (text), "Hello, World!");
  clutter_text_set_font_name (CLUTTER_TEXT (text), "Sans 64px");
  clutter_actor_add_constraint (text, clutter_align_constraint_new (stage, CLUTTER_ALIGN_X_AXIS, 0.5));
  clutter_actor_add_constraint (text, clutter_align_constraint_new (stage, CLUTTER_ALIGN_Y_AXIS, 0.5));
  g_signal_connect (text, "paint", G_CALLBACK (_text_paint_cb), NULL);

  clutter_container_add (CLUTTER_CONTAINER (stage), text, NULL);

  clutter_actor_show (stage);

  clutter_main ();*/


  Gtk::Main kit( argc, argv );

  notify_init("xmms2-jack-dj");
  notification = notify_notification_new("", NULL, ICON_PATH);
  setup_jack();
  setup_festival();
  setup_signal_handler();

  try {
    DDJXmms2Client myclient;
  }
  catch( Xmms::connection_error& err ) {
    std::cout << "Connection failed: " << err.what() << std::endl;
    return EXIT_FAILURE;
  }

  jack_client_close (client);
  return EXIT_SUCCESS;

}

