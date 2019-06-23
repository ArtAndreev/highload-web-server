FROM centos:7

WORKDIR /build

RUN yum -y install gcc make autoconf

COPY . .

WORKDIR vendor/libevent-2.1.8-stable
RUN ./configure && \
     make && \
     make install
ENV LD_LIBRARY_PATH="/usr/local/lib:${LD_LIBRARY_PATH}"

WORKDIR ../../
RUN make

EXPOSE 80

CMD ["./bin/server", "-c", "etc/httpd.conf"]
