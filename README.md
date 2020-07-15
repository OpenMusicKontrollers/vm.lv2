## VM

### A programmable virtual machine LV2 plugin

This plugin bundle contains various plugins with a programmable stack-based
virtual machine to modify up to 8 LV2 Control, CV, Audio, Atom and MIDI signals.

To be used when that specific simple filter plugin you desperately need does
not yet exist.

#### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/vm.lv2/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/vm.lv2/commits/master)

### Binaries

For GNU/Linux (64-bit, 32-bit, armv7), Windows (64-bit, 32-bit) and MacOS
(64/32-bit univeral).

To install the plugin bundle on your system, simply copy the __vm.lv2__
folder out of the platform folder of the downloaded package into your
[LV2 path](http://lv2plug.in/pages/filesystem-hierarchy-standard.html).

#### Stable release

* [vm.lv2-0.10.0.zip](https://dl.open-music-kontrollers.ch/vm.lv2/stable/vm.lv2-0.10.0.zip) ([sig](https://dl.open-music-kontrollers.ch/vm.lv2/stable/vm.lv2-0.10.0.zip.sig))

#### Unstable (nightly) release

* [vm.lv2-latest-unstable.zip](https://dl.open-music-kontrollers.ch/vm.lv2/unstable/vm.lv2-latest-unstable.zip) ([sig](https://dl.open-music-kontrollers.ch/vm.lv2/unstable/vm.lv2-latest-unstable.zip.sig))

### Sources

#### Stable release

* [vm.lv2-0.10.0.tar.xz](https://git.open-music-kontrollers.ch/lv2/vm.lv2/snapshot/vm.lv2-0.10.0.tar.xz)
([sig](https://git.open-music-kontrollers.ch/lv2/vm.lv2/snapshot/vm.lv2-0.10.0.tar.xz.asc))

#### Git repository

* <https://git.open-music-kontrollers.ch/lv2/vm.lv2>

### Packages

* [ArchLinux](https://www.archlinux.org/packages/community/x86_64/vm.lv2/)

### Bugs and feature requests

* [Gitlab](https://gitlab.com/OpenMusicKontrollers/vm.lv2)
* [Github](https://github.com/OpenMusicKontrollers/vm.lv2)

### Plugins

![VM ](/screenshots/screenshot_1.png)

#### Control VM

Virtual machine for LV2 Control ports. Features 8 inputs and 8 outputs.

#### CV VM

Virtual machine for LV2 Control Voltage ports. Features 8 inputs and 8 outputs.

#### Audio VM

Virtual machine for LV2 Audio ports. Features 8 inputs and 8 outputs.

#### Atom VM

Virtual machine for LV2 Atom event ports. Features 8 inputs and 8 outputs.

#### MIDI VM

Virtual machine for LV2 MIDI event ports. Features 8 inputs and 8 outputs.

### License

Copyright (c) 2017-2020 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.
