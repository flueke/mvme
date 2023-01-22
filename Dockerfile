# syntax=docker/dockerfile:1

# TODO: ctest requires google benchmark and google test
# TODO: convert to multistage: build and runtime containers

# TODO: trying to run the mvme container
# docker build -t mvme/latest . && docker run  --rm --name=mvme -e DISPLAY --volume="/tmp/.X11-unix:/tmp/.X11-unix:rw" --env="QT_X11_NO_MITSHM=1" mvme/latest
# Running the above command does open up the mvme windows but no mouse
# interaction was possible on my home machine. The container had to be killed
# from a second terminal.
# Access to USB devices probably also requires some additional work.

# TODO: might be better to build an AppImage instead for binary releases. Use
# docker to create native debian/ubuntu/... packages.

FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash build-essential git cmake ninja-build ca-certificates \
    qtbase5-dev qtbase5-dev-tools libqt5websockets5-dev libqt5opengl5-dev libqt5svg5-dev \
    libqwt-qt5-dev libquazip5-dev libusb-dev zlib1g-dev libgraphviz-dev libboost-dev \
    sphinx-common sphinx-rtd-theme-common python3-sphinx python3-sphinxcontrib.qthelp \
    texlive-latex-base texlive-latex-extra texlive-latex-recommended latexmk texlive-fonts-recommended

WORKDIR /root
RUN git clone --recurse-submodules https://github.com/flueke/mvme.git \
    && mkdir build && cd build \
    && cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/mvme -DMVME_BUILD_TESTS=OFF ../mvme \
    && ninja && ninja install

CMD [ "/bin/bash", "-c", "source /mvme/bin/initMVME && mvme" ]