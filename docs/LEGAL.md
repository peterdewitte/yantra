<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Legal and scope note

What Yantra receives and decodes, what it sends, and the licences that come with it (DOC-05). This note describes the program; it is not legal advice.

## 1 Receive-only

Yantra and its radio plugins only receive. No radio plugin transmits, and Yantra never registers with a network. A TETRA terminal used in terminal mode (chapter 9 of the [user manual](manual/README.md)) is only asked for its own measurements; Yantra makes no calls and sends no messages through it.

## 2 What is decoded, and what is not

Yantra is for broadcast and system information, levels and quality only. The radio plugins decode, from the cells' downlink:

- the level of every carrier heard, and the quality figures of the carriers they synchronise to;
- the cell identity (country and network code, location area, colour code, carrier);
- the broadcast parameters a terminal needs to select a cell (access, power and reselection parameters, services offered) and the neighbour cell list.

Not decoded, by design: no short data messages (SDS), no subscriber or group addresses, no voice, no call set-up or traffic, and no decryption of any kind. The decoder Yantra's plugins use (osmo-tetra) is built without its crypto part. Air-interface encryption is neither broken nor bypassed, and nothing that identifies a person is decoded, stored or shown. Only measurement records reach Yantra: no samples and no messages ([measurement method note](METHOD.md)).

## 3 Your responsibility

Receiving radio signals, and in particular decoding the broadcast information of a network, may be regulated, licensed or forbidden where you use Yantra. Before you measure, check the law of the country, and the agreement with the network operator and the owner of the building you measure in. You alone are responsible for how you use Yantra and what you do with its results.

## 4 Privacy and network use

Yantra sends nothing by itself. In particular:

- **Sharing of exports and reports**: files are sent only when you choose a sharing method (a shared folder, an upload address or your own mail server) and ask for it (manual 10.4). Nothing is sent before.
- **Network listener**: Yantra opens no network socket without the probe plugin; with it, the remote API listens only after it is started (in the Remote probes window or with `api.listen` of the headless probe). Switch the probe plugin off to be sure (manual 14.1).
- **MAK floor plans of the tour**: downloaded from the MAK's media server only after you agree (section 5); this is the only other request Yantra makes.
- **GSMTAP output** of the radio plugins: off by default; when you switch it on, it sends broadcast information to the address you set (127.0.0.1 by default, this PC).

Yantra does not collect usage data, does not check for updates and has no crash reporter. Projects, recordings and reports stay where you save them. Reports carry the surveyor's name you type, nothing more.

## 5 Licences

- **Yantra** is free software under the GNU General Public License, version 3 or later. Third-party libraries, tools, fonts and data are listed with their licences in [THIRD_PARTY.md](../THIRD_PARTY.md).
- **Radio plugins** (radio parts) contain parts under the GNU Affero General Public License, version 3 (osmo-tetra). The complete corresponding source of the program, including those parts, is available at the source code link in Help > About and in the [README](../README.md) (AGPL-3.0 section 13). If you change the plugins and let others use them over a network, you must offer them your source in the same way.
- **Demo plans, Fraunces Tavern**: from the Historic American Buildings Survey (HABS NY-6321), in the public domain. Credit: "Historic American Buildings Survey, Library of Congress".
- **Tour plans of the MAK** (Museum of Applied Arts, Vienna: a beer house in Vienna, Haus Claeson in Falsterbo and Villa Bunzl in Vienna, by Josef Frank): licensed CC BY-NC-ND 4.0 by the MAK. They are never shipped with Yantra: the tour downloads them on first use, after you agree, into your own cache, checks them by SHA-256, and shows them unchanged with their credit and licence. Use them for non-commercial practice only, and do not pass on changed copies.
- **curtat.be name and logo**: a trademark of Curtat BV, shown in the About dialog and in reports; not covered by the GPL. Forks must remove or replace the logo and must not suggest they come from curtat.be.
- **The Yantra logo** is part of Yantra, under the GPL.
