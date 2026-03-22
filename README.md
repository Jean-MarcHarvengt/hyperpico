# hyperpico
Initially created as a GFX/Sound expansion for the commodore PET, the HyperPICO system now serves as:
* A standalone development platform for the MCUME project with USB/SD/WIFI/HDMI/AUDIO featuring but also 8MB QSPI PSRAM
* A GFX/Sound expansion for the TRS80 model I/III when combined the HyperTRSPICO interface board
* A full Z80 based computer (HyperZ80) when adding a Z80 CPU to the HyperTRSPICO board directly
* A GFX/Sound expansion for the Commodore PET 8032/4032 when combined the HyperPETPICO interface board
<br>
How does it work?
* The Raspberry PICO2 (RP2350) is the heart of the system
* It spies the memory bus of the interfaced system/CPU to act as RAM/ROM/IO memory space (using GPIO and PIO)
* It recreates and improves the graphics of the original system over HDMI (offering extra Sprites,Tiles and/or Bitmap layers)
* It offers Audio SID emulation (mono)
* It also provide 3MB of flash storage for programs...
* When used as standalone board, BUS GPIO interface is freed up for on board SD/USB/PSRAM connectivity to create a perfect emulation system using MCUME
<br><br>


