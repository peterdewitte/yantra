#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# stage_perms.sh STAGE   (NFR-09, used by build_deb.sh)
# Sets explicit modes on a .deb staging tree, whatever the parent folder
# or umask: folders 0755 without setuid/setgid (a setgid parent such as a
# shared build disk is inherited by new folders and dpkg-deb refuses a
# DEBIAN folder with mode 2755), files 0755 when executable, else 0644.
set -euo pipefail
stage=${1:?usage: stage_perms.sh STAGE}
# A numeric 0755 keeps the setgid bit of a folder (GNU and uutils chmod):
# clear it by name.
find "$stage" -type d -exec chmod u=rwx,go=rx,ug-s {} +
find "$stage" -type f -perm /111 -exec chmod 0755 {} +
find "$stage" -type f ! -perm /111 -exec chmod 0644 {} +
