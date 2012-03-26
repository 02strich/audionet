=====
Audionet
=====

The goal of Audionet is transparently networked audio interfaces primarily for Windows. It is based arround a windows audio driver supporting multiple audio streaming procotols:

- raw PCM streaming
- RAOP or Airtunes 2 (in planning)

Installation
------------

Compile using the *make.cmd* using a WDK command prompt for your platform and install using the supplied *audionet.inf*. The driver is configurable through a custom property sheet in the device manager.
