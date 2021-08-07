#!/bin/bash
# based on the script install-ffmpeg from svnpenn/a/install-ffmpeg.sh (givin' credit where it's due :)
# uses an (assumed installed via package) cross compiler to compile ffmpeg

check_missing_packages () {

  local check_packages=('pkg-config' 'make' 'git' 'autoconf' 'automake' 'yasm' 'i686-w64-mingw32-gcc' 'i686-w64-mingw32-g++' 'x86_64-w64-mingw32-g++' 'libtool' 'nasm')

  for package in "${check_packages[@]}"; do
    type -P "$package" >/dev/null || missing_packages=("$package" "${missing_packages[@]}")
  done

  if [[ -n "${missing_packages[@]}" ]]; then
    clear
    echo "Could not find the following execs: ${missing_packages[@]}"
    echo ""
    echo "on ubuntu: sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 yasm make automake autoconf git pkg-config libtool-bin nasm gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 -y"
    echo 'Install the missing packages before running this script.'
    exit 1
  fi
}

check_missing_packages
set -x

cpu_count="$(grep -c processor /proc/cpuinfo 2>/dev/null)" # linux cpu count
if [ -z "$cpu_count" ]; then
  cpu_count=`sysctl -n hw.ncpu | tr -d '\n'` # OS X cpu count
  if [ -z "$cpu_count" ]; then
    echo "warning, unable to determine cpu count, defaulting to 1"
    cpu_count=1 # else default to just 1, instead of blank, which means infinite
  fi
fi

mkdir -p sandbox_quick
cd sandbox_quick
echo "I am in $(pwd)"

#type=win32 # win32 or win64
rm -rf ./install_root

for type in win32 win64
do
echo "Now build $type system."
host=x86_64-w64-mingw32
if [[ $type == win32 ]]; then
  host=i686-w64-mingw32
fi

prefix=$(pwd)/install_root/$type
export PKG_CONFIG_PATH="$prefix/x264/lib/pkgconfig" # let ffmpeg find our dependencies [currently not working :| ]

# x264
x264_dir=x264
if [ ! -d $(pwd)/$x264_dir ]; then
  echo "$x264_dir is not exited!"
  rm -rf x264
  git clone --depth 1 http://repo.or.cz/r/x264.git || exit 1
fi

cd x264
# --enable-static       library is built by default but not installed
# --enable-win32thread  avoid installing pthread
./configure --host=$host --enable-static --enable-win32thread --cross-prefix=$host- --prefix=$prefix/x264 || exit 1
make -j$cpu_count
make install
cd ..
  
# and ffmpeg
ffmpeg_dir=ffmpeg_4.2
if [ ! -d $(pwd)/$ffmpeg_dir ]; then
  rm -rf $ffmpeg_dir.tmp.git
  git clone --depth 1 https://github.com/FFmpeg/FFmpeg.git $ffmpeg_dir.tmp.git
  mv $ffmpeg_dir.tmp.git $ffmpeg_dir
fi

cd $ffmpeg_dir
  # not ready for this since we don't reconfigure after changes: # git pull
    arch=x86_64
    if [[ $type == win32 ]]; then
      arch=x86
    fi
	make clean
	echo  "Build ffmpeg($arch) begin..."
    # shouldn't really ever need these?
    ./configure --enable-gpl --enable-libx264 --enable-nonfree --enable-shared --disable-static\
      --arch=$arch --target-os=mingw32 \
	  --disable-ffmpeg  --disable-ffplay --disable-ffprobe \
	  --disable-doc \
	  --disable-optimizations \
	  --enable-version3 --disable-iconv \
	  --disable-symver \
	  --disable-programs \
	  --disable-postproc \
	  --disable-decoders  --enable-decoder=h264 --enable-decoder=mpeg4 --enable-decoder=aac --enable-decoder=mp3 --enable-decoder=pcm_s16le \
      --disable-encoders \
      --disable-demuxers --enable-demuxer=rtsp --enable-demuxer=flv --enable-demuxer=h264 --enable-demuxer=wav --enable-demuxer=aac --enable-demuxer=hls --enable-demuxer=mp3 \
      --disable-muxers --enable-muxer=rtsp --enable-muxer=flv --enable-muxer=h264 --enable-muxer=mp4 --enable-muxer=wav \
	  --disable-protocols --enable-protocol=rtmp --enable-protocol=hls --enable-protocol=tcp --enable-protocol=https --enable-protocol=http --enable-protocol=file \
	  --disable-parsers --enable-parser=mpeg4video --enable-parser=aac --enable-parser=h264 \
	  --disable-bsfs \
      --disable-indevs \
      --disable-outdevs \
      --disable-filters \
	  --disable-devices \
	  --extra-ldflags="-static-libgcc" \
      --cross-prefix=$host- --pkg-config=pkg-config --prefix=$prefix/ffmpeg_simple_installed || exit 1
  #rm **/*.a # attempt force a kind of rebuild...
  make -j$cpu_count && make install && echo "done installing it $prefix/ffmpeg_simple_installed"
cd ..

done
