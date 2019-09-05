# Ponscripter-fork (steam)

![Travis-CI Build Status](https://travis-ci.org/sekaiproject/ponscripter-fork.svg)

## Introduction

Ponscripter was created as an NScripter-style visual novel game interpreter with an emphasis
on supporting games in Western languages.

This fork is intended to provide additional features to Ponscripter that make it more suitable for releasing
games on Steam. It, however, does not have qualms with breaking compatibility in minor ways. See the CHANGES file.

Other engines focussed on Western-language games include:

* [Ponscripter](http://unclemion.com/onscripter/releases/) &mdash; The project this is based on
* [ONScripter-EN](http://unclemion.com/onscripter/releases/) &mdash; An NScripter-compatible engine based on ONscripter
* [Ren'Py](http://www.renpy.org/) &mdash; A modern visual novel engine with python scripting support.

### Documentation

Ponscripter documentation is provided in the form of a set of manual
pages. Start with ponscripter(7).

You may also browse the documentation [online](https://sekaiproject.github.io/ponscripter-fork/doc/).


## Compatibility

This incarnation of the engine has primarily been tested with the [Steam Narcissu side 2nd](http://store.steampowered.com/app/264380) script.

We strive to keep the engine able to compile and run correctly without the Steam runtime/api, but
the Steam platforms are our primary targets.

The following platforms are supported:

* Nintendo Switch

Any issues on these platforms should be reported.

## Bugs

Please report all bugs with this project to the e-mail indicated in the git repository. Include your platform and, if possible, clear reproduction instructions.

## Acknowledgements

The original Ponscripter engine was maintained and, in great part, created by [Mion](http://unclemion.com).

Ponscripter is built primarily on Ogapee's work, without which there
would be nothing here.

The version of ONScripter taken as a base also includes notable
contributions from Chendo (original English support) and insani
(further enhancements).

The following people (in alphabetical order) have contributed 
noteworthy patches and bug reports that have enhanced Ponscripter:

* Agilis
* Daniel Oaks
* Euank
* Mion of Sonozaki Futago-tachi
* Roine Gustafsson
* Roto
* Starchanchan

Apologies to anyone not mentioned &mdash; please let me know so I can set the
record straight!

## Witch Hunt Fork

The Witch Hunt fork of this repo is brought to you with huge thanks to everyone mentioned above, but the latest changes are being maintained by chronotrig, so feel free to bother him first regarding any issues (chronotrig.dev@gmail.com).

This includes 2x mode, which can be used to support older Nscripter games at 2x resolution. See AnimationInfo.h for more details on usage. This feature will eventually be added as a config setting, but meanwhile, we at the Witch Hunt will be focusing on other various bugs and projects.

## Nintendo Switch Fork

This fork contains changes required to support Nintendo Switch, such as touch screen support and RomFS support.  
These changes are ported from ONScripter-Switch, located on the World Wide Web at the following URL: https://github.com/uyjulian/onscripter-switch  
This fork is maintained by Julian Uy, and he can be contacted at the e-mail indicated in the git repository.

## License

ONScripter is copyright © 2001-2007 Ogapee.  The Ponscripter fork is
copyright © 2006-2009 Haeleth.

Ponscripter is licensed under the GNU General Public License, version
2 or (at your option) any later version.  See COPYING for details.
