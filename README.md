# COHIRADIA Streaming Client based on liquiddsp / QT
This is a streaming client/player software for the COHIRADIA project by Hermann Scharfetter.

https://www.cohiradia.org/de/

https://www.radiomuseum.org/dsp_cohiradia.cfm

![main1](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui1.jpg)

Following the basic principles of cohiplayer_smi (https://github.com/radiolab81/cohiplayer_smi) , the streaming client is more of a real desktop app. Based on liquid-dsp (https://liquidsdr.org/) and QT, it can stream IQ-WAV Cohiradia recordings or the COHILiveNetwork to the smisdr (https://github.com/radiolab81/smisdr) via highspeed ethernet connection. It supports the entire range of DACs provided by smisdr (from 8- to 16-bit RF DACs) as well as the ultra-low-cost osmo-fl2k sdr transmitter and other  TCP/IP based SDRs from the radiolab81 group.

![mainmw](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mw.jpg)

![mainsw](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui_sw.jpg)

![mainsw2](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/49m.jpg)

Due to the high sample rates enabled by SMI on the Raspberry Pi, playback of shortwave recordings in the 49m band is also possible, if the streaming PC has sufficient processing power and bandwidth. According to our tests, sample rates of 25 MSPS are achievable on modern PCs with a direct Gigabit connection to the DAC, without any dropped samples occurring in the SMISDR. (god bless DMA and SMI)

If you are using the client in conjunction with the budget solution osmo-fl2k, please use `sudo fl2k_tcp -p 1235 -s 10000000` and `socat` as a TCP/IP bridge: `socat -u TCP4-LISTEN:1234,reuseaddr TCP4-LISTEN:1235,reuseaddr`  Set the IP address to 127.0.0.1 (localhost) and use a sample rate of 10 MSPS, which the FL2K handles very well.

![mainfl2k](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/mainui_fl2k.jpg)

## 🚀 NEW: Direct I/Q Streaming & FPGA DUC (In-Band Signaling)

We now support direct I/Q streaming for the FPGA gateware of [smiSDR](https://github.com/radiolab81/smisdr/blob/main/gateware/README.md) and **parlioSDR**!


![mainsw3](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/COHIRADIAStreamer_IQ_20M.jpg)

Previously, the *COHIRADIAStreamer* had to perform the resource-intensive Digital Up-Conversion (DUC) in software and transmit the ready-to-use RF samples over the network. Reaching higher target frequencies, such as the shortwave (HF) bands, inevitably led to extreme network bandwidth requirements. Because of this, reaching the HF bands was mostly limited to setups like the Raspberry Pi 4 equipped with a true Gigabit Ethernet connection. The Raspberry Pi 4 was the only machine, providing I/Q software DUC too.

With the newly introduced **I/Q Mode (16 Bit with In-Band Signaling)**, the Digital Up-Converter is shifted directly into the gateware. 

### ✨ Key Advantages:
* **Drastically Reduced Network Bandwidth:** Since only the I/Q baseband needs to be transmitted at the native file sample rate (e.g., 250 kHz), the required data rate drops significantly. Up-converting into the 15m band (e.g., at 18.96 MHz) now requires merely **~8.0 MBit/s** of network bandwidth!

  ![mainsw4](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/netw_bw_15m.jpg)

  ![mainsw5](https://github.com/radiolab81/COHIRADIAStreamer/blob/main/www/15m.jpg)
  
* **Shortwave over 100 MBit/s and ESP32:** Thanks to this massive reduction in bandwidth, you no longer need a Gigabit connection. Shortwave transmission is now perfectly feasible on older Raspberry Pi models limited to 100 MBit/s Ethernet, or even on the ESP32P4 (**parlioSDR**)! 
* **Reduced Host CPU Load:** The highly demanding software up-sampling processes, like the resampler and NCO mixer, are entirely bypassed on the COHIRADIAStreamer host PC, smisdr or parlioSDR-Device.

### 🛠 Technical Implementation & GUI Usage:
Using this mode is straightforward:
1. Select a file in the GUI and check the box **"I/Q Mode: SW or FPGA DUC (16 Bit with In-Band Signaling)"**.
2. Options like *Samplerate* and *DAC Bits* are automatically disabled since the streamer natively extracts these parameters directly from the WAV file metadata.
3. Upon starting the stream, the application calculates the Phase Tuning Word (FTW) for the NCO frequency shift and sends initialization commands directly within the data stream to the FPGA.
4. The I/Q data is transmitted as a 14-bit payload with a 2-bit tag in the MSB (Bit 15:14: `00` for I, `01` for Q), allowing the FPGA to easily separate and process the streams. See https://github.com/radiolab81/smisdr/blob/main/gateware/README.md#-protocol-review-in-band-signaling-over-smibus--parlio for more details.



## To build the COHIRADIAStreamer, please install the following dependencies and prerequisites.

```console
sudo apt update
sudo apt install build-essential cmake git qtbase5-dev qt5-qmake libliquid-dev qt6-base-dev libqt6widgets6 libxkbcommon-dev
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
