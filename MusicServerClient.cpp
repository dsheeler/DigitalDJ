#include "MusicServerClient.h"

MusicServerClient::MusicServerClient(Glib::RefPtr<Glib::MainLoop> ml) {
    this->ml = ml;
}

Glib::RefPtr<Gdk::Pixbuf> MusicServerClient::scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {
    const int width = pixbuf->get_width();
    const int height = pixbuf->get_height();
    int dest_width = 128;
    int dest_height = 128;
    double ratio = width / static_cast< double >(height);
    
    if( width > height ) {
        dest_height = static_cast< int >(dest_height / ratio);
    }
    else if( height > width ) {
        dest_width = static_cast< int >(dest_width * ratio);
    }
    return pixbuf->scale_simple( dest_width, dest_height, Gdk::INTERP_BILINEAR );
}
