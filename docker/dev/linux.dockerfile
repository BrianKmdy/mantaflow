FROM ubuntu:20.04

# Install required tools and libraries
# Set frontend to non-interactive to avoid timezone prompt
ENV DEBIAN_FRONTEND=noninteractive 
RUN apt update\
    && apt install -y cmake g++ git doxygen python3-dev qt5-qmake qt5-default

WORKDIR /project

CMD cd build\
    && cmake .. -DOPENMP=OFF -DGUI=ON -DPYTHON_VERSION=3.8\
    && cmake --build .