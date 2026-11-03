# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests of the generator library (RAD-70, RAD-93): channel coding against a
# separate test-only reference decoder (ref_decoder.*), known answers, PDU
# field layouts and round trips (ref_pdu.*).
yantra_add_test_dir(gen LIBS yantra::gen)
