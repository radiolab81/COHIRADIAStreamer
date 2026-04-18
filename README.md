# COHIRADIA Streaming Client based on liquiddsp / QT
This is a streaming client/player software for the COHIRADIA project by Hermann Scharfetter.

https://www.cohiradia.org/de/

https://www.radiomuseum.org/dsp_cohiradia.cfm

![main1](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui1.jpg)

Following the basic principles of cohiplayer_smi (https://github.com/radiolab81/cohiplayer_smi) , the streaming client is more of a real desktop app. Based on liquid-dsp (https://liquidsdr.org/) and QT, it can stream IQ-WAV Cohiradia recordings or the COHILiveNetwork to the smisdr (https://github.com/radiolab81/smisdr) via highspeed ethernet connection. It supports the entire range of DACs provided by smisdr (from 8- to 16-bit RF DACs) as well as the ultra-low-cost osmo-fl2k sdr transmitter and other  TCP/IP based SDRs from the radiolab81 group.

![mainsw](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui_sw.jpg)

Due to the high sample rates enabled by SMI on the Raspberry Pi, playback of shortwave recordings in the 49m band is also possible, if the streaming PC has sufficient processing power and bandwidth. According to our tests, sample rates of 25 MSPS are achievable on modern PCs with a direct Gigabit connection to the DAC, without any dropped samples occurring in the SMISDR. (god bless DMA and SMI)

If you are using the client in conjunction with the budget solution osmo-fl2k, please use `sudo fl2k_tcp -p 1235 -s 10000000` and `socat` as a TCP/IP bridge: `socat -u TCP4-LISTEN:1234,reuseaddr TCP4-LISTEN:1235,reuseaddr`  Set the IP address to 127.0.0.1 (localhost) and use a sample rate of 10 MSPS, which the FL2K handles very well.

![mainfl2k](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui_fl2k.jpg)

To build the COHIRADIAStreamer, please install the following dependencies and prerequisites.

```console
sudo apt update
sudo apt install build-essential cmake qtbase5-dev qt5-qmake libliquid-dev qt6-base-dev libqt6widgets6 libxkbcommon-dev
```
build the app:

```console
git clone https://github.com/radiolab81/COHIRADIAStreamer
cd COHIRADIAStreamer
mkdir build
cd build
cmake ..
make
```

running the app:
```console
./COHIRADIAStreamer
```
