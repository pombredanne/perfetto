FROM debian:stretch
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get -y update
RUN apt-get -y install git python2.7 gcc-6 build-essential
RUN adduser --disabled-password --gecos '' bot
USER bot
COPY job_runner.sh /home/bot/job_runner.sh
CMD bash /home/bot/job_runner.sh
