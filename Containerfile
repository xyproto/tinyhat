FROM registry.fedoraproject.org/fedora:44
RUN dnf -y install \
	bc binutils-x86_64-linux-gnu bison bsdtar bubblewrap bzip2 cpio curl diffutils dosfstools \
	elfutils-libelf-devel file findutils flex gcc gcc-c++ gcc-x86_64-linux-gnu git glibc-common gzip \
	ImageMagick kmod libarchive libxml2 lz4 make mtools openssl openssl-devel pacman patch perl procps-ng \
	python3 python3-lxml python3-numpy python3-pillow rsync squashfs-tools util-linux util-linux-core \
	vorbis-tools which xfsprogs xorriso xz zstd \
	&& dnf clean all
