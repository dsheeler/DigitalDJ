def options(opt):
  opt.load('compiler_cxx')

def configure(cnf):
  cnf.load('compiler_cxx')
  cnf.check_cfg(package='xmms2-client-cpp', args='--libs --cflags',
      uselib_store='XMMS2-CLIENT-CPP')
  cnf.check_cfg(package='xmms2-client-cpp-glib', args='--libs --cflags',
      uselib_store='XMMS2-CLIENT-CPP-GLIB')
  cnf.check_cfg(package='gtkmm-3.0', args='--libs --cflags',
      uselib_store='GTKMM-3.0')
  cnf.check_cfg(package='libnotify', args='--libs --cflags',
      uselib_store='NOTIFY')
  cnf.check(lib='mpdclient', uselib_store='MPDCLIENT')
  cnf.check(lib='asound', uselib_store='ASOUND')
  cnf.check(lib='jack', uselib_store='JACK')
  cnf.check(lib='omp', uselib_store='OMP')
  cnf.check(lib='ncurses', uselib_store='NCURSES')
  cnf.check(lib='eststring', uselib_store='ESTSTRING')
  cnf.check(lib='estbase', uselib_store='ESTBASE', use='ASOUND')
  cnf.check(lib='estools', uselib_store='ESTOOLS', use=['ASOUND', 'ESTBASE',
      'OMP', 'NCURSES'])
  cnf.check(lib='pthread', uselib_store='PTHREAD')
  cnf.check(lib='Festival', use=['ESTBASE', 'ESTOOLS', 'OMP', 'NCURSES',
      'ASOUND'], uselib_store='FESTIVAL')
  cnf.check_cxx(cxxflags=['-std=c++17', '-g', '-Wall'], uselib_store='FLAGS')


def build(bld):
  bld(features='cxx', source='MusicServerClient.cpp', target='MusicServerClient.o', use=['GTKMM-3.0'])
  bld(features='cxx', source='SpeechEngine.cpp', target='SpeechEngine.o', use=['FESTIVAL'])
  bld(features='cxx', source='FestivalSpeechEngine.cpp', target='FestivalSpeechEngine.o', use=['FESTIVAL'])
  bld(features='cxx', source='MpdClient.cpp', target='MpdClient.o', use=['MPDCLIENT', 'GTKMM-3.0'])
  bld(features='cxx', source='Xmms2dClient.cpp', target='Xmms2dClient.o', use=['XMMS2-CLIENT-CPP', 'GTKMM-3.0'])
  bld(features='cxx cxxprogram', source='digitalDJ.cpp',
            target='digitalDJ', use=['MpdClient.o', 'Xmms2dClient.o', 'MusicServerClient.o',
            'SpeechEngine.o', 'FestivalSpeechEngine.o',
            'FLAGS', 'JACK', 'ESTSTRING', 'ESTBASE',
            'ESTOOLS', 'FESTIVAL', 'NCURSES', 'NOTIFY', 'XMMS2-CLIENT-CPP-GLIB',
            'XMMS-CLIENT-CPP', 'GTKMM-3.0' ,'XMMS2-CLIENT-CPP', 'ASOUND',
            'MPDCLIENT', 'OMP'])
