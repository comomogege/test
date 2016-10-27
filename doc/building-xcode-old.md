##Build instructions for Xcode 7.2.1

**NB** These are outdated, please refer to [Building using Xcode][xcode] instructions.

###Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in Terminal go to **/Users/user/TBuild** and run

    git clone https://github.com/telegramdesktop/tdesktop.git

then go to **/Users/user/TBuild/tdesktop** and run

    git checkout mac32

###Prepare libraries

In your build Terminal run

    MACOSX_DEPLOYMENT_TARGET=10.6

to set minimal supported OS version to 10.6 for future console builds.

####zlib 1.2.8

http://www.zlib.net/ > Download [**zlib source code, version 1.2.8, zipfile format**](http://zlib.net/zlib128.zip)

Extract to **/Users/user/TBuild/Libraries**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/zlib-1.2.8** and run:

        prefix=/usr/local/zlib_old CFLAGS="-mmacosx-version-min=10.6" LDFLAGS="-mmacosx-version-min=10.6" ./configure
        make
        sudo make install

####OpenSSL 1.0.1g

Get sources from https://github.com/telegramdesktop/openssl-xcode, by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/openssl-xcode.git

http://www.openssl.org/source/ > Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

Extract openssl-1.0.1h.tar.gz and copy everything from **openssl-1.0.1h** to **/Users/user/TBuild/Libraries/openssl-xcode** to have **/Users/user/TBuild/Libraries/openssl-xcode/include**

#####Building library

* Open **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj** with Xcode
* Product > Build

####liblzma

http://tukaani.org/xz/ > Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/xz-5.0.5** and there run

    ./configure
    make
    sudo make install

####zlib 1.2.8

Using se system lib

####libexif 0.6.20

Get sources from https://github.com/telegramdesktop/libexif-0.6.20, by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/libexif-0.6.20** and there run

    ./configure
    make
    sudo make install

####OpenAL Soft

Get sources by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone git://repo.or.cz/openal-soft.git

to have **/Users/user/TBuild/Libraries/openal-soft/CMakeLists.txt**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/openal-soft/build** and there run

    cmake -D LIBTYPE:STRING=STATIC -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.6 -D CMAKE_INSTALL_PREFIX:STRING=/usr/local/openal_old ..
    make
    sudo make install

####Opus codec

Download sources [opus-1.1.tar.gz](http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz) from http://www.opus-codec.org/downloads/, extract to **/Users/user/TBuild/Libraries** and rename to have **/Users/user/TBuild/Libraries/opus/configure**

#####Building libraries

Download [pkg-config 0.28](http://pkgconfig.freedesktop.org/releases/pkg-config-0.28.tar.gz) from http://pkg-config.freedesktop.org, extract it to **/Users/user/TBuild/Libraries**

In Terminal go to **/Users/user/TBuild/Libraries/pkg-config-0.28** and run

    ./configure --with-internal-glib
    make
    sudo make install

then go to **/Users/user/TBuild/Libraries/opus** and there run

    ./configure
    make
    sudo make install

####FFmpeg

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.1

#####Building libraries

Download [libiconv-1.14](http://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.14.tar.gz) from http://www.gnu.org/software/libiconv/#downloading, extract it to **/Users/user/TBuild/Libraries**

In Termianl go to **/Users/user/TBuild/Libraries/libiconv-1.14** and run

    CFLAGS="-mmacosx-version-min=10.6" CPPFLAGS="-mmacosx-version-min=10.6" LDFLAGS="-mmacosx-version-min=10.6" ./configure --enable-static --prefix=/usr/local/iconv_old
    make
    sudo make install

Then in Terminal go to **/Users/user/TBuild/Libraries/ffmpeg** and run

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"

    brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

    ./configure --prefix=/usr/local/ffmpeg_old --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-cflags="-mmacosx-version-min=10.6" --extra-cxxflags="-mmacosx-version-min=10.6" --extra-ldflags="-mmacosx-version-min=10.6"

    make
    sudo make install

####Qt 5.3.2, slightly patched
#####Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone git://code.qt.io/qt/qt5.git qt5_3_2
    cd qt5_3_2
    git checkout 5.3
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.3.2
    cd qtimageformats && git checkout v5.3.2 && cd ..
    cd qtbase && git checkout v5.3.2 && cd ..

#####Apply the patch

From **/Users/user/TBuild/Libraries/qt5_3_2/qtbase**, run:

    git apply ../../../tdesktop/Telegram/Patches/qtbase_5_3_2.diff

From **/Users/user/TBuild/Libraries/qt5_3_2/qtimageformats**, run:

    git apply ../../../tdesktop/Telegram/Patches/qtimageformats_5_3_2.diff

#####Building library

Go to **/Users/user/TBuild/Libraries/qt5_3_2** and run:

    ./configure -prefix "/usr/local/tdesktop/Qt-5.3.2" -debug-and-release -force-debug-info -opensource -confirm-license -static -opengl desktop -nomake examples -nomake tests -platform macx-g++
    make -j4
    sudo make -j4 install

building (**make** command) will take really long time.

###Building Telegram Desktop

* Launch Xcode, all projects will be taken from **/Users/user/TBuild/tdesktop/Telegram**
* Open MetaEmoji.xcodeproj and build for Debug (Release optionally)
* Open MetaLang.xcodeproj and build for Debug (Release optionally)
* Open Telegram.xcodeproj and build for Debug
* Build Updater target as well, it is required for Telegram relaunch
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram target settings (Apple LLVM 6.1 - Custom Compiler Flags > Other C / C++ Flags > Release)

[xcode]: building-xcode.md
